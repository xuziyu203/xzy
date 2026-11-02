"""Native Qt presentation layer for the 2D rendering console.

This module has no serial access and starts no threads or timers.  Keyboard
ownership, commands and telemetry are supplied by the application controller.
"""
from __future__ import annotations

from PySide6.QtCore import QPointF, QRectF, QSize, Qt
from PySide6.QtGui import QColor, QFont, QFontDatabase, QLinearGradient, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QFrame, QGridLayout, QHBoxLayout, QLabel,
    QLineEdit, QMainWindow, QPlainTextEdit, QPushButton, QScrollArea,
    QSizePolicy, QVBoxLayout, QWidget,
)


def ui_font() -> str:
    available = set(QFontDatabase.families())
    return next((x for x in ("Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans CJK SC", "WenQuanYi Zen Hei")
                 if x in available), "Sans Serif")


STYLE = """
QMainWindow, QScrollArea, QWidget#scrollContent { background: #080f1d; color: #e4edf9; }
QScrollArea { border: none; }
QWidget { color: #d8e4f3; font-size: 12px; }
QFrame#card { background: #0e192a; border: 1px solid #243951; border-radius: 10px; }
QFrame#metric { background: #102135; border: 1px solid #29435c; border-radius: 8px; }
QLabel { background: transparent; border: none; }
QLabel#sectionTitle { color: #dcebf9; font-size: 14px; font-weight: 700; }
QLabel#sectionTag { color: #4fcfe0; font-size: 10px; letter-spacing: 2px; }
QLabel#muted { color: #819bb5; }
QLabel#small { color: #7b94ad; font-size: 11px; }
QLabel#metricValue { color: #e4faff; font-size: 29px; font-weight: 600; }
QLabel#metricName { color: #8ba7bd; font-size: 11px; }
QLabel#linkBadge { color: #85a0ba; background: #142237; border: 1px solid #2c425c; border-radius: 10px; padding: 4px 12px; }
QLabel#notice { color: #b6d5ed; background: #10243a; border: 1px solid #27465f; border-radius: 6px; padding: 8px 10px; }
QLabel#identity { color: #708ca7; font-size: 10px; }
QLineEdit, QComboBox { color: #c4d7e9; background: #091422; border: 1px solid #2a415a; border-radius: 5px; padding: 7px 9px; selection-background-color: #196885; }
QLineEdit:focus, QComboBox:focus { border-color: #4ac5dc; }
QComboBox::drop-down { border: none; width: 24px; }
QPushButton { color: #bdd1e5; background: #172a40; border: 1px solid #304b65; border-radius: 5px; padding: 7px 11px; min-height: 16px; }
QPushButton:hover { color: #effcff; background: #1a3a50; border-color: #55bfd5; }
QPushButton:pressed { background: #23556c; }
QPushButton:disabled { color: #52677b; background: #111e2e; border-color: #223247; }
QPushButton#primary { color: #061a26; background: #5bd6e5; border-color: #79eafa; font-weight: 700; }
QPushButton#primary:hover { background: #8feaf2; }
QPushButton#primary:disabled { color: #5a7c88; background: #193848; border-color: #274959; }
QPushButton#purple { color: #dfd3ff; background: #292443; border-color: #51467a; }
QPushButton#purple:hover { background: #3a315c; border-color: #ad95f0; }
QPushButton#danger { color: #e2a9bb; background: #2d2032; border-color: #5b3a4e; }
QPushButton#keycap { min-height: 72px; max-height: 72px; padding: 0; border: none; background: transparent; }
QCheckBox { color: #9fb6ce; spacing: 8px; background: transparent; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #405a73; background: #0b1727; border-radius: 3px; }
QCheckBox::indicator:checked { background: #58d7e6; border: 2px solid #baf3ff; }
QPlainTextEdit { color: #8caac2; background: #08121f; border: 1px solid #1f344b; border-radius: 6px; padding: 7px; font-family: Consolas, 'DejaVu Sans Mono', monospace; font-size: 11px; selection-background-color: #24536b; }
QScrollBar:vertical { width: 7px; background: #0a1524; margin: 0; }
QScrollBar::handle:vertical { background: #304b66; min-height: 30px; border-radius: 3px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { height: 7px; background: #0a1524; }
QScrollBar::handle:horizontal { background: #304b66; min-width: 30px; }
QStatusBar { background: #080f1d; color: #68849f; border-top: 1px solid #1a2c41; font-size: 11px; }
"""


