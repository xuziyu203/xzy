"""Offline keyboard transport tests. Fake serial only; no COM or board access."""
import queue
import tempfile
import threading
import unittest
from unittest import mock

import gold_game_panel as core
import keyboard_transport as kt


ACTIVE = b"ENEMY STATUS STATE=DRAWING GAME=1 MOVING=1 GAME_OVER=0 ERROR=00000000\n"


class Clock:
    def __init__(self):
        self.value = 0.0
    def __call__(self):
        return self.value
    def advance(self, seconds):
        self.value += seconds


class MemoryJournal:
    def __init__(self):
        self.rx = bytearray()
        self.tx = bytearray()
        self.events = []
        self.observed_build = None
    def receive(self, data):
        self.rx.extend(data)
    def accepted(self, data):
        self.tx.extend(data)
    def event(self, kind, **data):
        self.events.append((kind, data))


class BoardSerial:
    def __init__(self, clock):
        self.clock = clock
        self.data = bytearray()
        self.writes = []
        self.status = ACTIVE
        self.connect = b"PLAYER CONNECTED A1B2C3D4\n"
        self.partial_write = None
        self.write_error = False
        self.before_read = None
        self.after_write = None
        self.delayed = []
        self.shield_delay = 0
        self.shield_reply = b"PLAYER SHIELD ACCEPTED\n"
        self.closed = False
    @property
    def in_waiting(self):
        return len(self.data)
    def read(self, n):
        self.clock.advance(.001)
        if self.before_read:
            self.before_read()
        for due, data in list(self.delayed):
            if self.clock() >= due:
                self.data.extend(data)
                self.delayed.remove((due, data))
        out = bytes(self.data[:n])
        del self.data[:n]
        return out
    def write(self, data):
        self.writes.append(data)
        if self.write_error:
            raise OSError("fake wire failure")
        if data == b"ENEMY STATUS\n":
            self.data.extend(self.status)
        elif data == b"PLAYER CONNECT\n":
            self.data.extend(self.connect)
        elif data == b"PLAYER SHIELD\n":
            self.delayed.append((self.clock() + self.shield_delay, self.shield_reply))
        elif data == b"ENEMY STOP\n":
            self.data.extend(b"ENEMY STOPPED\n")
        if self.after_write:
            self.after_write(data)
        return len(data) if self.partial_write is None else self.partial_write
    def close(self):
        self.closed = True


def fixture():
    clock = Clock()
    mailbox = kt.InputMailbox(clock)
    ser = BoardSerial(clock)
    log = MemoryJournal()
    events = []
    stop = threading.Event()
    proto = kt.KeyboardProtocol(ser, log, lambda k, d: events.append((k, d)), stop, mailbox, clock)
    return proto, ser, log, mailbox, clock, events


def input_packets(ser):
    return [p for p in ser.writes if p.startswith(b"PLAYER INPUT ")]


