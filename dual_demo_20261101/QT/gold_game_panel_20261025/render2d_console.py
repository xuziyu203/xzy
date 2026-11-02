"""Qt front end for the supplied GOLD panel; offline UI is available without a board."""
from __future__ import annotations

import argparse
import json
import queue
import sys
import threading
from pathlib import Path

import gold_game_panel as backend


def make_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', default=backend.DEFAULT_ROOT)
    parser.add_argument('--port', default='COM15')
    parser.add_argument('--check-only', '--verify-only', dest='check_only', action='store_true')
    parser.add_argument('--preview', action='store_true', help='offline keyboard preview; never opens serial automatically')
    parser.add_argument('--screenshot', help='save an offline Qt rendering and exit (no serial)')
    return parser


def run_gui(args):
    from PySide6.QtCore import QTimer
    from PySide6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
    controller = create_controller(args, app)
    controller.view.show()
    if args.screenshot:
        def capture():
            controller.view.grab().save(str(Path(args.screenshot).resolve()))
            app.quit()
        QTimer.singleShot(350, capture)
    return app.exec()


def create_controller(args, app):
    """Deferred imports keep --check-only independent of Qt and serial."""
    from PySide6.QtCore import QObject, QEvent, QTimer, Qt
    from PySide6.QtWidgets import QLineEdit, QComboBox, QTextEdit, QPlainTextEdit, QAbstractSpinBox
    from panel_view import PanelView
    from keyboard_transport import InputMailbox, KeyboardWorker, KEY_MASKS

    qt_keys = {Qt.Key.Key_W: 'W', Qt.Key.Key_A: 'A', Qt.Key.Key_S: 'S',
               Qt.Key.Key_D: 'D', Qt.Key.Key_Space: 'SPACE', Qt.Key.Key_Tab: 'TAB'}

    class Controller(QObject):
        def __init__(self):
            super().__init__(app)
            self.view = PanelView()
            self.worker = None
            self.events = queue.Queue()
            self.requests = queue.Queue(maxsize=8)
            self.stop = threading.Event()
            self.mailbox = InputMailbox()
            self.connected = self.busy = self.keyboard_enabled = self.closing = False
            self.want_keyboard = False
            self.release_pending = False
            self.physical = set()
            self.mouse = set()
            self.last_frame_sample = None
            self.view.root_edit.setText(args.root)
            self.view.port_combo.setCurrentText(args.port)
            self.view.preview_toggle.setChecked(True)
            self.view.connect_btn.clicked.connect(self.connect_board)
            self.view.disconnect_btn.clicked.connect(self.disconnect_board)
            self.view.refresh_ports_btn.clicked.connect(self.refresh_ports)
            for action, button in self.view.action_buttons.items():
                button.clicked.connect(lambda checked=False, a=action: self.submit(a))
            self.view.keyboard_toggle.toggled.connect(self.keyboard_toggle)
            self.view.preview_toggle.toggled.connect(self.preview_toggle)
            self.view.capture_btn.clicked.connect(self.capture_keyboard)
            for key, button in self.view.keys.items():
                button.pressed.connect(lambda k=key: self.key_change(k, True, 'mouse'))
                button.released.connect(lambda k=key: self.key_change(k, False, 'mouse'))
            self.timer = QTimer(self)
            self.timer.setInterval(20)
            self.timer.timeout.connect(self.pump)
            self.timer.start()
            app.installEventFilter(self)
            app.applicationStateChanged.connect(self.application_state)
            self.view.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
            self.view.setFocus()
            self.show_notice('离线演示：WASD 移动 · 空格开火 · Tab 护盾；高亮仅表示本地按键，未发送串口。')
            self.refresh_controls()

        def show_notice(self, text, error=False):
            self.view.notice.setText(str(text))
            self.view.notice.setProperty('error', error)
            self.view.notice.style().unpolish(self.view.notice)
            self.view.notice.style().polish(self.view.notice)

        def append(self, text):
            self.view.log_text.appendPlainText(str(text))

        def editing(self):
            focus = app.focusWidget()
            if isinstance(focus, (QLineEdit, QComboBox, QAbstractSpinBox)):
                return True
            return isinstance(focus, (QTextEdit, QPlainTextEdit)) and not focus.isReadOnly()

        def capture_allowed(self):
            return (not self.closing and not self.editing()
                    and (self.view.preview_toggle.isChecked() or self.keyboard_enabled))

        def capture_keyboard(self):
            self.view.setFocus(Qt.FocusReason.MouseFocusReason)
            if self.capture_allowed():
                self.show_notice('键盘已聚焦。WASD / 空格支持同时按住；Tab 每次按下触发一次，Esc 释放。')
            else:
                self.show_notice('请开启离线演示，或连接后勾选键盘接管。')

        def active_keys(self):
            return self.physical | self.mouse

        def mask(self):
            mask = 0
            for key in self.active_keys():
                mask |= KEY_MASKS.get(key, 0)
            return mask

        def key_change(self, key, down, source='keyboard'):
            held = self.physical if source == 'keyboard' else self.mouse
            if down and not self.capture_allowed():
                return
            was_active = key in self.active_keys()
            if down:
                held.add(key)
            else:
                held.discard(key)
            is_active = key in self.active_keys()
            self.view.keys[key].set_active(is_active)
            if was_active == is_active:
                return
            if self.view.preview_toggle.isChecked():
                if key == 'TAB' and is_active:
                    self.append('[离线预览 · 未发送] Tab → PLAYER SHIELD')
                elif key != 'TAB':
                    self.append(f'[离线预览 · 未发送] {key} {"按下" if down else "松开"} → 输入掩码 {self.mask():02X}')
                self.view.keyboard_status.setText('离线演示 · ' + (' + '.join(sorted(self.active_keys())) or '全部松开'))
            elif self.keyboard_enabled:
                self.mailbox.update(self.mask())
                if key == 'TAB' and is_active:
                    self.submit('shield_key')

        def clear_keys(self):
            self.physical.clear()
            self.mouse.clear()
            for button in self.view.keys.values():
                button.set_active(False)
                button.setDown(False)
            self.mailbox.clear()

        def set_keyboard_check(self, value):
            self.view.keyboard_toggle.blockSignals(True)
            self.view.keyboard_toggle.setChecked(value)
            self.view.keyboard_toggle.blockSignals(False)

        def suspend_keyboard(self, reason):
            had_live = self.keyboard_enabled or self.want_keyboard
            self.clear_keys()
            self.want_keyboard = self.keyboard_enabled = False
            self.set_keyboard_check(False)
            if had_live and self.connected:
                if self.enqueue('keyboard_off'):
                    self.release_pending = True
                    self.busy = True
            self.view.keyboard_status.setText(reason)
            self.refresh_controls()

        def eventFilter(self, obj, event):
            typ = event.type()
            if obj is self.view and typ == QEvent.Type.Close and not self.closing:
                if self.worker and self.worker.is_alive():
                    event.ignore()
                    self.closing = True
                    self.disconnect_board()
                    return True
                self.dispose()
                return False
            if typ == QEvent.Type.WindowDeactivate and obj is self.view:
                self.suspend_keyboard('窗口失焦 · 已释放输入；接管需重新开启')
            if typ == QEvent.Type.FocusIn and isinstance(obj, (QLineEdit, QComboBox, QAbstractSpinBox)):
                self.suspend_keyboard('编辑连接参数 · 已释放输入')
            if typ not in (QEvent.Type.KeyPress, QEvent.Type.KeyRelease, QEvent.Type.ShortcutOverride):
                return False
            if not hasattr(obj, 'window') or obj.window() is not self.view:
                return False
            key = event.key()
            if key == Qt.Key.Key_Escape and typ == QEvent.Type.KeyPress and not self.editing():
                self.suspend_keyboard('Esc · 已释放全部输入')
                return True
            name = qt_keys.get(key)
            if name is None:
                return False
            # Consume Tab only while in game/preview context, never in editable fields.
            if typ == QEvent.Type.ShortcutOverride:
                if self.capture_allowed() and not event.modifiers() & (Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier | Qt.KeyboardModifier.MetaModifier):
                    event.accept()
                    return True
                return False
            if event.isAutoRepeat():
                return self.capture_allowed() or name in self.physical
            if typ == QEvent.Type.KeyRelease and name in self.physical:
                self.key_change(name, False)
                return True
            if typ == QEvent.Type.KeyPress and self.capture_allowed():
                # Ctrl/Alt combinations belong to the OS/editor, not the game.
                if event.modifiers() & (Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.AltModifier | Qt.KeyboardModifier.MetaModifier):
                    self.suspend_keyboard('系统快捷键 · 已释放输入')
                    return False
                self.key_change(name, True)
                return True
            return False

        def application_state(self, state):
            if state != Qt.ApplicationState.ApplicationActive:
                self.suspend_keyboard('应用失焦 · 已释放输入')

        def preview_toggle(self, enabled):
            self.suspend_keyboard('离线演示 · 未发送串口' if enabled else '键盘未接管')
            self.show_notice('离线演示仅显示本地键态，不发送键盘命令。' if enabled else '连接并开始游戏后，可勾选键盘接管。')
            self.view.setFocus()

        def keyboard_toggle(self, enabled):
            if not enabled:
                self.suspend_keyboard('键盘已释放')
                return
            if not self.connected or self.busy or self.release_pending:
                self.set_keyboard_check(False)
                self.show_notice('请先连接匹配固件并开始游戏，再开启键盘接管。', True)
                return
            self.view.preview_toggle.blockSignals(True)
            self.view.preview_toggle.setChecked(False)
            self.view.preview_toggle.blockSignals(False)
            self.clear_keys()
            self.want_keyboard = True
            self.view.setFocus()
            self.submit('keyboard_on')

        def refresh_ports(self):
            try:
                from serial.tools import list_ports
                current = self.view.port_combo.currentText()
                ports = sorted(p.device for p in list_ports.comports())
                self.view.port_combo.clear()
                self.view.port_combo.addItems(ports)
                self.view.port_combo.setCurrentText(current)
                self.show_notice('已刷新串口列表；未打开端口。')
            except Exception as exc:
                self.show_notice(str(exc), True)

        def connect_board(self):
            if self.worker and self.worker.is_alive():
                return
            if not self.view.declared.isChecked():
                self.show_notice('请确认已加载原包匹配的 GOLD 配对且串口空闲，再勾选连接声明。', True)
                return
            self.suspend_keyboard('正在连接 · 键盘未接管')
            self.view.preview_toggle.blockSignals(True)
            self.view.preview_toggle.setChecked(False)
            self.view.preview_toggle.blockSignals(False)
            self.requests = queue.Queue(maxsize=8)
            self.events = queue.Queue()
            self.stop = threading.Event()
            self.mailbox = InputMailbox()
            self.worker = KeyboardWorker(self.view.root_edit.text().strip(), self.view.port_combo.currentText().strip(),
                                         Path(__file__).resolve().parent / 'board_logs', self.requests, self.events,
                                         self.stop, self.mailbox)
            self.worker.start()
            self.show_notice('正在核验原候选本地 SHA 并连接；不自动开始游戏。')
            self.refresh_controls()

        def disconnect_board(self):
            self.clear_keys()
            self.want_keyboard = self.keyboard_enabled = False
            self.set_keyboard_check(False)
            self.stop.set()
            self.connected = False
            self.release_pending = False
            self.show_notice('正在释放键盘并关闭串口；不发送游戏退出或复位。')
            self.refresh_controls()

        def enqueue(self, action):
            invert = int(self.view.flipx.isChecked()) | (int(self.view.flipy.isChecked()) << 1)
            try:
                self.requests.put_nowait((action, invert))
                return True
            except queue.Full:
                self.clear_keys()
                self.show_notice('操作队列已满，输入已清零，请等待当前操作完成。', True)
                return False

        def submit(self, action):
            if not self.connected:
                return
            if self.busy and action != 'shield_key':
                return
            if self.keyboard_enabled and action not in ('shield', 'shield_key', 'stop', 'exit'):
                self.show_notice('键盘接管期间保持输入优先；先释放键盘再执行此操作。')
                return
            if action in ('stop', 'exit'):
                self.clear_keys()
                self.want_keyboard = self.keyboard_enabled = False
                self.set_keyboard_check(False)
            if self.enqueue(action):
                if action != 'shield_key':
                    self.busy = True
                self.refresh_controls()

        def decoded(self, record):
            kind = record['kind']
            if kind == 'enemy':
                f = record['fields']
                for key, field in [('bullets', 'MEMBERS_VISIBLE'), ('lives', 'LIVES'), ('score', 'SCORE')]:
                    self.view.metric_labels[key].setText(f.get(field, '—'))
                try:
                    sample = (int(f['RUN_US']), int(f['RUN_FRAMES']))
                    prev = self.last_frame_sample
                    if prev and sample[0] > prev[0] and sample[1] >= prev[1]:
                        value = (sample[1] - prev[1]) * 1_000_000 / (sample[0] - prev[0])
                        self.view.metric_labels['fps'].setText(f'{value:.1f}')
                    else:
                        self.view.metric_labels['fps'].setText('—')
                    self.last_frame_sample = sample
                except (KeyError, ValueError):
                    self.last_frame_sample = None
                    self.view.metric_labels['fps'].setText(f.get('FPS', '—'))
                self.view.flow_label.setText(f"游戏 {f.get('STATE', '—')}  /  GAME {f.get('GAME', '—')}  /  MOVING {f.get('MOVING', '—')}")
            elif kind == 'joy':
                x, y = (record['x'], record['y']) if record['valid'] else ('—', '—')
                self.view.joy_label.setText(f"摇杆 X {x} · Y {y}  / ARM {record['armed']}  / FAULTS {record['faults']}")
            elif kind == 'effects':
                f = record['fields']
                self.view.shield_label.setText(f"护盾 ACTIVE {f.get('ACTIVE', '—')}  / MODE {f.get('MODE', '—')}  / ABSORBED {f.get('ABSORBED', '—')}")
            elif kind == 'flow':
                self.view.flow_label.setText(record['raw'])
            elif kind == 'diag':
                self.append('故障计数 ' + json.dumps({k: v for k, v in record['counts'].items() if v}, ensure_ascii=False))

        def refresh_controls(self):
            alive = self.worker is not None and self.worker.is_alive()
            self.view.connect_btn.setEnabled(not alive and not self.closing)
            self.view.disconnect_btn.setEnabled(alive and not self.stop.is_set())
            for widget in (self.view.root_edit, self.view.port_combo, self.view.refresh_ports_btn, self.view.declared):
                widget.setEnabled(not alive)
            for action, button in self.view.action_buttons.items():
                allowed = not self.keyboard_enabled or action in ('shield', 'stop', 'exit')
                button.setEnabled(self.connected and not self.busy and allowed)
            self.view.keyboard_toggle.setEnabled(self.connected and not self.busy and not self.release_pending)
            self.view.preview_toggle.setEnabled(not self.busy)
            self.view.link_label.setText('● 串口已连接' if self.connected else '○ 离线 / 未连接')
            scope = '最近板端回包 · 非性能验收窗口' if self.connected else '离线 · 指标为未查询或上次回包'
            self.view.statusBar().showMessage('2D RENDER CONSOLE  /  115200 · 8N1     |     ' + scope)

        def pump(self):
            if self.keyboard_enabled and self.want_keyboard and not self.editing() and self.view.isActiveWindow():
                self.mailbox.update(self.mask())
            for _ in range(300):
                try:
                    kind, value = self.events.get_nowait()
                except queue.Empty:
                    break
                if kind in ('tx', 'rx'):
                    # INPUT heartbeats are logged losslessly to disk, but not 50 lines/s in the UI.
                    if kind != 'tx' or not str(value).startswith('PLAYER INPUT '):
                        self.append(('TX  › ' if kind == 'tx' else 'RX  ‹ ') + str(value))
                elif kind == 'input_tx':
                    self.view.keyboard_status.setText(f"已写入串口 · MASK {value['mask']:02X} · SEQ {value['seq']:08X}")
                elif kind == 'decoded':
                    self.decoded(value)
                elif kind == 'connected':
                    self.connected = True
                    self.last_frame_sample = None
                    for label in self.view.metric_labels.values():
                        label.setText('—')
                    self.view.identity.setText('本地配对 SHA 已核验 · 板端身份仍需按原包要求确认')
                    self.show_notice(value)
                elif kind == 'disconnected':
                    self.connected = self.busy = False
                    self.release_pending = False
                    self.suspend_keyboard('串口已断开 · 键盘已释放')
                    self.append(value)
                elif kind == 'busy':
                    self.busy = bool(value)
                elif kind == 'keyboard':
                    enabled = bool(value.get('enabled'))
                    self.keyboard_enabled = enabled and self.want_keyboard
                    if enabled and not self.want_keyboard:
                        self.mailbox.clear()
                        self.enqueue('keyboard_off')
                    if not enabled:
                        self.want_keyboard = False
                        self.clear_keys()
                    self.set_keyboard_check(self.keyboard_enabled)
                    self.view.keyboard_status.setText(value.get('reason', '键盘已接管' if enabled else '键盘已释放'))
                elif kind == 'build':
                    self.view.identity.setText('板端 BUILD: ' + str(value))
                elif kind == 'log':
                    self.view.logpath.setText('日志：' + str(value))
                elif kind == 'error':
                    self.show_notice(value, True)
                    self.append('ERROR › ' + str(value))
                    if self.want_keyboard or self.keyboard_enabled:
                        self.suspend_keyboard('命令失败 · 已释放键盘')
                elif kind == 'done':
                    if str(value) == '键盘接管已关闭。':
                        self.release_pending = False
                    message = str(value).replace('用 KEY1 开火取得 10 个确认击杀', '按板端设置的击毁阈值进入压力模式')
                    self.show_notice(message)
            self.refresh_controls()
            if self.closing and (self.worker is None or not self.worker.is_alive()):
                self.dispose()
                self.view.close()

        def dispose(self):
            self.timer.stop()
            app.removeEventFilter(self)
            try:
                app.applicationStateChanged.disconnect(self.application_state)
            except RuntimeError:
                pass

    return Controller()


def main():
    args = make_parser().parse_args()
    if args.check_only:
        print(json.dumps(backend.verify_pair(args.root), ensure_ascii=False, indent=2))
        print('PASS local files only; no Qt or serial imported/opened')
        return 0
    return run_gui(args)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f'{type(exc).__name__}: {exc}', file=sys.stderr)
        raise SystemExit(1)