class KeyCap(QPushButton):
    """Drawn, mouse-operable key; active is the local keyboard state only."""

    def __init__(self, key: str, caption: str, accent: str = "#5ddce8", parent=None):
        super().__init__(parent)
        self.setObjectName("keycap")
        self.key, self.caption, self.accent = key, caption, QColor(accent)
        self._active = False
        self.setAccessibleName(f"{key} {caption}")
        self.setToolTip(f"{key} · {caption}")
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setAutoRepeat(False)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setMouseTracking(True)
        self.setMinimumSize(76, 72)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.setFixedHeight(72)

    def sizeHint(self):
        return QSize(88, 72)

    def minimumSizeHint(self):
        return QSize(76, 72)

    def set_active(self, active: bool):
        active = bool(active)
        if self._active != active:
            self._active = active
            self.update()

    def is_active(self) -> bool:
        return self._active

    def enterEvent(self, event):
        super().enterEvent(event)
        self.update()

    def leaveEvent(self, event):
        super().leaveEvent(event)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        active = self._active or self.isDown()
        hover = self.underMouse()
        r = QRectF(2, 2, self.width() - 4, self.height() - 7)
        cut = 11.0
        shape = QPolygonF([QPointF(r.left(), r.top() + cut), QPointF(r.left() + cut, r.top()),
                          QPointF(r.right(), r.top()), QPointF(r.right(), r.bottom() - cut),
                          QPointF(r.right() - cut, r.bottom()), QPointF(r.left(), r.bottom())])
        shadow = QPolygonF([p + QPointF(0, 4) for p in shape])
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor("#050b13"))
        painter.drawPolygon(shadow)
        gradient = QLinearGradient(r.topLeft(), r.bottomRight())
        if active:
            gradient.setColorAt(0, self.accent.lighter(125))
            gradient.setColorAt(1, self.accent.darker(125))
            stroke = self.accent.lighter(140)
        else:
            gradient.setColorAt(0, QColor("#1c344b") if hover else QColor("#142b41"))
            gradient.setColorAt(1, QColor("#102033"))
            stroke = self.accent if hover else QColor("#3a5b75")
        painter.setBrush(gradient)
        painter.setPen(QPen(stroke, 1.3 if active or hover else 1.0))
        painter.drawPolygon(shape)
        painter.setPen(QPen(self.accent if not active else QColor("#ffffff"), 2))
        painter.drawLine(QPointF(r.left() + 14, r.top() + 8), QPointF(r.left() + 37, r.top() + 8))
        painter.setPen(QColor("#071b29") if active else QColor("#e1f5ff"))
        font = QFont("Consolas", 19 if len(self.key) == 1 else 15)
        font.setBold(True)
        painter.setFont(font)
        painter.drawText(QRectF(0, 12, self.width(), 30), Qt.AlignmentFlag.AlignCenter, self.key)
        font = QFont(ui_font(), 9)
        painter.setFont(font)
        painter.setPen(QColor("#16374a") if active else QColor("#85a7bd"))
        painter.drawText(QRectF(0, 44, self.width(), 19), Qt.AlignmentFlag.AlignCenter, self.caption)
        if active:
            painter.setBrush(QColor("#eaffff"))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawEllipse(QPointF(r.right() - 10, r.top() + 10), 2.3, 2.3)


class ConsoleBackground(QWidget):
    def paintEvent(self, event):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#080f1d"))
        p.setPen(QPen(QColor(49, 83, 112, 22), 1))
        for x in range(0, self.width(), 32):
            p.drawLine(x, 0, x, self.height())
        for y in range(0, self.height(), 32):
            p.drawLine(0, y, self.width(), y)


def label(text: str, name: str = "", wrap=False) -> QLabel:
    w = QLabel(text)
    if name:
        w.setObjectName(name)
    w.setWordWrap(wrap)
    w.setMinimumWidth(0)
    return w


def card(title: str, tag: str = ""):
    frame = QFrame()
    frame.setObjectName("card")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(14, 10, 14, 11)
    layout.setSpacing(8)
    head = QHBoxLayout()
    head.addWidget(label(title, "sectionTitle"))
    head.addStretch()
    if tag:
        head.addWidget(label(tag, "sectionTag"))
    layout.addLayout(head)
    return frame, layout


