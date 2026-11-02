"""Single-owner serial transport for the Qt keyboard panel.

The board protocol is the project's existing PLAYER CONNECT / PLAYER INPUT
session protocol, not raw key characters. INPUT is silent; only CONNECT and
ordinary commands wait for replies. No board is opened at import or in tests.

The keyboard feature is opt-in. It never changes APP ownership, a scene, an
input-source setting, firmware, assets, or a shield mode. A new session always
starts neutral. A session revoked by the board is not automatically renewed.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import queue
import re
import threading
import time

import gold_game_panel as core

KEY_MASKS = {"W": 0x01, "A": 0x02, "S": 0x04, "D": 0x08, "SPACE": 0x20}
ALLOWED_MASK = 0x2F
HEARTBEAT_SECONDS = 0.030
GUI_LEASE_SECONDS = 0.150
STATUS_PERIOD_SECONDS = 1.0
CONNECT_COMMAND = core.Command("PLAYER CONNECT", ("PLAYER CONNECTED ",), 2.0)


def parse_session(reply: str) -> int:
    match = re.fullmatch(r"PLAYER CONNECTED ([0-9A-Fa-f]{8})", reply)
    if not match:
        raise core.PanelError("PLAYER CONNECT 回包不是有效的 8 位会话标识；未开始键盘输入。")
    return int(match[1], 16)


def encode_input(session: int, sequence: int, mask: int) -> bytes:
    if (not isinstance(session, int) or not 0 <= session <= 0xFFFFFFFF
            or not isinstance(sequence, int) or not 0 <= sequence <= 0xFFFFFFFF
            or not isinstance(mask, int) or mask < 0 or mask & ~ALLOWED_MASK):
        raise ValueError("Invalid session, sequence, or keyboard mask")
    return f"PLAYER INPUT {session:08X} {sequence:08X} {mask:02X}\n".encode("ascii")


@dataclass(frozen=True)
class InputSnapshot:
    mask: int
    updated_at: float | None
    generation: int


class InputMailbox:
    """Latest full key state, never a FIFO of old key presses.

    GUI calls update(mask) every ~30 ms while capture is active. clear() is an
    invalidation edge: later update() calls cannot revive the old generation.
    This is important when deactivation occurs during a slow CONNECT reply.
    """

    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self._lock = threading.Lock()
        self._mask = 0
        self._updated_at = None
        self._generation = 0

    def update(self, mask: int):
        if not isinstance(mask, int) or mask < 0 or mask & ~ALLOWED_MASK:
            raise ValueError("Unsupported key mask")
        with self._lock:
            self._mask = mask
            self._updated_at = self.clock()

    def clear(self):
        with self._lock:
            self._mask = 0
            self._updated_at = None
            self._generation += 1

    def snapshot(self) -> InputSnapshot:
        with self._lock:
            return InputSnapshot(self._mask, self._updated_at, self._generation)


class KeyboardProtocol(core.Protocol):
    """All writes and reads occur on the worker thread, including heartbeats."""

    def __init__(self, ser, journal, emit, stop, mailbox, clock=time.monotonic):
        super().__init__(ser, journal, emit, stop, clock)
        self.mailbox = mailbox
        self.session = None
        self.sequence = 0
        self.keyboard_enabled = False
        self.generation = None
        self.started_at = 0.0
        self.last_sent_at = 0.0
        self.last_mask = 0
        self.next_status_at = float("inf")

    def _keyboard_event(self, reason):
        self.emit("keyboard", {"enabled": self.keyboard_enabled,
                               "session": self.session,
                               "reason": reason})

    def _write_input(self, mask):
        """One full silent INPUT packet; never retry a partial/unknown write."""
        if self.fatal or self.session is None:
            raise core.PanelError("输入连接不可用；未发送按键。")
        data = encode_input(self.session, self.sequence, mask)
        text = data[:-1].decode("ascii")
        self.journal.event("TX_ATTEMPT", command=text, hex=data.hex(), keyboard=True)
        try:
            n = self.ser.write(data)
        except Exception as exc:
            self.fatal = True
            self.journal.event("TX_OUTCOME_UNKNOWN", error=str(exc), keyboard=True)
            raise core.PanelError("键盘串口写入异常，发送结果未知；不自动重发。") from exc
        if not isinstance(n, int) or not 0 <= n <= len(data):
            self.fatal = True
            raise core.PanelError("键盘写入返回长度无效；不自动重发。")
        self.journal.accepted(data[:n])
        if n != len(data):
            self.fatal = True
            raise core.PanelError("键盘命令仅部分写入；不补发、不追加释放包。")
        self.emit("input_tx", {"mask": mask, "seq": self.sequence,
                               "session": self.session, "text": text,
                               "scope": "accepted_by_host_serial_driver_not_board_ack"})
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        self.last_sent_at = self.clock()
        self.last_mask = mask

    def enable_keyboard(self):
        if self.keyboard_enabled:
            return "键盘已接管。"
        # Discard held/stale keys before the potentially slow board handshake.
        self.mailbox.clear()
        start_generation = self.mailbox.snapshot().generation
        state = core.query(self, "enemy")["fields"]
        if (state.get("GAME") != "1" or state.get("MOVING") != "1"
                or state.get("GAME_OVER") != "0"):
            raise core.PanelError("请先进入正在运行的游戏，再开启键盘接管；未更改 APP 或游戏配置。")
        reply = self.transact(CONNECT_COMMAND)
        self.session = parse_session(reply)
        self.sequence = 0
        self._write_input(0)  # Required sequence-zero neutral packet, no ACK wait.
        if self.mailbox.snapshot().generation != start_generation or self.stop.is_set():
            self.disable_keyboard("接管过程中窗口失焦或已取消，保持释放。", send_zero=False)
            return "键盘接管已取消。"
        self.generation = start_generation
        self.started_at = self.clock()
        self.next_status_at = self.started_at + STATUS_PERIOD_SECONDS
        self.keyboard_enabled = True
        self.journal.event("KEYBOARD_ENABLED", session=f"{self.session:08X}",
                           heartbeat_seconds=HEARTBEAT_SECONDS,
                           gui_lease_seconds=GUI_LEASE_SECONDS)
        self._keyboard_event("已建立输入会话；按键按下/松开实时发送，每秒刷新游戏状态。")
        return "键盘接管已开启。"

    def disable_keyboard(self, reason="键盘已释放。", send_zero=True):
        had_session = self.session is not None
        # Set false before writing: reentrant callbacks cannot retain an active
        # stream when the neutral packet itself fails.
        self.keyboard_enabled = False
        self.next_status_at = float("inf")
        self.mailbox.clear()
        failure = None
        if had_session and send_zero and not self.fatal:
            try:
                self._write_input(0)
            except core.PanelError as exc:
                failure = exc
        self.session = None
        self.generation = None
        self.journal.event("KEYBOARD_DISABLED", reason=reason,
                           neutral_attempted=bool(had_session and send_zero),
                           release_error=str(failure) if failure else None)
        self._keyboard_event(reason)
        if failure:
            raise failure

    def tick_input(self):
        if not self.keyboard_enabled or self.fatal:
            return
        now = self.clock()
        snapshot = self.mailbox.snapshot()
        if self.stop.is_set() or snapshot.generation != self.generation:
            self.disable_keyboard("键盘捕获已释放，需要重新开启接管。")
            return
        last_gui = self.started_at if snapshot.updated_at is None else snapshot.updated_at
        if now - last_gui >= GUI_LEASE_SECONDS:
            self.disable_keyboard("界面输入心跳超时，已发送释放；请重新开启键盘接管。")
            return
        mask = 0 if snapshot.updated_at is None else snapshot.mask
        if mask != self.last_mask or now - self.last_sent_at >= HEARTBEAT_SECONDS:
            self._write_input(mask)

    @staticmethod
    def revoked_reason(line):
        if line.startswith("BUILD:"):
            return "观察到固件重新启动，原键盘会话已撤销。"
        if re.match(r"^(?:ENEMY (?:STOPPED|EXITED|RESTARTED)|PLAYER DISCONNECTED|GAME OVER)(?:\s|$)", line):
            return "观察到停止、退出或游戏结束；重新开始后请重新开启键盘接管。"
        if line.startswith("ENEMY STATUS "):
            f = core.fields(line)
            if f.get("GAME_OVER") == "1" or f.get("MOVING") == "0" or f.get("GAME") == "0" or f.get("STATE") in ("IDLE", "EXITED"):
                return "游戏已停止或结束，原键盘会话已释放。"
        return None

    def read_lines(self):
        lines = super().read_lines()
        for line in lines:
            if self.keyboard_enabled and core.protocol_rejection(line):
                self.disable_keyboard("板端拒绝键盘/护盾请求，已释放接管。")
                raise core.PanelError(f"板端拒绝：{line}；没有重连或自动重试。")
            reason = self.revoked_reason(line) if self.keyboard_enabled else None
            if reason:
                # The firmware may already have revoked the token: do not send
                # another packet into that invalid session.
                self.disable_keyboard(reason, send_zero=False)
        self.tick_input()
        return lines

    def poll_game_if_due(self):
        if self.keyboard_enabled and self.clock() >= self.next_status_at:
            self.next_status_at = self.clock() + STATUS_PERIOD_SECONDS
            core.query(self, "enemy")


class KeyboardWorker(threading.Thread):
    """Qt-independent thread with the same events as the original panel worker."""

    def __init__(self, root, port, logs, requests, events, stop, mailbox):
        super().__init__(daemon=True)
        self.root, self.port, self.logs = root, port, logs
        self.requests, self.events, self.stop = requests, events, stop
        self.mailbox = mailbox

    def emit(self, kind, data):
        self.events.put((kind, data))

    def perform(self, proto, action, invert=0):
        if action == "keyboard_on":
            return proto.enable_keyboard()
        if action == "keyboard_off":
            proto.disable_keyboard("键盘接管已关闭。")
            return "键盘接管已关闭。"
        if action == "shield_key":
            if not proto.keyboard_enabled:
                raise core.PanelError("键盘未接管；Tab 没有发送护盾命令。")
            # A discrete event from the GUI. QKeyEvent auto-repeat is filtered
            # there, not expanded into repeated shield commands here.
            return proto.transact(core.COMMANDS["shield"])
        if proto.keyboard_enabled:
            if action == "shield":
                return proto.transact(core.COMMANDS["shield"])
            if action in ("stop", "exit"):
                proto.disable_keyboard("即将停止或退出游戏，先释放键盘。")
            else:
                raise core.PanelError("键盘接管中，请先释放接管再执行诊断或场景操作。")
        return core.run_action(proto, action, invert)

    def run(self):
        ser = log = proto = None
        try:
            identity = core.verify_pair(self.root)
            import serial  # Only after explicit connect + unchanged local identity checks.
            log = core.Journal(self.logs, identity, self.port)
            self.emit("log", str(log.path))
            ser = serial.Serial(port=None, baudrate=115200, bytesize=8, parity="N", stopbits=1,
                                timeout=0.005, write_timeout=0.1, xonxoff=False, rtscts=False, dsrdtr=False)
            ser.port, ser.dtr, ser.rts = self.port, False, False
            ser.open()
            log.event("PORT_OPEN", dtr=False, rts=False, automatic_commands=[])
            proto = KeyboardProtocol(ser, log, self.emit, self.stop, self.mailbox)
            self.emit("connected", "已连接；尚未接管键盘，未自动发送配置。")
            while not self.stop.is_set() and not proto.fatal:
                try:
                    # Poll incoming data before taking another request, and
                    # keep the latest key state moving even with an empty queue.
                    proto.read_lines()
                    proto.poll_game_if_due()
                    try:
                        action, invert = self.requests.get_nowait()
                    except queue.Empty:
                        continue
                    try:
                        log.event("USER_ACTION", action=action, invert=invert)
                        result = self.perform(proto, action, invert)
                        self.emit("done", result if isinstance(result, str) else "查询完成。")
                    finally:
                        self.emit("busy", False)
                except core.PanelError as exc:
                    if proto.keyboard_enabled or proto.session is not None:
                        try:
                            proto.disable_keyboard("串口操作失败，已停止键盘接管。", send_zero=not proto.fatal)
                        except core.PanelError:
                            pass
                    log.event("ACTION_FAILED", error=str(exc))
                    self.emit("error", str(exc))
                    self.emit("busy", False)
        except Exception as exc:
            if log:
                log.event("CONNECTION_FAILURE", error=f"{type(exc).__name__}: {exc}")
            self.emit("error", f"{type(exc).__name__}: {exc}")
        finally:
            if proto is not None:
                try:
                    proto.disable_keyboard("串口已断开，键盘捕获已释放。", send_zero=not proto.fatal)
                except Exception as exc:
                    self.emit("error", "最后的释放包未确认写入：" + str(exc))
            if ser is not None:
                try:
                    ser.close()
                except Exception as exc:
                    self.emit("error", "关闭串口失败：" + str(exc))
            if log:
                log.close(bytes(proto.partial) if proto else b"")
            self.emit("disconnected", "串口已关闭；没有发送 EXIT、复位或清事件。")

