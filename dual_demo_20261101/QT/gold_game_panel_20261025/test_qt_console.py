"""Offscreen Qt interaction tests. No board, COM port or FPGA is accessed."""
from __future__ import annotations

import os
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

import queue
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest import mock

from PySide6.QtCore import QEvent, QPoint, Qt
from PySide6.QtGui import QKeyEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

import render2d_console


class QtConsoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.serial_patch = mock.patch('serial.Serial', side_effect=AssertionError('TEST MUST NOT OPEN SERIAL'))
        self.serial_mock = self.serial_patch.start()
        args = SimpleNamespace(root='OFFLINE_TEST_NO_FILES', port='COM_TEST_NEVER_OPEN', preview=True, screenshot=None)
        self.c = render2d_console.create_controller(args, self.app)
        self.c.timer.stop()
        self.v = self.c.view
        self.v.show()
        self.v.activateWindow()
        self.v.setFocus()
        self.app.processEvents()
        self.c.capture_keyboard()
        self.app.processEvents()

    def tearDown(self):
        self.c.connected = False
        self.c.worker = None
        self.v.close()
        self.v.deleteLater()
        self.app.processEvents()
        self.serial_mock.assert_not_called()
        self.serial_patch.stop()

    def press(self, key):
        QTest.keyPress(self.v, key)
        self.app.processEvents()

    def release(self, key):
        QTest.keyRelease(self.v, key)
        self.app.processEvents()

    def drain(self):
        result = []
        while True:
            try:
                result.append(self.c.requests.get_nowait())
            except queue.Empty:
                return result

    def make_live_without_worker(self):
        self.v.preview_toggle.blockSignals(True)
        self.v.preview_toggle.setChecked(False)
        self.v.preview_toggle.blockSignals(False)
        self.c.connected = True
        self.c.keyboard_enabled = True
        self.c.want_keyboard = True
        self.c.set_keyboard_check(True)
        self.c.refresh_controls()
        self.v.setFocus()
        self.app.processEvents()

    def test_launch_is_offline_with_no_fabricated_metrics(self):
        self.assertTrue(self.v.preview_toggle.isChecked())
        self.assertFalse(self.c.connected)
        self.assertFalse(self.c.keyboard_enabled)
        self.assertIsNone(self.c.worker)
        self.assertEqual({label.text() for label in self.v.metric_labels.values()}, {'—'})
        self.assertIn('离线', self.v.link_label.text())

    def test_w_press_and_release_changes_drawn_key_state(self):
        self.press(Qt.Key.Key_W)
        self.assertTrue(self.v.keys['W'].is_active())
        self.assertEqual(self.c.mask(), 0x01)
        self.release(Qt.Key.Key_W)
        self.assertFalse(self.v.keys['W'].is_active())
        self.assertEqual(self.c.mask(), 0)
        self.assertIn('未发送', self.v.log_text.toPlainText())
        self.assertEqual(self.drain(), [])

    def test_movement_and_fire_can_be_held_together(self):
        for key in (Qt.Key.Key_W, Qt.Key.Key_D, Qt.Key.Key_Space):
            self.press(key)
        self.assertEqual(self.c.mask(), 0x29)
        self.assertTrue(all(self.v.keys[k].is_active() for k in ('W', 'D', 'SPACE')))
        self.release(Qt.Key.Key_W)
        self.assertEqual(self.c.mask(), 0x28)
        self.release(Qt.Key.Key_Space)
        self.release(Qt.Key.Key_D)
        self.assertEqual(self.c.mask(), 0)

    def test_tab_is_one_shield_preview_per_press_not_focus_traversal(self):
        focus = self.app.focusWidget()
        self.press(Qt.Key.Key_Tab)
        self.assertTrue(self.v.keys['TAB'].is_active())
        self.assertIs(self.app.focusWidget(), focus)
        for _ in range(5):
            event = QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_Tab,
                              Qt.KeyboardModifier.NoModifier, '\t', True, 1)
            self.app.sendEvent(self.v, event)
        self.assertEqual(self.v.log_text.toPlainText().count('PLAYER SHIELD'), 1)
        self.release(Qt.Key.Key_Tab)
        self.assertFalse(self.v.keys['TAB'].is_active())
        self.press(Qt.Key.Key_Tab)
        self.release(Qt.Key.Key_Tab)
        self.assertEqual(self.v.log_text.toPlainText().count('PLAYER SHIELD'), 2)

    def test_editor_receives_letters_and_space_without_game_input(self):
        self.press(Qt.Key.Key_W)
        self.v.root_edit.setFocus()
        self.app.processEvents()
        self.assertEqual(self.c.mask(), 0)
        self.v.root_edit.selectAll()
        QTest.keyClicks(self.v.root_edit, 'wasd ')
        self.assertEqual(self.v.root_edit.text(), 'wasd ')
        self.assertEqual(self.c.active_keys(), set())
        self.assertFalse(any(key.is_active() for key in self.v.keys.values()))
        self.assertEqual(self.drain(), [])
        QTest.keyClick(self.v.root_edit, Qt.Key.Key_Tab)
        self.assertFalse(self.v.keys['TAB'].is_active())
        self.assertNotIn('PLAYER SHIELD', self.v.log_text.toPlainText())

    def test_window_deactivation_releases_all_highlights(self):
        self.press(Qt.Key.Key_W)
        self.press(Qt.Key.Key_Space)
        self.app.sendEvent(self.v, QEvent(QEvent.Type.WindowDeactivate))
        self.assertEqual(self.c.mask(), 0)
        self.assertFalse(any(key.is_active() for key in self.v.keys.values()))

    def test_escape_releases_keys_and_disarms_live_input(self):
        self.make_live_without_worker()
        self.press(Qt.Key.Key_D)
        self.press(Qt.Key.Key_Space)
        self.press(Qt.Key.Key_Escape)
        self.assertEqual(self.c.mask(), 0)
        self.assertFalse(self.c.keyboard_enabled)
        self.assertFalse(self.c.want_keyboard)
        self.assertFalse(self.v.keyboard_toggle.isChecked())
        self.assertEqual([a for a, _ in self.drain()], ['keyboard_off'])

    def test_mouse_keycap_uses_same_mask_and_highlight(self):
        key = self.v.keys['A']
        QTest.mousePress(key, Qt.MouseButton.LeftButton, pos=key.rect().center())
        self.assertEqual(self.c.mask(), 0x02)
        self.assertTrue(key.is_active())
        QTest.mouseRelease(key, Qt.MouseButton.LeftButton, pos=key.rect().center())
        self.assertEqual(self.c.mask(), 0)
        self.assertFalse(key.is_active())

    def test_mouse_and_physical_holds_are_combined(self):
        key = self.v.keys['W']
        self.press(Qt.Key.Key_W)
        QTest.mousePress(key, Qt.MouseButton.LeftButton, pos=key.rect().center())
        self.release(Qt.Key.Key_W)
        self.assertEqual(self.c.mask(), 0x01)
        self.assertTrue(key.is_active())
        QTest.mouseRelease(key, Qt.MouseButton.LeftButton, pos=key.rect().center())
        self.assertEqual(self.c.mask(), 0)
        self.assertFalse(key.is_active())

    def test_live_tab_queues_one_shield_command_per_physical_edge(self):
        self.make_live_without_worker()
        self.press(Qt.Key.Key_Tab)
        for _ in range(5):
            self.app.sendEvent(self.v, QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_Tab,
                               Qt.KeyboardModifier.NoModifier, '\t', True, 1))
        self.assertEqual([a for a, _ in self.drain()], ['shield_key'])
        self.release(Qt.Key.Key_Tab)
        self.press(Qt.Key.Key_Tab)
        self.release(Qt.Key.Key_Tab)
        self.assertEqual([a for a, _ in self.drain()], ['shield_key'])

    def test_live_focus_loss_queues_release_and_requires_reenable(self):
        self.make_live_without_worker()
        self.press(Qt.Key.Key_S)
        self.app.sendEvent(self.v, QEvent(QEvent.Type.WindowDeactivate))
        self.assertEqual(self.c.mask(), 0)
        self.assertFalse(self.c.keyboard_enabled)
        self.assertFalse(self.v.keyboard_toggle.isChecked())
        self.assertEqual([a for a, _ in self.drain()], ['keyboard_off'])
        self.press(Qt.Key.Key_W)
        self.assertEqual(self.c.mask(), 0)

    def test_ctrl_shortcut_does_not_send_game_input(self):
        self.make_live_without_worker()
        QTest.keyPress(self.v, Qt.Key.Key_A, Qt.KeyboardModifier.ControlModifier)
        self.assertEqual(self.c.mask(), 0)
        self.assertFalse(self.c.keyboard_enabled)

    def test_title_centered_and_keycaps_on_right(self):
        title = self.v.title_label
        self.assertEqual(title.alignment(), Qt.AlignmentFlag.AlignCenter)
        center_x = title.mapTo(self.v, QPoint(title.width() // 2, 0)).x()
        self.assertLessEqual(abs(center_x - self.v.width() // 2), 10)
        for key in self.v.keys.values():
            self.assertGreater(key.mapTo(self.v, QPoint(0, 0)).x(), self.v.width() // 2)

    def test_failed_keyboard_enable_clears_requested_state(self):
        self.make_live_without_worker()
        self.c.events.put(('error', 'PLAYER CONNECT rejected'))
        self.c.pump()
        self.assertFalse(self.c.want_keyboard)
        self.assertFalse(self.c.keyboard_enabled)
        self.assertFalse(self.v.keyboard_toggle.isChecked())
        self.assertEqual(self.c.mask(), 0)

    def test_pending_release_blocks_reenable_until_matching_done(self):
        self.make_live_without_worker()
        self.c.suspend_keyboard('test release')
        self.assertTrue(self.c.release_pending)
        self.assertFalse(self.v.keyboard_toggle.isEnabled())
        self.assertEqual([action for action, _ in self.drain()], ['keyboard_off'])
        # A previous ordinary operation can finish after release was queued.
        self.c.events.put(('busy', False))
        self.c.pump()
        self.assertTrue(self.c.release_pending)
        self.assertFalse(self.v.keyboard_toggle.isEnabled())
        # Only completion of this keyboard-off operation unlocks reactivation.
        self.c.events.put(('keyboard', {'enabled': False, 'session': None, 'reason': '键盘接管已关闭。'}))
        self.c.events.put(('done', '键盘接管已关闭。'))
        self.c.events.put(('busy', False))
        self.c.pump()
        self.assertFalse(self.c.release_pending)
        self.assertFalse(self.c.keyboard_enabled)
        self.assertTrue(self.v.keyboard_toggle.isEnabled())

    def test_controller_worker_serial_keyboard_end_to_end(self):
        """Real Qt events + real worker; only local pair check and serial are fake."""
        import gold_game_panel as backend

        class ScriptedSerial:
            def __init__(self):
                self.condition = threading.Condition()
                self.rx = bytearray()
                self.writes = []
                self.opened = self.closed = False
                self.port = None
                self.dtr = self.rts = True

            def open(self):
                if self.dtr or self.rts:
                    raise AssertionError('DTR/RTS must be low before fake open')
                self.opened = True

            @property
            def in_waiting(self):
                with self.condition:
                    return len(self.rx)

            def read(self, count):
                with self.condition:
                    if not self.rx and not self.closed:
                        self.condition.wait(0.005)
                    data = bytes(self.rx[:count])
                    del self.rx[:count]
                    return data

            def write(self, data):
                with self.condition:
                    self.writes.append(bytes(data))
                    if data == b'ENEMY STATUS\n':
                        self.rx.extend(b'ENEMY STATUS STATE=DRAWING ERROR=00000000 MOVING=1 GAME=1 GAME_OVER=0 '
                                       b'RUN_US=1000000 RUN_FRAMES=60 LIVES=3 SCORE=0 MEMBERS_VISIBLE=1134\r\n')
                    elif data == b'PLAYER CONNECT\n':
                        self.rx.extend(b'PLAYER CONNECTED CAFEBABE\r\n')
                    elif data == b'PLAYER SHIELD\n':
                        self.rx.extend(b'PLAYER SHIELD ACCEPTED\r\n')
                    elif not data.startswith(b'PLAYER INPUT CAFEBABE '):
                        raise AssertionError(f'Unexpected fake-wire command: {data!r}')
                    self.condition.notify_all()
                    return len(data)

            def close(self):
                with self.condition:
                    self.closed = True
                    self.condition.notify_all()

            def snapshot(self):
                with self.condition:
                    return list(self.writes)

        wire = ScriptedSerial()
        real_journal = backend.Journal

        def wait_until(predicate, description, timeout=2.0):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                self.app.processEvents()
                if predicate():
                    return
                QTest.qWait(10)
            self.fail(f'{description}; UI log={self.v.log_text.toPlainText()!r}; wire={wire.snapshot()!r}')

        def input_packets():
            return [data for data in wire.snapshot() if data.startswith(b'PLAYER INPUT ')]

        def masks_after(index):
            return [data.split()[-1] for data in input_packets()[index:]]

        with tempfile.TemporaryDirectory() as td, \
                mock.patch.object(backend, 'verify_pair', return_value={'firmware_id': 'SYNTHETIC_OFFLINE_TEST'}), \
                mock.patch.object(backend, 'Journal', side_effect=lambda base, identity, port: real_journal(td, identity, port)), \
                mock.patch('serial.Serial', return_value=wire) as factory:
            try:
                self.c.timer.start()
                self.v.declared.setChecked(True)
                QTest.mouseClick(self.v.connect_btn, Qt.MouseButton.LeftButton)
                wait_until(lambda: self.c.connected, 'controller receives connection event')
                self.assertTrue(wire.opened)
                self.assertEqual(wire.snapshot(), [], 'connecting alone must not configure or start a game')
                QTest.mouseClick(self.v.keyboard_toggle, Qt.MouseButton.LeftButton,
                                 pos=QPoint(8, self.v.keyboard_toggle.height() // 2))
                wait_until(lambda: self.c.keyboard_enabled, 'real session handshake enables keyboard')
                self.assertEqual(wire.snapshot()[:3], [b'ENEMY STATUS\n', b'PLAYER CONNECT\n',
                                                     b'PLAYER INPUT CAFEBABE 00000000 00\n'])
                self.press(Qt.Key.Key_W)
                self.press(Qt.Key.Key_Space)
                wait_until(lambda: b'21' in masks_after(0), 'W + Space reaches worker as full mask 21')
                self.assertTrue(self.v.keys['W'].is_active())
                self.assertTrue(self.v.keys['SPACE'].is_active())
                before_release = len(input_packets())
                self.release(Qt.Key.Key_W)
                self.release(Qt.Key.Key_Space)
                wait_until(lambda: b'00' in masks_after(before_release), 'key releases reach fake serial')
                self.press(Qt.Key.Key_Tab)
                for _ in range(3):
                    self.app.sendEvent(self.v, QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_Tab,
                                       Qt.KeyboardModifier.NoModifier, '\t', True, 1))
                self.release(Qt.Key.Key_Tab)
                wait_until(lambda: wire.snapshot().count(b'PLAYER SHIELD\n') == 1, 'one Tab edge sends shield')
                wait_until(lambda: 'PLAYER SHIELD ACCEPTED' in self.v.log_text.toPlainText(), 'shield ACK reaches GUI')
                self.press(Qt.Key.Key_D)
                wait_until(lambda: input_packets()[-1].split()[-1] == b'08', 'D hold is delivered')
                before_deactivate = len(input_packets())
                self.app.sendEvent(self.v, QEvent(QEvent.Type.WindowDeactivate))
                wait_until(lambda: b'00' in masks_after(before_deactivate), 'focus loss sends a neutral packet')
                self.assertFalse(self.c.keyboard_enabled)
                self.assertEqual(self.c.mask(), 0)
                self.assertFalse(any(key.is_active() for key in self.v.keys.values()))
                QTest.mouseClick(self.v.disconnect_btn, Qt.MouseButton.LeftButton)
                wait_until(lambda: not self.c.worker.is_alive(), 'worker closes after disconnect')
                self.c.worker.join(timeout=1)
                self.app.processEvents()
                self.assertTrue(wire.closed)
                self.assertEqual(wire.snapshot().count(b'PLAYER SHIELD\n'), 1)
                self.assertEqual(wire.snapshot().count(b'PLAYER CONNECT\n'), 1)
                self.assertFalse(any(b'RESET' in data or b'ENEMY EXIT' in data for data in wire.snapshot()))
                packets = input_packets()
                self.assertEqual(packets[-1].split()[-1], b'00')
                self.assertEqual([int(data.split()[3], 16) for data in packets], list(range(len(packets))))
                factory.assert_called_once()
            finally:
                self.c.stop.set()
                if self.c.worker is not None:
                    self.c.worker.join(timeout=2)
                self.c.timer.stop()


if __name__ == '__main__':
    unittest.main(verbosity=2)