class EncodingTests(unittest.TestCase):
    def test_session_strict_full_match(self):
        self.assertEqual(kt.parse_session("PLAYER CONNECTED abcD0123"), 0xABCD0123)
        for s in ("PLAYER CONNECTED 123", "PLAYER CONNECTED 123456789", "PLAYER CONNECTED 00000000 EXTRA", "PLAYER CONNECTED ZZZZZZZZ"):
            with self.subTest(s=s), self.assertRaises(core.PanelError):
                kt.parse_session(s)
    def test_full_mask_and_wire_format(self):
        self.assertEqual(kt.KEY_MASKS, dict(W=1, A=2, S=4, D=8, SPACE=32))
        self.assertEqual(kt.encode_input(0x123, 0xABC, 0x29), b"PLAYER INPUT 00000123 00000ABC 29\n")
    def test_invalid_fields_rejected_before_write(self):
        for args in ((-1,0,0), (0,2**32,0), (0,0,0x40), (0,0,0x10), (0,0,-1)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                kt.encode_input(*args)
    def test_mailbox_coalesces_latest_state(self):
        m = kt.InputMailbox()
        for mask in (1,3,2,0,0x28):
            m.update(mask)
        self.assertEqual(m.snapshot().mask, 0x28)
    def test_mailbox_clear_is_sticky_generation(self):
        m = kt.InputMailbox()
        m.update(1)
        g = m.snapshot().generation
        m.clear()
        m.update(32)
        self.assertNotEqual(m.snapshot().generation, g)


class StreamingTests(unittest.TestCase):
    def test_opt_in_handshake_initial_neutral_silent(self):
        p,s,l,m,c,e = fixture()
        m.update(0x29)  # Stale held keys must be discarded.
        p.enable_keyboard()
        self.assertEqual(s.writes, [b"ENEMY STATUS\n", b"PLAYER CONNECT\n", b"PLAYER INPUT A1B2C3D4 00000000 00\n"])
        self.assertTrue(p.keyboard_enabled)
        self.assertEqual(p.sequence, 1)
        self.assertTrue(any(k == "keyboard" and d["enabled"] for k,d in e))
        self.assertFalse(any(x[0] == "COMMAND_TIMEOUT" for x in l.events))
    def test_not_running_never_connects_or_reconfigures(self):
        p,s,_,_,_,_ = fixture()
        s.status = ACTIVE.replace(b"MOVING=1", b"MOVING=0")
        with self.assertRaisesRegex(core.PanelError, "正在运行"):
            p.enable_keyboard()
        self.assertEqual(s.writes, [b"ENEMY STATUS\n"])
    def test_malformed_connect_does_not_send_input(self):
        p,s,_,_,_,_ = fixture()
        s.connect = b"PLAYER CONNECTED ABC\n"
        with self.assertRaises(core.PanelError):
            p.enable_keyboard()
        self.assertEqual(input_packets(s), [])
    def test_press_combination_release_and_sequence(self):
        p,s,_,m,_,_ = fixture()
        p.enable_keyboard()
        for mask in (0x01,0x09,0x29,0x20,0):
            m.update(mask)
            p.tick_input()
        self.assertEqual([x.split()[-1] for x in input_packets(s)], [b"00",b"01",b"09",b"29",b"20",b"00"])
        self.assertEqual([x.split()[3] for x in input_packets(s)], [f"{n:08X}".encode() for n in range(6)])
    def test_sequence_wrap(self):
        p,s,_,m,_,_ = fixture()
        p.enable_keyboard()
        p.sequence = 0xFFFFFFFF
        m.update(1);p.tick_input()
        m.update(0);p.tick_input()
        self.assertIn(b" FFFFFFFF 01\n", input_packets(s)[-2])
        self.assertIn(b" 00000000 00\n", input_packets(s)[-1])
    def test_held_keys_have_bounded_heartbeat(self):
        p,s,_,m,c,_ = fixture()
        p.enable_keyboard()
        m.update(1);p.tick_input()
        n = len(input_packets(s))
        c.advance(.01);m.update(1);p.tick_input()
        self.assertEqual(len(input_packets(s)),n)
        c.advance(.025);m.update(1);p.tick_input()
        self.assertEqual(len(input_packets(s)),n+1)
    def test_gui_lease_expires_stops_and_never_revives(self):
        p,s,_,m,c,e = fixture()
        p.enable_keyboard()
        m.update(0x20);p.tick_input()
        c.advance(.151);p.tick_input()
        self.assertFalse(p.keyboard_enabled)
        self.assertIsNone(p.session)
        self.assertTrue(input_packets(s)[-1].endswith(b" 00\n"))
        n = len(s.writes)
        m.update(0x20);c.advance(.1);p.tick_input()
        self.assertEqual(len(s.writes),n)
        self.assertTrue(any(k=="keyboard" and "超时" in d["reason"] for k,d in e))
    def test_focus_clear_neutral_before_future_stale_update(self):
        p,s,_,m,_,_ = fixture()
        p.enable_keyboard()
        m.update(1);p.tick_input()
        m.clear();m.update(1);p.tick_input()
        self.assertFalse(p.keyboard_enabled)
        self.assertTrue(input_packets(s)[-1].endswith(b" 00\n"))
    def test_cancel_during_connect_only_sends_neutral(self):
        p,s,_,m,_,e = fixture()
        s.after_write = lambda data: m.clear() if data == b"PLAYER CONNECT\n" else None
        p.enable_keyboard()
        self.assertFalse(p.keyboard_enabled)
        self.assertEqual(len(input_packets(s)), 1)
        self.assertTrue(input_packets(s)[0].endswith(b" 00\n"))
        self.assertFalse(any(k=="keyboard" and d["enabled"] for k,d in e))
    def test_disconnect_stop_still_sends_final_neutral(self):
        p,s,_,m,_,_ = fixture()
        p.enable_keyboard();m.update(0x20);p.tick_input()
        p.stop.set();p.tick_input()
        self.assertFalse(p.keyboard_enabled)
        self.assertTrue(input_packets(s)[-1].endswith(b" 00\n"))
    def test_partial_write_is_fatal_no_release_append(self):
        p,s,_,m,_,_ = fixture()
        p.enable_keyboard()
        s.partial_write = 4
        m.update(1)
        with self.assertRaisesRegex(core.PanelError, "部分"):
            p.tick_input()
        before = len(s.writes)
        p.disable_keyboard("write failed")
        self.assertTrue(p.fatal)
        self.assertEqual(len(s.writes),before)
    def test_unknown_write_outcome_is_not_retried(self):
        p,s,l,m,_,_ = fixture()
        p.enable_keyboard();s.write_error=True;m.update(1)
        with self.assertRaisesRegex(core.PanelError, "未知"):
            p.tick_input()
        before=len(s.writes);p.disable_keyboard("failed")
        self.assertEqual(len(s.writes),before)
        self.assertIn("TX_OUTCOME_UNKNOWN", [k for k,_ in l.events])


class SchedulingTests(unittest.TestCase):
    def worker(self, mailbox):
        return kt.KeyboardWorker("unused", "unused", "unused", queue.Queue(), queue.Queue(), threading.Event(), mailbox)
    def test_diagnostic_rejected_while_streaming(self):
        p,s,_,m,_,_ = fixture();p.enable_keyboard()
        n=len(s.writes)
        with self.assertRaisesRegex(core.PanelError,"先释放"):
            self.worker(m).perform(p,"refresh")
        self.assertEqual(len(s.writes),n)
    def test_shield_once_while_heartbeat_continues_waiting(self):
        p,s,_,m,c,_ = fixture();p.enable_keyboard()
        m.update(0x09);p.tick_input()
        s.shield_delay=.25
        s.before_read=lambda:m.update(0x09)
        self.worker(m).perform(p,"shield_key")
        self.assertEqual(s.writes.count(b"PLAYER SHIELD\n"),1)
        self.assertGreaterEqual(len(input_packets(s)),8)
        self.assertTrue(p.keyboard_enabled)
    def test_focus_loss_during_shield_releases_without_heartbeat_revival(self):
        p,s,_,m,c,_ = fixture();p.enable_keyboard();m.update(1);p.tick_input()
        deadline=c()+.05
        def lose_focus():
            if c()>=deadline:m.clear()
        s.before_read=lose_focus;s.shield_delay=.15
        self.worker(m).perform(p,"shield_key")
        self.assertFalse(p.keyboard_enabled)
        self.assertTrue(input_packets(s)[-1].endswith(b" 00\n"))
    def test_shield_not_sent_without_capture(self):
        p,s,_,m,_,_ = fixture()
        with self.assertRaises(core.PanelError):self.worker(m).perform(p,"shield_key")
        self.assertEqual(s.writes,[])
    def test_stop_neutral_precedes_stop_command(self):
        p,s,_,m,_,_ = fixture();p.enable_keyboard();m.update(0x20);p.tick_input()
        self.worker(m).perform(p,"stop")
        idx=s.writes.index(b"ENEMY STOP\n")
        self.assertTrue(s.writes[idx-1].endswith(b" 00\n"))
        self.assertFalse(p.keyboard_enabled)
    def test_game_over_status_disarms_without_reconnecting(self):
        p,s,_,m,c,_ = fixture();p.enable_keyboard()
        s.status=ACTIVE.replace(b"GAME_OVER=0",b"GAME_OVER=1")
        c.advance(1.01);m.update(0);p.poll_game_if_due()
        self.assertFalse(p.keyboard_enabled)
        self.assertEqual(s.writes.count(b"PLAYER CONNECT\n"),1)
    def test_revocation_signals_disarm(self):
        for line in (b"ENEMY STOPPED\n",b"ENEMY EXITED\n",b"ENEMY RESTARTED\n",b"GAME OVER\n"):
            with self.subTest(line=line):
                p,s,_,_,_,_=fixture();p.enable_keyboard()
                n=len(input_packets(s));s.data.extend(line);p.read_lines()
                self.assertFalse(p.keyboard_enabled)
                self.assertEqual(len(input_packets(s)),n)
    def test_error_reply_disables_and_is_reported(self):
        p,s,_,_,_,_=fixture();p.enable_keyboard()
        s.data.extend(b"ERR INVALID_SESSION\n")
        with self.assertRaisesRegex(core.PanelError,"INVALID_SESSION"):p.read_lines()
        self.assertFalse(p.keyboard_enabled)
        self.assertTrue(input_packets(s)[-1].endswith(b" 00\n"))
    def test_no_status_poll_when_disabled(self):
        p,s,_,_,c,_=fixture();c.advance(100);p.poll_game_if_due()
        self.assertEqual(s.writes,[])
    def test_worker_preserves_identity_gate_before_import_serial(self):
        with tempfile.TemporaryDirectory() as td:
            worker=kt.KeyboardWorker(td,"NO_DEVICE",td,queue.Queue(),queue.Queue(),threading.Event(),kt.InputMailbox())
            with mock.patch.object(core,"verify_pair",side_effect=core.PanelError("bad SHA")),mock.patch.dict("sys.modules",serial=mock.Mock()) as mods:
                worker.run()
                mods["serial"].Serial.assert_not_called()
    def test_worker_connect_disconnect_sends_nothing_without_opt_in(self):
        p,s,_,m,_,_=fixture();stop=threading.Event()
        def open_port():
            self.assertFalse(s.dtr);self.assertFalse(s.rts)
            stop.set()
        s.open=open_port
        mod=mock.Mock();mod.Serial.return_value=s
        with tempfile.TemporaryDirectory() as td,mock.patch.object(core,"verify_pair",return_value={}),mock.patch.dict("sys.modules",serial=mod):
            worker=kt.KeyboardWorker(td,"NO_DEVICE",td,queue.Queue(),queue.Queue(),stop,m)
            worker.run()
        self.assertEqual(s.writes,[])
        self.assertTrue(s.closed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