class PanelView(QMainWindow):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("2D 图形渲染与弹幕交互平台")
        self.resize(1360, 900)
        self.setMinimumSize(1080, 720)
        self.setFont(QFont(ui_font(), 10))
        self.setStyleSheet(STYLE)
        self.action_buttons = {}
        self.metric_labels = {}
        self.keys = {}

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        content = ConsoleBackground()
        content.setObjectName("scrollContent")
        scroll.setWidget(content)
        self.setCentralWidget(scroll)
        outer = QVBoxLayout(content)
        outer.setContentsMargins(25, 15, 25, 12)
        outer.setSpacing(12)

        header = QVBoxLayout()
        header.setSpacing(5)
        eyebrow = label("FPGA  /  GRAPHICS SYSTEM", "sectionTag")
        eyebrow.setAlignment(Qt.AlignmentFlag.AlignCenter)
        header.addWidget(eyebrow)
        self.title_label = label("2D 图形渲染与弹幕交互平台")
        self.title_label.setStyleSheet("font-size: 27px; font-weight: 700; color: #ecf8ff; letter-spacing: 2px;")
        self.title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        header.addWidget(self.title_label)
        tagline = label("RENDER  ·  CONTROL  ·  OBSERVE", "small")
        tagline.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tagline.setStyleSheet("color: #6c92b0; letter-spacing: 3px; font-size: 10px;")
        header.addWidget(tagline)
        outer.addLayout(header)

        body = QHBoxLayout()
        body.setSpacing(16)
        left = QVBoxLayout()
        left.setSpacing(12)
        right = QVBoxLayout()
        right.setSpacing(12)
        body.addLayout(left, 1)
        right_host = QWidget()
        right_host.setLayout(right)
        right_host.setFixedWidth(350)
        right.setContentsMargins(0, 0, 0, 0)
        body.addWidget(right_host)
        outer.addLayout(body, 1)

        connection, cl = card("设备连接", "01 / LINK")
        pathrow = QHBoxLayout()
        pathrow.addWidget(label("配对目录", "muted"))
        self.root_edit = QLineEdit()
        self.root_edit.setPlaceholderText("选择与当前板上固件对应的配对目录")
        pathrow.addWidget(self.root_edit, 1)
        cl.addLayout(pathrow)
        row = QHBoxLayout()
        row.addWidget(label("串口", "muted"))
        self.port_combo = QComboBox()
        self.port_combo.setEditable(True)
        self.port_combo.setMinimumWidth(115)
        row.addWidget(self.port_combo)
        self.refresh_ports_btn = QPushButton("刷新端口")
        row.addWidget(self.refresh_ports_btn)
        self.connect_btn = QPushButton("连接设备")
        self.connect_btn.setObjectName("primary")
        row.addWidget(self.connect_btn)
        self.disconnect_btn = QPushButton("断开")
        row.addWidget(self.disconnect_btn)
        row.addStretch()
        self.link_label = label("●  未连接", "linkBadge")
        row.addWidget(self.link_label)
        cl.addLayout(row)
        self.declared = QCheckBox("已加载对应 bit / ELF，且串口没有被其他工具占用")
        cl.addWidget(self.declared)
        self.identity = label("连接前核对配对文件；板端身份尚未读取。", "identity", True)
        cl.addWidget(self.identity)
        left.addWidget(connection)

        stats = QHBoxLayout()
        stats.setSpacing(10)
        for key, title, unit in (("fps", "帧率", "FPS"), ("bullets", "普通弹数量", "BULLETS"),
                                 ("lives", "剩余生命", "LIVES"), ("score", "游戏得分", "SCORE")):
            frame = QFrame()
            frame.setObjectName("metric")
            fl = QVBoxLayout(frame)
            fl.setContentsMargins(14, 10, 14, 10)
            fl.setSpacing(3)
            fl.addWidget(label(title, "metricName"))
            valrow = QHBoxLayout()
            value = label("—", "metricValue")
            value.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
            self.metric_labels[key] = value
            valrow.addWidget(value)
            unit_label = label(unit, "small")
            unit_label.setAlignment(Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight)
            valrow.addWidget(unit_label)
            fl.addLayout(valrow)
            stats.addWidget(frame, 1)
        left.addLayout(stats)

        control, ctl = card("场景与运行控制", "02 / SCENE")
        row = QHBoxLayout()
        for text, action, style in (("配置轻量 → 压力", "prepare", ""), ("开始 / 继续", "move", "primary"),
                                    ("开启护盾", "shield", "purple"), ("暂停", "stop", ""), ("退出游戏", "exit", "danger")):
            button = QPushButton(text)
            if style:
                button.setObjectName(style)
            button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            self.action_buttons[action] = button
            row.addWidget(button)
        ctl.addLayout(row)
        row = QHBoxLayout()
        center = QPushButton("校准摇杆中心")
        self.action_buttons["center"] = center
        row.addWidget(center)
        self.flipx, self.flipy = QCheckBox("X 反向"), QCheckBox("Y 反向")
        row.addWidget(self.flipx)
        row.addWidget(self.flipy)
        row.addStretch()
        row.addWidget(label("松开摇杆 · 停止场景后校准", "small"))
        ctl.addLayout(row)
        self.joy_label = label("摇杆  X —   Y —     ARM —     FAULTS —", "muted", True)
        self.flow_label = label("场景状态尚未读取", "muted", True)
        self.shield_label = label("护盾状态尚未读取", "muted", True)
        ctl.addWidget(self.joy_label)
        state_row = QHBoxLayout()
        state_row.addWidget(self.flow_label, 1)
        state_row.addWidget(self.shield_label, 1)
        ctl.addLayout(state_row)
        left.addWidget(control)

        logcard, ll = card("串口交互记录", "03 / TELEMETRY")
        grid = QGridLayout()
        grid.setSpacing(6)
        queries = (("刷新全部", "refresh"), ("基础状态", "status"), ("素材", "assets"),
                   ("游戏", "enemy"), ("XY 读数", "joy"), ("故障详情", "diag"),
                   ("转场", "flow"), ("护盾状态", "effects"))
        for i, (text, action) in enumerate(queries):
            button = QPushButton(text)
            button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            self.action_buttons[action] = button
            grid.addWidget(button, 0, i)
        ll.addLayout(grid)
        self.log_text = QPlainTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setMaximumBlockCount(1000)
        self.log_text.setFixedHeight(135)
        self.log_text.setPlaceholderText("等待连接 · 发送与接收记录将在这里显示")
        ll.addWidget(self.log_text, 1)
        self.logpath = label("日志保存于程序目录下的 board_logs", "small", True)
        ll.addWidget(self.logpath)
        left.addWidget(logcard, 1)

        keyboard, kl = card("键盘交互", "INPUT DECK")
        options = QHBoxLayout()
        self.keyboard_toggle = QCheckBox("键盘接管")
        self.preview_toggle = QCheckBox("离线演示")
        options.addWidget(self.keyboard_toggle)
        options.addStretch()
        options.addWidget(self.preview_toggle)
        kl.addLayout(options)
        keyboard_hint = label("按住移动 / 开火 · 按下 Tab 触发护盾", "small", True)
        kl.addWidget(keyboard_hint)
        keygrid = QGridLayout()
        keygrid.setSpacing(9)
        for key, caption, pos in (("W", "上移", (0, 1)), ("A", "左移", (1, 0)),
                                  ("S", "下移", (1, 1)), ("D", "右移", (1, 2))):
            cap = KeyCap(key, caption)
            self.keys[key] = cap
            keygrid.addWidget(cap, *pos)
        keygrid.addWidget(label("MOVE", "sectionTag"), 0, 0, alignment=Qt.AlignmentFlag.AlignBottom)
        keygrid.addWidget(label("W A S D", "small"), 0, 2, alignment=Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight)
        kl.addLayout(keygrid)
        space = KeyCap("SPACE", "空格 · 按住开火")
        tab = KeyCap("TAB", "Tab · 护盾", "#b6a1f5")
        self.keys.update(SPACE=space, TAB=tab)
        kl.addWidget(space)
        kl.addWidget(tab)
        self.capture_btn = QPushButton("点击捕获键盘 · Esc 释放")
        self.capture_btn.setObjectName("primary")
        self.capture_btn.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        kl.addWidget(self.capture_btn)
        self.keyboard_status = label("键盘未接管 · 可开启离线演示查看高亮", "muted", True)
        self.keyboard_status.setMinimumHeight(28)
        kl.addWidget(self.keyboard_status)
        kl.addWidget(label("高亮表示本地按键状态；实际发送与设备回复请查看左侧记录。", "small", True))
        right.addWidget(keyboard)

        helpcard, hl = card("操作提示", "HOW TO PLAY")
        for number, text in (("01", "WASD 可组合移动；松开空格停止开火。"),
                             ("02", "切出窗口或按 Esc 释放键态；无需键盘时关闭接管。")):
            row = QHBoxLayout()
            no = label(number, "sectionTag")
            no.setFixedWidth(22)
            row.addWidget(no, 0, Qt.AlignmentFlag.AlignTop)
            row.addWidget(label(text, "muted", True), 1)
            hl.addLayout(row)
        right.addWidget(helpcard)
        right.addStretch(1)

        self.notice = label("未连接设备。可先开启离线演示，查看键帽高亮与界面布局。", "notice", True)
        outer.addWidget(self.notice)
        self.statusBar().showMessage("2D RENDER CONSOLE  /  115200 · 8N1                                      LOCAL UI · NO DEVICE DATA")

