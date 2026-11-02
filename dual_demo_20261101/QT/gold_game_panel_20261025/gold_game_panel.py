"""Small Tk panel for the frozen GOLD_SHIELD_FLOW candidate. Host tools only.

No serial access at import, in check-only, or in tests. No reset, upload, retry,
buffer purge or automatic game exit. Every board command comes from a button.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import queue
import re
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

FW = "GOLD_SHIELD_FLOW_20261025_CANDIDATE_PENDING_BOARD"
DEFAULT_ROOT = r"C:\Users\Administrator\Desktop\1\three_renderer_async_quad_20261006\validation\gold_shield_flow_20261025\sol"
MANIFEST_SHA = "bb26ba4b90206fb78eb2e3077b2eff4f8046c4d3617d4cd9514d0e8edb5c24fd"
PAIR = {
    "bit": ("pair/ti60f225_oob.bit", "12666ad6b3566690e0a64ac3549ed1ad339419a86325e9446fcd866be8fe5576", 2837364),
    "elf": ("pair/apbTest.elf", "1cf4dd9ef7ca5ce362fbe194221bedf0ac73bb48c31919c78d3554f670ecabb7", 200952),
    "map": ("pair/apbTest.map", "0f60ae8556420b1130b7fb6a877551c20af7203900a35c52eb8f292a57da89a7", 218783),
}
REASONS = ["NONE", "TRANSACTION_TIMEOUT", "BYTE_TIMEOUT", "IDLE_BUSY",
           "IDLE_LINES", "START_FLAGS", "START_NOT_BUSY", "TX_NO_VALID",
           "TX_NACK", "RX_NO_VALID", "STOP_FLAGS", "RESERVED11",
           "STOP_LINES", "READY_TIMEOUT", "CONFIG_MISMATCH", "PAIR_TIMEOUT",
           "SERVICE_GAP", "TIME_BACKWARD", "RECOVERY_LINES", "RESERVED19"]


class PanelError(Exception):
    pass


class BuildMismatch(PanelError):
    pass


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def verify_pair(root):
    root = Path(root).resolve()
    manifest = root / "delivery_pairs.json"
    if sha256(manifest) != MANIFEST_SHA:
        raise PanelError("delivery_pairs.json SHA 不匹配；此面板只配套本次 GOLD 候选。")
    doc = json.loads(manifest.read_text(encoding="utf-8-sig"))
    if doc.get("candidate_available") is False or doc.get("device_access_allowed") is False:
        raise PanelError("清单禁止使用此候选。")
    c = doc["candidate"]
    if (c.get("firmware_id"), c.get("joy_status_abi"), c.get("joy_diag_abi")) != (FW, 2, 1):
        raise PanelError("固件身份或 JOY ABI 不匹配。")
    checked = {}
    for kind, (relative, expected, size) in PAIR.items():
        row = c[kind]
        if (row.get("path"), row.get("sha256"), row.get("bytes")) != (relative, expected, size):
            raise PanelError(kind + " 清单与面板固定身份不符。")
        path = (root / relative).resolve()
        if not path.is_relative_to(root) or path.stat().st_size != size or sha256(path) != expected:
            raise PanelError(kind + " 路径、长度或 SHA 不匹配。")
        checked[kind] = {"path": str(path), "sha256": expected, "bytes": size}
    return {"firmware_id": FW, "manifest_sha256": MANIFEST_SHA, "files": checked,
            "identity_scope": "LOCAL_SHA_PLUS_OPERATOR_DECLARATION_NOT_TARGET_READBACK"}


def fields(line):
    return dict(re.findall(r"\b([A-Z][A-Z0-9_]*)=([^\s]+)", line))


def parse_line(line):
    for prefix, count, abi, kind in (("PLAYER JOY DIAG ABI=", 96, 1, "diag"),
                                     ("PLAYER JOY ABI=", 32, 2, "joy")):
        if not line.startswith(prefix):
            continue
        match = re.fullmatch(re.escape(prefix) + r"(\d+) WORDS=([0-9 ]+)", line)
        if not match or int(match[1]) != abi:
            raise PanelError("不支持或损坏的 " + kind + " ABI 行。")
        w = [int(x) for x in match[2].split()]
        if len(w) != count or any(x > 0xFFFFFFFF for x in w):
            raise PanelError(kind + " 词数或 u32 范围错误。")
        if kind == "diag":
            if w[0] != 1:
                raise PanelError("DIAG 内容 ABI 不符。")
            def reason(start):
                return REASONS[w[start+3]] if w[start] and w[start+3] < 20 else ("NONE" if not w[start] else "UNKNOWN")
            return {"kind": kind, "retry_epoch": w[1], "counts": dict(zip(REASONS, w[2:22])),
                    "first": reason(22), "retry_first": reason(59)}
        flags = [(w[29] >> (8 * i)) & 255 for i in range(4)]
        if w[31] != 0 or w[30] not in (0, 1) or any(v not in (0, 1) for v in flags):
            raise PanelError("JOY 布尔字段或保留词异常。")
        def s16(v):
            v &= 65535
            return v - 65536 if v & 32768 else v
        return {"kind": kind, "sequence": w[18], "epoch": w[19], "pairs": w[20],
                "faults": w[21], "error": w[22], "valid": w[30], "enabled": flags[0],
                "armed": flags[1], "centered": flags[2], "latched": flags[3],
                "x": s16(w[28]) if w[30] else None,
                "y": s16(w[28] >> 16) if w[30] else None}
    for prefix, kind in (("STATUS ", "status"), ("ASSET STATUS ", "assets"),
                         ("ENEMY STATUS ", "enemy"), ("ENEMY FLOW ", "flow"),
                         ("ENEMY EFFECTS ", "effects")):
        if line.startswith(prefix):
            return {"kind": kind, "fields": fields(line), "raw": line}
    return None


def protocol_rejection(line):
    # Field names such as ERROR=00000000 and BUSY=0 are not error messages.
    if re.match(r"^(ERR|ERROR|BUSY|NAK|REJECTED)(?:\s|:|$)", line):
        return True
    return bool(re.search(r"(?:^|\s)(?:ERR|ERROR|BUSY|REJECTED|FAILED|UNSUPPORTED)(?:\s|:|$)", line))


@dataclass(frozen=True)
class Command:
    text: str
    prefixes: tuple[str, ...]
    timeout: float = 4.0


COMMANDS = {
    "status": Command("STATUS", ("STATUS ",)),
    "assets": Command("ASSET STATUS", ("ASSET STATUS ",)),
    "enemy": Command("ENEMY STATUS", ("ENEMY STATUS ",)),
    "joy": Command("PLAYER JOY STATUS", ("PLAYER JOY ABI=",)),
    "diag": Command("PLAYER JOY DIAG", ("PLAYER JOY DIAG ABI=",)),
    "flow": Command("ENEMY FLOW", ("ENEMY FLOW ",)),
    "effects": Command("ENEMY EFFECTS", ("ENEMY EFFECTS ",)),
    "prepare": Command("ENEMY FLOW LIGHT_PRESSURE", ("ENEMY FLOW ",)),
    "move": Command("ENEMY MOVE", ("ENEMY MOVING", "ENEMY SHOWN"), 12.0),
    "shield": Command("PLAYER SHIELD", ("PLAYER SHIELD ACCEPTED",)),
    "stop": Command("ENEMY STOP", ("ENEMY STOPPED",), 12.0),
    "exit": Command("ENEMY EXIT", ("ENEMY EXITED",), 12.0),
}


class Journal:
    def __init__(self, base, identity, port):
        self.path = Path(base) / ("panel_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ"))
        self.path.mkdir(parents=True, exist_ok=False)
        self.rx = (self.path / "rx.raw.bin").open("wb")
        self.tx = (self.path / "tx.accepted.bin").open("wb")
        self.events = (self.path / "events.jsonl").open("w", encoding="utf-8")
        self.observed_build = None
        self.start_ns = time.monotonic_ns()
        (self.path / "identity.json").write_text(json.dumps(dict(identity, port=port,
            loaded_identity_evidence="operator declaration", baudrate=115200), ensure_ascii=False, indent=2), encoding="utf-8")
        self.event("OPEN_REQUEST")

    def event(self, kind, **data):
        self.events.write(json.dumps(dict(monotonic_ns=time.monotonic_ns(), event=kind, **data), ensure_ascii=False) + "\n")
        self.events.flush()

    def receive(self, data):
        self.rx.write(data)
        self.rx.flush()
        self.event("RX", bytes=len(data), hex=data.hex())

    def accepted(self, data):
        self.tx.write(data)
        self.tx.flush()
        self.event("TX_ACCEPTED_BY_DRIVER", bytes=len(data), hex=data.hex())

    def close(self, partial=b""):
        (self.path / "partial_rx.bin").write_bytes(partial)
        self.event("CLOSE", observed_build=self.observed_build, automatic_exit_sent=False)
        for f in (self.rx, self.tx, self.events):
            f.close()


class Protocol:
    def __init__(self, ser, journal, emit, stop, clock=time.monotonic):
        self.ser, self.journal, self.emit, self.stop, self.clock = ser, journal, emit, stop, clock
        self.partial = bytearray()
        self.fatal = False
        self.flow_prepared = False

    def read_lines(self):
        data = self.ser.read(min(self.ser.in_waiting or 1, 4096))
        if not data:
            return []
        self.journal.receive(data)
        self.partial.extend(data)
        result = []
        while b"\n" in self.partial:
            raw, _, remaining = self.partial.partition(b"\n")
            self.partial[:] = remaining
            line = raw.rstrip(b"\r").decode("ascii", errors="backslashreplace")
            result.append(line)
            self.emit("rx", line)
            if line.startswith("BUILD:"):
                self.flow_prepared = False
                self.journal.observed_build = line.split(":", 1)[1].strip()
                self.emit("build", self.journal.observed_build)
                if self.journal.observed_build != FW:
                    self.fatal = True
                    raise BuildMismatch("实际收到的 BUILD 与 GOLD 候选不符；已停止发送。")
            parsed = parse_line(line)
            if parsed:
                self.emit("decoded", parsed)
                self.journal.event("DECODED", record=parsed)
        if len(self.partial) > 65536:
            self.fatal = True
            raise PanelError("接收行超过 64 KiB；停止发送并保留原始字节。")
        return result

    def transact(self, command):
        if self.fatal or self.stop.is_set():
            raise PanelError("连接已停止。")
        if not command.text or any(ord(c) < 32 or ord(c) > 126 for c in command.text):
            raise PanelError("命令必须是单行可打印 ASCII。")
        # Preserve already waiting bytes before writing; never purge a buffer.
        before = self.clock()
        while self.ser.in_waiting:
            self.read_lines()
            if self.clock() - before > 0.5:
                raise PanelError("串口持续有未处理输出；本次未发命令，请先查看回包。")
        if self.partial:
            raise PanelError("收到未结束的串口行；本次未发命令，原始字节已保留。")
        data = command.text.encode("ascii") + b"\n"
        self.journal.event("TX_ATTEMPT", command=command.text, hex=data.hex())
        try:
            n = self.ser.write(data)
        except Exception as exc:
            self.fatal = True
            self.journal.event("TX_OUTCOME_UNKNOWN", error=str(exc))
            raise PanelError("串口写入异常，实际发送前缀未知；不自动重发。") from exc
        if n is None or not 0 <= n <= len(data):
            self.fatal = True
            raise PanelError("串口返回无效写入长度；不自动重发。")
        self.journal.accepted(data[:n])
        self.emit("tx", command.text)
        if n != len(data):
            self.fatal = True
            raise PanelError("串口只接受了部分命令；不自动补发。")
        end = self.clock() + command.timeout
        received = []
        match = None
        while self.clock() < end and not self.stop.is_set():
            for line in self.read_lines():
                received.append(line)
                if protocol_rejection(line):
                    self.journal.event("COMMAND_REJECTED", command=command.text, reply=line)
                    raise PanelError(f"板端拒绝「{command.text}」：{line}；已停止本次操作，不重试。")
                if any(line == p or (p.endswith(" ") and line.startswith(p)) or
                       (p.endswith("=") and line.startswith(p)) or line.startswith(p + " ")
                       for p in command.prefixes):
                    match = line
            if match is not None:
                self.journal.event("COMMAND_REPLY", command=command.text, reply=match,
                                   correlation="single outstanding request; wire has no request ID")
                return match
        if self.stop.is_set():
            raise PanelError("用户断开；没有发送 EXIT 或取消命令。")
        detail = "没有收到完整回包" if not received else "收到回包但不符合此命令的完成格式：" + received[-1][:180]
        self.journal.event("COMMAND_TIMEOUT", command=command.text, lines=received, partial_hex=self.partial.hex())
        # A delayed reply cannot safely be associated with a subsequent request.
        self.fatal = True
        raise PanelError(f"{command.text}：{detail}；已停止发送，不自动重试。")


def query(proto, name):
    line = proto.transact(COMMANDS[name])
    parsed = parse_line(line)
    if parsed is None or parsed["kind"] != name:
        raise PanelError("查询回包格式不符：" + name)
    return parsed


def require_assets(record):
    f = record["fields"]
    try:
        okay = (int(f["VALID_MASK"], 16) & 0x0FFFFFFE == 0x0FFFFFFE
                and int(f["ACTIVE"], 16) == 0 and int(f["ERROR"], 16) == 0)
    except (KeyError, ValueError):
        okay = False
    if not okay:
        raise PanelError("素材未齐或上传器不空闲；本面板不上传，请查看 ASSET STATUS。")


def require_enemy(record, inactive=False):
    f = record["fields"]
    try:
        okay = int(f["ERROR"], 16) == 0 and f["MOVING"] == "0"
        if inactive:
            okay = okay and f["STATE"] == "IDLE"
    except (KeyError, ValueError):
        okay = False
    if not okay:
        raise PanelError("需要游戏已停止" + ("并退出到 IDLE" if inactive else "") + "；没有发送后续配置。")


def run_action(proto, action, invert=0):
    if action in ("status", "assets", "enemy", "joy", "diag", "flow", "effects"):
        return query(proto, action)
    if action == "refresh":
        for name in ("status", "assets", "enemy", "joy", "diag", "flow", "effects"):
            query(proto, name)
        return "各项查询完成。"
    if action == "center":
        require_enemy(query(proto, "enemy"))
        samples = [query(proto, "joy"), query(proto, "joy")]
        if any(not s["valid"] or s["error"] or s["latched"] or not s["enabled"] for s in samples):
            raise PanelError("尚无有效、无故障的 XY；未发送 CENTER。")
        if samples[0]["epoch"] != samples[1]["epoch"] or samples[0]["sequence"] == samples[1]["sequence"]:
            raise PanelError("两次查询未取得同 epoch 的新 XY 对；未发送 CENTER。")
        x, y = samples[-1]["x"], samples[-1]["y"]
        if invert not in range(4) or not (1024 <= x <= 31743 and 1024 <= y <= 31743):
            raise PanelError("中心超出固件允许范围；松开摇杆后再操作。")
        if any(abs(samples[0][k] - samples[1][k]) > 256 for k in ("x", "y")):
            raise PanelError("两次 XY 波动较大；未发送 CENTER，请松开摇杆。")
        proto.transact(Command(f"PLAYER JOY CENTER {x} {y} {invert}", ("PLAYER JOY OK",)))
        return f"已发送实测中心 X={x} Y={y}，反向位={invert}。开始后回中至少 100 ms。"
    if action == "prepare":
        state = query(proto, "status")["fields"]
        if state.get("GAME_ONLY") != "1" or state.get("SAFE_IDLE") != "1" or state.get("ERROR") != "00000000":
            raise PanelError("STATUS 未报告安全 idle；未发送模式配置。")
        require_assets(query(proto, "assets"))
        require_enemy(query(proto, "enemy"), inactive=True)
        query(proto, "flow")
        proto.transact(COMMANDS["prepare"])
        proto.flow_prepared = True
        return "轻量→压力配置已回复。点击「开始/继续」，用 KEY1 开火取得 10 个确认击杀。"
    if action == "move":
        require_assets(query(proto, "assets"))
        enemy = query(proto, "enemy")
        require_enemy(enemy)
        if not proto.flow_prepared and enemy["fields"].get("GAME") != "1":
            raise PanelError("还未配置本局：先点击「配置轻量→压力」，再点击开始。")
        query(proto, "flow")
        line = proto.transact(COMMANDS["move"])
        return line + "；回中至少 100 ms，查看 ARM=1 后推杆。"
    if action == "shield":
        f = query(proto, "enemy")["fields"]
        if f.get("GAME") != "1" or f.get("MOVING") != "1" or f.get("GAME_OVER") != "0":
            raise PanelError("开盾需要 GAME=1、MOVING=1 且未死亡。")
        return proto.transact(COMMANDS["shield"])
    if action in ("stop", "exit"):
        result = proto.transact(COMMANDS[action])
        if action == "exit":
            proto.flow_prepared = False
            query(proto, "status")
        return result
    raise PanelError("未知面板操作。")


class Worker(threading.Thread):
    def __init__(self, root, port, logs, requests, events, stop):
        super().__init__(daemon=True)
        self.root, self.port, self.logs = root, port, logs
        self.requests, self.events, self.stop = requests, events, stop

    def emit(self, kind, data):
        self.events.put((kind, data))

    def run(self):
        ser = log = proto = None
        try:
            identity = verify_pair(self.root)
            import serial  # Only after explicit connection and successful local verification.
            log = Journal(self.logs, identity, self.port)
            self.emit("log", str(log.path))
            ser = serial.Serial(port=None, baudrate=115200, bytesize=8, parity="N", stopbits=1,
                                timeout=0.05, write_timeout=2, xonxoff=False, rtscts=False, dsrdtr=False)
            ser.port, ser.dtr, ser.rts = self.port, False, False
            ser.open()
            log.event("PORT_OPEN", dtr=False, rts=False, automatic_commands=[])
            proto = Protocol(ser, log, self.emit, self.stop)
            self.emit("connected", "已连接；点击「刷新全部」或单项状态按钮。")
            while not self.stop.is_set() and not proto.fatal:
                try:
                    action, invert = self.requests.get_nowait()
                except queue.Empty:
                    proto.read_lines()
                    continue
                try:
                    log.event("USER_BUTTON", action=action, invert=invert)
                    result = run_action(proto, action, invert)
                    self.emit("done", result if isinstance(result, str) else "查询完成，见当前读数与日志。")
                except PanelError as exc:
                    log.event("ACTION_FAILED", action=action, error=str(exc))
                    self.emit("error", str(exc))
                finally:
                    self.emit("busy", False)
        except Exception as exc:
            if log:
                log.event("CONNECTION_FAILURE", error=f"{type(exc).__name__}: {exc}")
            self.emit("error", f"{type(exc).__name__}: {exc}")
        finally:
            if ser is not None:
                try:
                    ser.close()
                except Exception as exc:
                    self.emit("error", "关闭串口失败：" + str(exc))
            if log:
                log.close(bytes(proto.partial) if proto else b"")
            self.emit("disconnected", "串口已关闭；没有发送 EXIT、复位或清事件。")


def launch_gui(args):
    import tkinter as tk
    from tkinter import ttk, scrolledtext

    class Panel:
        def __init__(self, win):
            self.win, self.worker = win, None
            self.connected = self.busy = self.closing = False
            self.events, self.requests = queue.Queue(), queue.Queue(maxsize=1)
            self.stop = threading.Event()
            self.buttons = []
            win.title("金盾游戏 · 简易控制面板")
            win.geometry("1030x790")
            win.minsize(860, 700)
            style = ttk.Style(win)
            if "clam" in style.theme_names():
                style.theme_use("clam")
            style.configure("TButton", padding=(12, 9), font=("Microsoft YaHei UI", 10))
            style.configure("TLabel", font=("Microsoft YaHei UI", 10))
            style.configure("Title.TLabel", font=("Microsoft YaHei UI", 19, "bold"), foreground="#8a5c05")
            style.configure("Step.TLabel", font=("Microsoft YaHei UI", 11, "bold"))
            outer = ttk.Frame(win, padding=18)
            outer.pack(fill="both", expand=True)
            ttk.Label(outer, text="金盾游戏控制", style="Title.TLabel").pack(anchor="w")
            ttk.Label(outer, text="校准 → 轻量模式 → 10 杀自动淡化进入压力模式").pack(anchor="w", pady=(2, 12))
            line = ttk.Frame(outer)
            line.pack(fill="x")
            ttk.Label(line, text="候选目录").pack(side="left")
            self.root = tk.StringVar(value=args.root)
            ttk.Entry(line, textvariable=self.root).pack(side="left", fill="x", expand=True, padx=8)
            ttk.Label(line, text="串口").pack(side="left")
            self.port = tk.StringVar(value=args.port)
            ttk.Entry(line, textvariable=self.port, width=9).pack(side="left", padx=8)
            self.connect_btn = ttk.Button(line, text="连接", command=self.connect)
            self.connect_btn.pack(side="left")
            self.disconnect_btn = ttk.Button(line, text="断开串口", command=self.disconnect, state="disabled")
            self.disconnect_btn.pack(side="left", padx=(6, 0))
            self.declared = tk.BooleanVar(value=False)
            ttk.Checkbutton(outer, text="已手动加载本次 GOLD bit + ELF，COM 口独占且无待处理回复（操作者声明）",
                            variable=self.declared).pack(anchor="w", pady=8)
            self.identity = tk.StringVar(value="本地文件连接时核验；当前未读到板端 BUILD。")
            ttk.Label(outer, textvariable=self.identity, foreground="#666666").pack(anchor="w")

            readbox = ttk.LabelFrame(outer, text="1  查看当前状态", padding=8)
            readbox.pack(fill="x", pady=(12, 8))
            for title, action in (("刷新全部", "refresh"), ("基础状态", "status"), ("素材", "assets"),
                                  ("XY 读数", "joy"), ("故障详情", "diag"), ("游戏", "enemy"),
                                  ("转场", "flow"), ("护盾", "effects")):
                b = ttk.Button(readbox, text=title, command=lambda a=action: self.submit(a))
                b.pack(side="left", padx=3)
                self.buttons.append(b)
            self.joy = tk.StringVar(value="X —   Y —    ARM —    ERROR —    FAULTS —")
            self.game = tk.StringVar(value="游戏：尚未查询    普通弹可见数：—")
            self.fx = tk.StringVar(value="护盾 / 转场：尚未查询")
            for var in (self.joy, self.game, self.fx):
                ttk.Label(outer, textvariable=var).pack(anchor="w", pady=2)

            center = ttk.LabelFrame(outer, text="2  松开摇杆，保持静止，然后校准", padding=8)
            center.pack(fill="x", pady=8)
            b = ttk.Button(center, text="用新 XY 校准中心", command=lambda: self.submit("center"))
            b.pack(side="left", padx=3)
            self.buttons.append(b)
            self.flipx, self.flipy = tk.BooleanVar(), tk.BooleanVar()
            ttk.Checkbutton(center, text="X 反向", variable=self.flipx).pack(side="left", padx=12)
            ttk.Checkbutton(center, text="Y 反向", variable=self.flipy).pack(side="left")
            ttk.Label(center, text="校准需停止；开始 / 恢复后回中 ≥100 ms，再推杆。", foreground="#666666").pack(side="left", padx=15)

            play = ttk.LabelFrame(outer, text="3  游戏操作 · 按顺序点击", padding=8)
            play.pack(fill="x", pady=(0, 8))
            for title, action in (("配置轻量→压力", "prepare"), ("开始 / 继续", "move"),
                                  ("开启永久金盾", "shield"), ("暂停", "stop"), ("退出游戏", "exit")):
                b = ttk.Button(play, text=title, command=lambda a=action: self.submit(a))
                b.pack(side="left", padx=4)
                self.buttons.append(b)
            ttk.Label(outer, text="KEY1 按住开火，松手停火；KEY2 开盾。关闭面板只断开串口，游戏会继续运行。",
                      foreground="#666666").pack(anchor="w")
            self.notice = tk.StringVar(value="先关闭 miniterm / 采集器，再勾选已加载并连接。")
            self.notice_label = tk.Label(outer, textvariable=self.notice, anchor="w", justify="left", wraplength=960,
                                        bg="#f3ebd7", fg="#68470a", padx=10, pady=9, font=("Microsoft YaHei UI", 10))
            self.notice_label.pack(fill="x", pady=10)
            self.text = scrolledtext.ScrolledText(outer, height=12, wrap="word", state="disabled",
                                                font=("Consolas", 10), bg="#132027", fg="#e3e9e8")
            self.text.pack(fill="both", expand=True)
            self.logpath = tk.StringVar(value="日志自动保存到本面板旁的 board_logs；不同状态回包不代表同一帧。")
            ttk.Label(outer, textvariable=self.logpath, wraplength=960, foreground="#666666").pack(anchor="w", pady=(6, 0))
            self.set_buttons()
            win.protocol("WM_DELETE_WINDOW", self.close)
            win.after(50, self.pump)

        def show(self, text, error=False):
            self.notice.set(text)
            self.notice_label.configure(bg="#fbe5e3" if error else "#f3ebd7", fg="#9a201b" if error else "#68470a")

        def append(self, text):
            self.text.configure(state="normal")
            self.text.insert("end", text + "\n")
            if int(self.text.index("end-1c").split(".")[0]) > 2500:
                self.text.delete("1.0", "501.0")
            self.text.see("end")
            self.text.configure(state="disabled")

        def set_buttons(self):
            for button in self.buttons:
                button.configure(state="normal" if self.connected and not self.busy else "disabled")
            alive = self.worker is not None and self.worker.is_alive()
            self.connect_btn.configure(state="disabled" if alive else "normal")
            self.disconnect_btn.configure(state="normal" if alive else "disabled")

        def connect(self):
            if not self.declared.get():
                self.show("先确认当前板上已加载本次 GOLD 配对，且串口由此面板独占，再勾选声明。", True)
                return
            if self.worker and self.worker.is_alive():
                return
            self.stop = threading.Event()
            self.requests = queue.Queue(maxsize=1)
            self.identity.set("正在核验本地 SHA；尚未观察到板端 BUILD。")
            self.worker = Worker(self.root.get(), self.port.get().strip(), Path(__file__).resolve().parent / "board_logs",
                                 self.requests, self.events, self.stop)
            self.worker.start()
            self.show("正在核验文件并连接串口；不会自动发送命令。")
            self.set_buttons()

        def disconnect(self):
            self.stop.set()
            self.connected = False
            self.set_buttons()
            self.show("正在关闭串口；游戏不会自动退出。")

        def submit(self, action):
            if not self.connected or self.busy:
                return
            self.busy = True
            self.set_buttons()
            self.show("处理中；等待当前回包，不自动重试。")
            self.requests.put_nowait((action, int(self.flipx.get()) | (int(self.flipy.get()) << 1)))

        def decoded(self, r):
            if r["kind"] == "joy":
                x, y = (r["x"], r["y"]) if r["valid"] else ("NA", "NA")
                self.joy.set(f"X {x}   Y {y}    ARM {r['armed']}    ERROR {r['error']}    LATCH {r['latched']}    FAULTS {r['faults']}    SEQ {r['sequence']}  （最近回包）")
            elif r["kind"] == "enemy":
                f = r["fields"]
                self.game.set(f"游戏 {f.get('GAME', '—')} / 运动 {f.get('MOVING', '—')}    生命 {f.get('LIVES', '—')}    分数 {f.get('SCORE', '—')}    普通弹可见 {f.get('MEMBERS_VISIBLE', '—')}    状态 {f.get('STATE', '—')}")
            elif r["kind"] == "diag":
                counts = {k: v for k, v in r["counts"].items() if v}
                self.append("解析：累计故障 " + json.dumps(counts) + f"；首槽 {r['first']}（可能为历史）")
            elif r["kind"] == "effects":
                f = r["fields"]
                self.fx.set(f"护盾 ACTIVE={f.get('ACTIVE', '—')} MODE={f.get('MODE', '—')} ABSORBED={f.get('ABSORBED', '—')}；转场详情见下方 ENEMY FLOW 原文。")

        def pump(self):
            while True:
                try:
                    kind, value = self.events.get_nowait()
                except queue.Empty:
                    break
                if kind in ("tx", "rx"):
                    self.append(("→ " if kind == "tx" else "← ") + value)
                elif kind == "decoded":
                    self.decoded(value)
                elif kind == "connected":
                    self.connected = True
                    self.identity.set("本地 SHA 匹配 + 操作者声明；板端 BUILD 尚未读到。")
                    self.show(value)
                elif kind == "disconnected":
                    self.connected = self.busy = False
                    self.append(value)
                elif kind == "busy":
                    self.busy = value
                elif kind == "build":
                    self.identity.set("实际收到 BUILD: " + value)
                elif kind == "log":
                    self.logpath.set("日志：" + value)
                elif kind == "error":
                    self.show(value, True)
                    self.append("错误：" + value)
                elif kind == "done":
                    self.show(value)
            self.set_buttons()
            if self.closing and (self.worker is None or not self.worker.is_alive()):
                self.win.destroy()
                return
            self.win.after(50, self.pump)

        def close(self):
            self.closing = True
            self.disconnect()

    window = tk.Tk()
    Panel(window)
    window.mainloop()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=DEFAULT_ROOT, help="frozen GOLD sol directory")
    ap.add_argument("--port", default="COM15")
    ap.add_argument("--check-only", "--verify-only", dest="check_only", action="store_true")
    args = ap.parse_args()
    if args.check_only:
        print(json.dumps(verify_pair(args.root), ensure_ascii=False, indent=2))
        print("PASS local files only; no serial imported/opened")
    else:
        launch_gui(args)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        raise SystemExit(f"ERROR: {type(exc).__name__}: {exc}")
