#!/usr/bin/env python3
"""Ti60 basic-demo UART panel. No automatic connection or firmware loading.

Requires PySide6 >= 6.10. Serial I/O runs in Qt's event loop without blocking.
Only RX STATUS updates the displayed board state; transmitting is not an ACK.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path

from PySide6.QtCore import QObject, QIODevice, QTimer, Qt, QUrl, Signal
from PySide6.QtGui import QDesktopServices, QFont
from PySide6.QtSerialPort import QSerialPort, QSerialPortInfo
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QFrame, QGridLayout, QGroupBox,
    QHBoxLayout, QLabel, QLayout, QLineEdit, QMainWindow, QPlainTextEdit, QPushButton,
    QScrollArea, QSizePolicy, QSpinBox, QVBoxLayout, QWidget,
)

VERSION = "1.2"
QUAD_PRESETS = (1, 2, 4, 8, 16, 20)
PRESETS = QUAD_PRESETS + (32, 64)
MODE_COMMANDS = (
    ("四分屏", "MODE QUAD", "CPU / BLIT / RECT / HUD"),
    ("全屏 · BLIT", "FULL BLIT", "硬件搬运"),
    ("全屏 · RECT", "FULL RECT", "硬件色块填充"),
    ("全屏 · CPU", "FULL CPU", "CPU 搬运对照"),
)


def stamp() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="milliseconds")


def validate_command(value: str) -> str:
    value = value.strip()
    if not value or len(value) > 160 or any(not 32 <= ord(c) < 127 for c in value):
        raise ValueError("请输入一条 1～160 字符的英文串口命令，不要包含换行。")
    return value


def rejection(line: str) -> bool:
    """Reject literal protocol errors; ERROR=00000000 is not an error."""
    if re.search(r"(?:^|\s)(?:BUSY|UNSUPPORTED(?:_HW)?|ERROR|ERR|FAIL)(?=$|[\s:])", line):
        return True
    for name in ("ERROR", "ASYNC_ERROR", "ERROR_INFO", "BG_ERROR"):
        found = re.search(r"\b" + name + r"=([0-9a-fA-F]+)\b", line)
        if found and int(found.group(1), 16):
            return True
    return bool(re.search(r"\bFAILED=[1-9][0-9]*\b", line))


def basic_status(line: str) -> dict[str, str] | None:
    # Game/asset status must not overwrite the basic-demo state card.
    if not line.startswith("STATUS "):
        return None
    result = dict(re.findall(r"\b([A-Z][A-Z0-9_]*)=([^\s]+)", line))
    if "MODE" not in result and re.search(r"\b(?:SERIAL|ASYNC) QUAD\b", line):
        result["MODE"] = "QUAD"
    return result


class SerialLink(QObject):
    state_changed = Signal(bool)
    busy_changed = Signal(bool)
    line_received = Signal(str)
    event_received = Signal(str, str)
    notice = Signal(str, bool)

    def __init__(self, log_root: Path, parent=None):
        super().__init__(parent)
        self.port = QSerialPort(self)
        self.port.readyRead.connect(self._read)
        self.port.errorOccurred.connect(self._error)
        self.timer = QTimer(self)
        self.timer.setTimerType(Qt.TimerType.PreciseTimer)
        self.timer.setInterval(2)
        self.timer.timeout.connect(self._write_byte)
        self.cooldown = QTimer(self)
        self.cooldown.setSingleShot(True)
        self.cooldown.timeout.connect(self._release)
        self.log_root = log_root
        self.session_dir: Path | None = None
        self.text_log = self.events = self.raw = None
        self.buffer = bytearray()
        self.tx = b""
        self.tx_pos = 0
        self.command = ""
        self.busy = False
        self.opening = False
        self.closing = False
        self.rejected = False

    def record(self, kind: str, data: str):
        when = stamp()
        if self.events:
            self.events.write(json.dumps({"time": when, "kind": kind, "data": data}, ensure_ascii=False) + "\n")
            self.events.flush()
        if self.text_log:
            self.text_log.write(f"[{when}] {kind}  {data}\n")
            self.text_log.flush()
        self.event_received.emit(kind, data)

    def open(self, name: str, baud: int) -> bool:
        if self.port.isOpen():
            return True
        try:
            self.log_root.mkdir(parents=True, exist_ok=True)
            self.session_dir = self.log_root / dt.datetime.now().strftime("session_%Y%m%d_%H%M%S_%f")
            self.session_dir.mkdir()
            self.text_log = (self.session_dir / "console.log").open("x", encoding="utf-8")
            self.events = (self.session_dir / "events.jsonl").open("x", encoding="utf-8")
            self.raw = (self.session_dir / "rx.raw.bin").open("xb")
            self.record("OPEN", f"port={name} baud={baud} panel={VERSION}")
        except OSError as exc:
            self._close_logs()
            self.notice.emit(f"无法创建日志：{exc}", True)
            return False
        self.buffer.clear()
        self.port.setPortName(name)
        self.port.setBaudRate(baud)
        self.port.setDataBits(QSerialPort.DataBits.Data8)
        self.port.setParity(QSerialPort.Parity.NoParity)
        self.port.setStopBits(QSerialPort.StopBits.OneStop)
        self.port.setFlowControl(QSerialPort.FlowControl.NoFlowControl)
        self.port.setReadBufferSize(256 * 1024)
        self.opening = True
        ok = self.port.open(QIODevice.OpenModeFlag.ReadWrite)
        self.opening = False
        if not ok:
            message = f"无法打开 {name}：{self.port.errorString()}。请检查端口并关闭其他串口程序。"
            self.record("OPEN_ERROR", message)
            self._close_logs()
            self.notice.emit(message, True)
            return False
        self.state_changed.emit(True)
        self.notice.emit(f"已连接 {name} · {baud}。点击 STATUS 读取当前状态。", False)
        return True

    def close(self):
        if self.closing:
            return
        self.closing = True
        self.timer.stop()
        self.cooldown.stop()
        if self.port.isOpen():
            self._read()
            if self.buffer:
                self.record("RX_PARTIAL", self.buffer.decode("ascii", errors="replace"))
                self.buffer.clear()
            if self.busy and self.tx_pos < len(self.tx):
                self.record("TX_INTERRUPTED", f"{self.command}; queued_bytes={self.tx_pos}/{len(self.tx)}")
            self.port.close()
            self.record("CLOSE", "serial port closed; no board command was sent")
        self._close_logs()
        self.tx = b""
        self.busy = False
        self.busy_changed.emit(False)
        self.state_changed.emit(False)
        self.closing = False

    def _close_logs(self):
        for name in ("text_log", "events", "raw"):
            handle = getattr(self, name)
            if handle:
                handle.close()
                setattr(self, name, None)

    def send(self, command: str):
        try:
            command = validate_command(command)
        except ValueError as exc:
            self.notice.emit(str(exc), True)
            return
        if not self.port.isOpen():
            self.notice.emit("请先连接串口。", True)
            return
        if self.busy:
            self.notice.emit("上一条命令仍在发送，请稍候。", True)
            return
        self.command = command
        self.tx = (command + "\n").encode("ascii")
        self.tx_pos = 0
        self.rejected = False
        self.busy = True
        self.busy_changed.emit(True)
        self.record("TX", command)
        self.notice.emit(f"正在发送：{command}", False)
        self.timer.start()

    def _write_byte(self):
        if not self.port.isOpen():
            self.close()
            return
        if self.tx_pos < len(self.tx):
            sent = self.port.write(self.tx[self.tx_pos:self.tx_pos + 1])
            if sent != 1:
                self.notice.emit(f"发送失败：{self.port.errorString()}", True)
                self.close()
                return
            self.tx_pos += 1
        elif self.port.bytesToWrite() == 0:
            self.timer.stop()
            self.record("TX_DRAINED", self.command)
            # Transmission is NOT an application-level acknowledgement.
            if not self.rejected:
                self.notice.emit(f"已发送 {self.command}；设置是否生效请看 RX / STATUS。", False)
            self.cooldown.start(350)

    def _release(self):
        self.busy = False
        self.tx = b""
        self.busy_changed.emit(False)

    def _read(self):
        data = bytes(self.port.readAll())
        if not data:
            return
        if self.raw:
            self.raw.write(data)
            self.raw.flush()
        self.buffer.extend(data)
        while b"\n" in self.buffer:
            raw_line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            line = raw_line.rstrip(b"\r").decode("ascii", errors="replace")
            self.record("RX", line)
            self.line_received.emit(line)
            if rejection(line):
                self.rejected = True
                self.notice.emit(f"板子回复：{line}", True)
        if len(self.buffer) > 65536:
            self.record("RX_PARTIAL", self.buffer.decode("ascii", errors="replace"))
            self.buffer.clear()
            self.notice.emit("收到超长未结束行；原始字节已保存，请查看日志。", True)

    def _error(self, code):
        if self.opening or self.closing or code == QSerialPort.SerialPortError.NoError:
            return
        message = self.port.errorString()
        self.record("SERIAL_ERROR", message)
        self.notice.emit(f"串口错误：{message}", True)
        if code in (QSerialPort.SerialPortError.ResourceError, QSerialPort.SerialPortError.ReadError,
                    QSerialPort.SerialPortError.WriteError, QSerialPort.SerialPortError.DeviceNotFoundError):
            self.close()


STYLE = """
QWidget { color:#e7edf6; font-size:14px; }
QMainWindow, QScrollArea, QWidget#root { background:#101827; }
QScrollArea { border:0; }
QLabel#title { font-size:26px; font-weight:700; }
QLabel#subtitle, QLabel#hint { color:#91a4bf; }
QLabel#badge { background:#203048; border-radius:12px; padding:6px 12px; color:#a7bdd8; }
QGroupBox { background:#172337; border:1px solid #293b54; border-radius:12px;
 margin-top:10px; padding:10px 8px 8px; font-weight:600; }
QGroupBox::title { subcontrol-origin:margin; left:16px; padding:0 5px; color:#b3c7e4; }
QPushButton { background:#24354d; border:1px solid #354a66; border-radius:8px;
 padding:10px 14px; font-weight:600; min-height:18px; }
QPushButton:hover { background:#2c4563; border-color:#52b8d5; }
QPushButton:pressed { background:#173a50; }
QPushButton:disabled { color:#65758b; background:#1b283c; border-color:#28384e; }
QPushButton#connect { background:#0e7189; border-color:#279bb3; }
QPushButton#mode { text-align:left; padding:12px; min-height:44px; }
QPushButton#apply { background:#14566b; border-color:#278ca5; }
QComboBox, QSpinBox, QLineEdit { background:#0f1a2b; border:1px solid #344960;
 border-radius:7px; padding:8px; selection-background-color:#167e98; }
QComboBox QAbstractItemView { background:#152238; selection-background-color:#205575; }
QSpinBox::up-button, QSpinBox::down-button { background:#8faac5; width:18px; border:1px solid #344960; }
QPlainTextEdit { background:#0b1422; border:1px solid #293b54; border-radius:8px;
 padding:9px; color:#b7d0e3; font-family:Consolas,monospace; font-size:12px; }
QLabel#notice { padding:10px; border-radius:7px; background:#172f41; color:#a1d5e8; }
QLabel#notice[error="true"] { background:#4a242c; color:#ffd3d7; }
QLabel#status { background:#111e30; border-radius:7px; padding:10px; color:#bccee5; }
QCheckBox { spacing:7px; color:#b6c8dd; }
QToolTip { background:#1a2b43; color:#e7edf6; border:1px solid #48627e; }
"""


class Panel(QMainWindow):
    def __init__(self, port_name="COM15", log_root: Path | None = None):
        super().__init__()
        self.setWindowTitle("Ti60 演示控制台 · 基础 Demo")
        screen = QApplication.primaryScreen()
        area = screen.availableGeometry() if screen else None
        self.resize(min(1080, area.width() - 60) if area else 1080,
                    min(950, area.height() - 60) if area else 950)
        self.setMinimumSize(800, 650)
        self.link = SerialLink(log_root or Path(__file__).resolve().parent / "logs", self)
        self.command_buttons: list[QPushButton] = []
        self.cached_status: dict[str, str] = {}
        self.mode_request: tuple[str, str] | None = None
        self.preset_buttons: dict[int, QPushButton] = {}
        root = QWidget(objectName="root")
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(root)
        self.setCentralWidget(scroll)
        layout = QVBoxLayout(root)
        layout.setSizeConstraint(QLayout.SizeConstraint.SetMinimumSize)
        layout.setContentsMargins(24, 16, 24, 16)
        layout.setSpacing(8)

        header = QHBoxLayout()
        heading = QVBoxLayout()
        heading.addWidget(QLabel("Ti60 演示控制台", objectName="title"))
        heading.addWidget(QLabel("彩色方块 Demo · 模式切换 / 突发传输 / 双缓冲", objectName="subtitle"))
        header.addLayout(heading)
        header.addStretch()
        self.badge = QLabel("未连接", objectName="badge")
        self.badge.setMaximumHeight(40)
        self.badge.setAlignment(Qt.AlignmentFlag.AlignCenter)
        header.addWidget(self.badge)
        layout.addLayout(header)

        connection = QHBoxLayout()
        connection.addWidget(QLabel("串口"))
        self.port_choice = QComboBox()
        self.port_choice.setEditable(True)
        self.port_choice.setMinimumWidth(150)
        self.port_choice.setCurrentText(port_name)
        self.refresh_ports()
        connection.addWidget(self.port_choice, 1)
        self.baud = QComboBox()
        self.baud.addItems(["115200", "57600", "230400"])
        connection.addWidget(self.baud)
        self.refresh = QPushButton("刷新端口")
        self.refresh.clicked.connect(self.refresh_ports)
        connection.addWidget(self.refresh)
        self.connect_button = QPushButton("连接", objectName="connect")
        self.connect_button.clicked.connect(self.toggle_connection)
        connection.addWidget(self.connect_button)
        layout.addLayout(connection)

        self.state_card = QLabel("板端状态：尚未查询", objectName="status")
        self.state_card.setWordWrap(True)
        layout.addWidget(self.state_card)

        modes = QGroupBox("01  显示模式")
        mode_row = QHBoxLayout(modes)
        for name, command, detail in MODE_COMMANDS:
            button = self.command_button(f"{name}\n{detail}", command, "mode")
            mode_row.addWidget(button, 1)
        layout.addWidget(modes)

        middle = QHBoxLayout()
        options = QGroupBox("02  演示开关")
        option_grid = QGridLayout(options)
        option_grid.addWidget(QLabel("BURST&FIFO"), 0, 0)
        option_grid.addWidget(self.command_button("开启", "BURST ON"), 0, 1)
        option_grid.addWidget(self.command_button("关闭", "BURST OFF"), 0, 2)
        option_grid.addWidget(QLabel("双缓冲 DBUF"), 1, 0)
        option_grid.addWidget(self.command_button("开启", "DBUF ON"), 1, 1)
        option_grid.addWidget(self.command_button("关闭", "DBUF OFF"), 1, 2)
        option_hint = QLabel("FIFO 固定开启；新版 Demo 默认开启异步和碰撞。", objectName="hint")
        option_hint.setWordWrap(True)
        option_grid.addWidget(option_hint, 2, 0, 1, 3)
        middle.addWidget(options, 1)

        counts = QGroupBox("03  方块数量")
        count_layout = QVBoxLayout(counts)
        preset_row = QGridLayout()
        self.count = QSpinBox()
        self.count.setRange(1, 20)
        self.count.setValue(4)
        for index, value in enumerate(PRESETS):
            button = QPushButton(str(value))
            button.setToolTip(f"发送 NUM {value}")
            button.clicked.connect(lambda checked=False, n=value: self.set_count(n))
            self.command_buttons.append(button)
            self.preset_buttons[value] = button
            preset_row.addWidget(button, index // 4, index % 4)
        count_layout.addLayout(preset_row)
        custom_row = QHBoxLayout()
        custom_row.addWidget(QLabel("自定义"))
        custom_row.addWidget(self.count)
        self.apply_count = QPushButton("应用数量", objectName="apply")
        self.apply_count.clicked.connect(lambda: self.set_count(self.count.value()))
        self.command_buttons.append(self.apply_count)
        custom_row.addWidget(self.apply_count)
        count_layout.addLayout(custom_row)
        self.count_hint = QLabel("分屏每区最多 20；切换全屏并查询 STATUS 后可选 32 / 64。", objectName="hint")
        self.count_hint.setWordWrap(True)
        count_layout.addWidget(self.count_hint)
        middle.addWidget(counts, 1)
        layout.addLayout(middle)

        tools_row = QHBoxLayout()
        tools_row.addWidget(self.command_button("查询 STATUS", "STATUS"))
        self.auto_status = QCheckBox("每 2 秒查询 STATUS")
        self.auto_status.setToolTip("默认关闭；正式性能采样请使用原采样工具。")
        tools_row.addWidget(self.auto_status)
        tools_row.addStretch()
        layout.addLayout(tools_row)

        self.message = QLabel("连接后查询 STATUS。切换开关后，请再次查询确认板端状态。", objectName="notice")
        self.message.setWordWrap(True)
        layout.addWidget(self.message)

        log_head = QHBoxLayout()
        log_head.addWidget(QLabel("串口收发记录"))
        log_head.addStretch()
        self.open_logs = QPushButton("打开日志目录")
        self.open_logs.clicked.connect(self.show_log_folder)
        log_head.addWidget(self.open_logs)
        clear = QPushButton("清空屏幕")
        clear.setToolTip("仅清空窗口显示，磁盘日志保留。")
        log_head.addWidget(clear)
        layout.addLayout(log_head)
        self.console = QPlainTextEdit()
        self.console.setReadOnly(True)
        self.console.setMaximumBlockCount(6000)
        self.console.setMinimumHeight(120)
        clear.clicked.connect(self.console.clear)
        layout.addWidget(self.console, 1)

        manual_row = QHBoxLayout()
        self.manual = QLineEdit()
        self.manual.setPlaceholderText("手动命令，例如 STATUS 或 NUM 20；回车发送")
        self.manual.returnPressed.connect(self.manual_send)
        manual_row.addWidget(self.manual, 1)
        self.send_button = QPushButton("发送")
        self.send_button.clicked.connect(self.manual_send)
        self.command_buttons.append(self.send_button)
        manual_row.addWidget(self.send_button)
        layout.addLayout(manual_row)
        layout.addWidget(QLabel("演示流程：连接 → 查询状态 → 全屏 BLIT → 切换突发 / 双缓冲 → 查询状态", objectName="hint"))

        self.link.notice.connect(self.show_notice)
        self.link.event_received.connect(self.append_event)
        self.link.line_received.connect(self.on_line)
        self.link.state_changed.connect(self.connection_changed)
        self.link.busy_changed.connect(lambda _: self.update_enabled())
        self.poll = QTimer(self)
        self.poll.setInterval(2000)
        self.poll.timeout.connect(self.poll_status)
        self.poll.start()
        self.update_enabled()

    def command_button(self, label: str, command: str, name="") -> QPushButton:
        button = QPushButton(label)
        if name:
            button.setObjectName(name)
        button.setToolTip(command)
        button.clicked.connect(lambda checked=False, c=command: self.send_command(c))
        self.command_buttons.append(button)
        return button

    def send_command(self, command: str):
        if command.startswith("FULL "):
            self.mode_request = ("FULL", command.split()[1])
        elif command == "MODE QUAD":
            self.mode_request = ("QUAD", "")
        self.update_enabled()
        self.link.send(command)

    def full_count_ready(self) -> bool:
        state = self.cached_status
        return (self.mode_request is None and state.get("MODE") == "FULL"
                and state.get("TRANSITION") == "0" and state.get("PENDING") == "0")

    def refresh_ports(self):
        value = self.port_choice.currentText().strip() or "COM15"
        self.port_choice.clear()
        for info in QSerialPortInfo.availablePorts():
            self.port_choice.addItem(info.portName())
            self.port_choice.setItemData(self.port_choice.count() - 1, info.description(), Qt.ItemDataRole.ToolTipRole)
        self.port_choice.setCurrentText(value)

    def toggle_connection(self):
        if self.link.port.isOpen():
            self.link.close()
            self.show_notice("已断开，串口已释放。", False)
        else:
            name = self.port_choice.currentText().strip()
            if name:
                self.link.open(name, int(self.baud.currentText()))

    def connection_changed(self, connected: bool):
        self.connect_button.setText("断开" if connected else "连接")
        self.badge.setText(f"已连接 · {self.link.port.portName()}" if connected else "未连接")
        self.cached_status.clear()
        self.mode_request = None
        self.state_card.setText("板端状态：尚未查询" if connected else "板端状态：未连接")
        if not connected:
            self.auto_status.setChecked(False)
        self.update_enabled()

    def update_enabled(self):
        connected = self.link.port.isOpen()
        for button in self.command_buttons:
            button.setEnabled(connected and not self.link.busy)
        self.manual.setEnabled(connected and not self.link.busy)
        self.port_choice.setEnabled(not connected)
        self.baud.setEnabled(not connected)
        self.refresh.setEnabled(not connected)
        self.auto_status.setEnabled(connected)
        full = self.full_count_ready()
        self.count.setMaximum(64 if full else 20)
        for value in (32, 64):
            self.preset_buttons[value].setEnabled(connected and not self.link.busy and full)

    def set_count(self, value: int):
        if value not in PRESETS:
            self.show_notice("数量支持 1 / 2 / 4 / 8 / 16 / 20；全屏另有 32 / 64。", True)
            return
        if value > 20 and not self.full_count_ready():
            self.show_notice("32 / 64 仅限全屏；请切换全屏并查询 STATUS，确认切换完成。", True)
            return
        self.count.setValue(value)
        self.link.send(f"NUM {value}")

    def manual_send(self):
        self.send_command(self.manual.text().strip())

    def poll_status(self):
        if self.auto_status.isChecked() and self.link.port.isOpen() and not self.link.busy and not self.manual.hasFocus():
            self.link.send("STATUS")

    def append_event(self, kind: str, data: str):
        now = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.console.appendPlainText(f"[{now}] {kind:11s} {data}")

    def show_notice(self, text: str, error: bool):
        self.message.setText(text)
        self.message.setProperty("error", error)
        self.message.style().unpolish(self.message)
        self.message.style().polish(self.message)

    def on_line(self, line: str):
        state = basic_status(line)
        if state is not None:
            # Replace, rather than merge: different modes report different fields.
            self.cached_status = state
            if self.mode_request and state.get("TRANSITION") == "0" and state.get("PENDING") == "0":
                wanted_mode, wanted_source = self.mode_request
                if state.get("MODE") == wanted_mode and (not wanted_source or state.get("SOURCE") == wanted_source):
                    self.mode_request = None
            self.update_enabled()
            mode = state.get("MODE", "未返回")
            source = state.get("SOURCE", "")
            mode = f"{mode} / {source}" if source else mode
            if "OBJECTS" in state:
                objects = state["OBJECTS"]
            else:
                objects = "CPU {} / BLIT {} / RECT {}".format(*(state.get(k, "—") for k in ("CPU_OBJECTS", "GPU_OBJECTS", "RECT_OBJECTS")))
            burst = state.get("BURST", "未返回")
            dbuf = state.get("DBUF", "未返回")
            fifo = state.get("FIFO", "未返回")
            combined = "ON" if burst == fifo == "ON" else "OFF" if "OFF" in (burst, fifo) else "未返回"
            pending = state.get("OPTIONS_PENDING")
            option_state = {"0": "已应用", "1": "待生效"}.get(pending, "未返回")
            failed = state.get("FAILED", state.get("ERROR", "未返回"))
            self.state_card.setText(
                f"模式  {mode}    |    方块  {objects}\n"
                f"BURST&FIFO  {combined}    |    双缓冲  {dbuf}\n"
                f"开关请求  {option_state}    |    FAILED/ERROR  {failed}    ·    {dt.datetime.now():%H:%M:%S}"
            )
        if line.startswith("OK OPTION PENDING"):
            self.show_notice("板端正在应用开关请求；请查询 STATUS，等待 OPTIONS_PENDING=0。", False)

    def show_log_folder(self):
        path = self.link.session_dir or self.link.log_root
        path.mkdir(parents=True, exist_ok=True)
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(path.resolve())))

    def closeEvent(self, event):
        self.link.close()
        event.accept()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM15")
    parser.add_argument("--log-dir", type=Path)
    args = parser.parse_args()
    app = QApplication([sys.argv[0]])
    app.setApplicationName("Ti60 Demo Panel")
    app.setStyle("Fusion")
    app.setFont(QFont("Microsoft YaHei UI" if sys.platform == "win32" else "Noto Sans CJK SC", 10))
    app.setStyleSheet(STYLE)
    panel = Panel(args.port, args.log_dir)
    panel.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
