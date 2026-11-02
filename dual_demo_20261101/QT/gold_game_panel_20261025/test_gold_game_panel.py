"""Behavior tests with fake serial only. No third-party dependencies."""
import hashlib
import importlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

import gold_game_panel as panel


class MemoryJournal:
    def __init__(self):
        self.rx, self.tx = bytearray(), bytearray()
        self.events = []
        self.observed_build = None
    def receive(self, data):
        self.rx.extend(data)
    def accepted(self, data):
        self.tx.extend(data)
    def event(self, kind, **kw):
        self.events.append((kind, kw))


class FakeSerial:
    def __init__(self, replies=(), initial=b"", partial_write=None, write_error=False):
        self.data = bytearray(initial)
        self.replies = list(replies)
        self.writes = []
        self.partial_write = partial_write
        self.write_error = write_error
        self.read_size = 4096
        self.closed = False
    @property
    def in_waiting(self):
        return len(self.data)
    def read(self, count):
        count = min(count, self.read_size)
        data = bytes(self.data[:count])
        del self.data[:count]
        if not data:
            time.sleep(0.0001)
        return data
    def write(self, data):
        self.writes.append(data)
        if self.write_error:
            raise OSError("write failed")
        if self.replies:
            self.data.extend(self.replies.pop(0))
        return len(data) if self.partial_write is None else self.partial_write
    def close(self):
        self.closed = True


def protocol(*args, **kwargs):
    s = FakeSerial(*args, **kwargs)
    journal = MemoryJournal()
    emitted = []
    p = panel.Protocol(s, journal, lambda k, d: emitted.append((k, d)), threading.Event())
    return p, s, journal, emitted


def joy(seq=1, x=13168, y=13440, valid=1, error=0, flags=1, epoch=0):
    w = [0] * 32
    w[18], w[19], w[20], w[22] = seq, epoch, seq, error
    w[28] = (x & 65535) | ((y & 65535) << 16)
    w[29], w[30] = flags, valid
    return "PLAYER JOY ABI=2 WORDS=" + " ".join(map(str, w)) + "\r\n"


ENEMY_IDLE = b"ENEMY STATUS STATE=IDLE ERROR=00000000 MOVING=0 GAME=0 GAME_OVER=0\r\n"
ASSETS = b"ASSET STATUS VALID_MASK=0FFFFFFE ACTIVE=0 ERROR=00000000\r\n"
STATUS = b"STATUS GAME_ONLY=1 ABI=474F0200 SAFE_IDLE=1 ERROR=00000000\r\n"
FLOW = b"ENEMY FLOW MODE=OFF\r\n"


class ParserTests(unittest.TestCase):
    def test_user_valid_xy_and_armed(self):
        r = panel.parse_line(joy(seq=122524, x=26672, y=13424, flags=65793).strip())
        self.assertEqual((r['x'], r['y'], r['armed'], r['latched']), (26672, 13424, 1, 0))
    def test_invalid_xy_never_reuses_historical_values(self):
        r = panel.parse_line(joy(valid=0).strip())
        self.assertEqual((r['x'], r['y']), (None, None))
    def test_signed_and_wrap_sequence(self):
        r = panel.parse_line(joy(seq=0, x=-3, y=-32768).strip())
        self.assertEqual((r['sequence'], r['x'], r['y']), (0, -3, -32768))
    def test_malformed_joy_rejected(self):
        for line in ('PLAYER JOY ABI=3 WORDS=0', 'PLAYER JOY ABI=2 WORDS=0', joy(flags=2).strip(), joy(valid=2).strip()):
            with self.subTest(line=line), self.assertRaises(panel.PanelError):
                panel.parse_line(line)
    def test_diag_cumulative_and_historical_slot(self):
        w = [0] * 96
        w[0], w[17], w[22], w[25] = 1, 22, 1, 15
        r = panel.parse_line('PLAYER JOY DIAG ABI=1 WORDS=' + ' '.join(map(str, w)))
        self.assertEqual(r['counts']['PAIR_TIMEOUT'], 22)
        self.assertEqual((r['first'], r['retry_first']), ('PAIR_TIMEOUT', 'NONE'))
    def test_zero_error_status_not_rejected(self):
        self.assertFalse(panel.protocol_rejection('STATUS ERROR=00000000 BUSY=0'))
    def test_error_forms(self):
        for line in ('ERR UNKNOWN_COMMAND', 'ENEMY BUSY', 'PLAYER SHIELD REJECTED', 'ERROR: failure'):
            self.assertTrue(panel.protocol_rejection(line))


class WireTests(unittest.TestCase):
    def test_import_never_imports_serial(self):
        result = subprocess.run([sys.executable, '-E', '-B', '-c',
                                 'import sys, gold_game_panel; assert "serial" not in sys.modules'],
                                cwd=Path(__file__).parent, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
    def test_exact_user_unknown_command_is_immediate(self):
        p, s, log, _ = protocol([b'ERR UNKNOWN_COMMAND\r\n'])
        with self.assertRaisesRegex(panel.PanelError, 'UNKNOWN_COMMAND'):
            p.transact(panel.COMMANDS['diag'])
        self.assertEqual(s.writes, [b'PLAYER JOY DIAG\n'])
        self.assertEqual(bytes(log.rx), b'ERR UNKNOWN_COMMAND\r\n')
        self.assertNotIn('COMMAND_TIMEOUT', [e[0] for e in log.events])
    def test_fragmented_status_and_raw_log(self):
        p, s, log, _ = protocol([STATUS])
        s.read_size = 3
        reply = p.transact(panel.COMMANDS['status'])
        self.assertIn('SAFE_IDLE=1', reply)
        self.assertEqual(bytes(log.rx), STATUS)
        self.assertEqual(bytes(log.tx), b'STATUS\n')
    def test_preexisting_complete_line_logged_not_used_as_reply(self):
        p, s, log, _ = protocol([STATUS], initial=b'EVENT OLD\r\n')
        p.transact(panel.COMMANDS['status'])
        self.assertTrue(bytes(log.rx).startswith(b'EVENT OLD\r\n'))
        self.assertEqual(s.writes, [b'STATUS\n'])
    def test_preexisting_partial_line_blocks_send(self):
        p, s, _, _ = protocol(initial=b'partial')
        with self.assertRaisesRegex(panel.PanelError, '未结束'):
            p.transact(panel.COMMANDS['status'])
        self.assertEqual(s.writes, [])
    def test_new_partial_rx_kept_on_timeout(self):
        p, s, _, _ = protocol([b'PARTIAL'])
        with self.assertRaisesRegex(panel.PanelError, '没有收到完整回包'):
            p.transact(panel.Command('STATUS', ('STATUS ',), .005))
        self.assertEqual(p.partial, b'PARTIAL')
        self.assertTrue(p.fatal)
        self.assertEqual(len(s.writes), 1)
    def test_wrong_prefix_timeout_not_silence(self):
        p, _, _, _ = protocol([b'OTHER MESSAGE\r\n'])
        with self.assertRaisesRegex(panel.PanelError, '收到回包但不符合'):
            p.transact(panel.Command('STATUS', ('STATUS ',), .005))
    def test_no_write_after_timeout(self):
        p, s, _, _ = protocol([])
        with self.assertRaises(panel.PanelError):
            p.transact(panel.Command('STATUS', ('STATUS ',), .005))
        with self.assertRaises(panel.PanelError):
            p.transact(panel.COMMANDS['status'])
        self.assertEqual(s.writes, [b'STATUS\n'])
    def test_wrong_build_stops_before_more_commands(self):
        p, s, _, _ = protocol([b'BUILD: OLD_FW\r\n' + STATUS])
        with self.assertRaises(panel.BuildMismatch):
            p.transact(panel.COMMANDS['status'])
        self.assertTrue(p.fatal)
    def test_correct_build_observed(self):
        p, _, log, _ = protocol([f'BUILD: {panel.FW}\r\n'.encode() + STATUS])
        p.transact(panel.COMMANDS['status'])
        self.assertEqual(log.observed_build, panel.FW)
    def test_partial_write_not_retried(self):
        p, s, log, _ = protocol(partial_write=3)
        with self.assertRaisesRegex(panel.PanelError, '部分命令'):
            p.transact(panel.COMMANDS['status'])
        self.assertEqual(bytes(log.tx), b'STA')
        self.assertTrue(p.fatal)
        self.assertEqual(len(s.writes), 1)
    def test_write_exception_unknown_outcome(self):
        p, s, log, _ = protocol(write_error=True)
        with self.assertRaisesRegex(panel.PanelError, '前缀未知'):
            p.transact(panel.COMMANDS['status'])
        self.assertEqual(bytes(log.tx), b'')
        self.assertIn('TX_OUTCOME_UNKNOWN', [e[0] for e in log.events])
        self.assertTrue(p.fatal)
    def test_no_control_chars_in_command(self):
        for text in ('STATUS\nENEMY MOVE', '\x03STATUS', '状态'):
            p, s, _, _ = protocol()
            with self.assertRaises(panel.PanelError):
                p.transact(panel.Command(text, ('STATUS ',)))
            self.assertEqual(s.writes, [])
    def test_stop_waits_for_complete_notification(self):
        p, _, _, _ = protocol([b'ENEMY STOP ACCEPTED\r\nENEMY STOPPED\r\n'])
        self.assertEqual(p.transact(panel.COMMANDS['stop']), 'ENEMY STOPPED')
    def test_stop_accepted_alone_not_complete(self):
        p, _, _, _ = protocol([b'ENEMY STOP ACCEPTED\r\n'])
        with self.assertRaises(panel.PanelError):
            p.transact(panel.Command('ENEMY STOP', ('ENEMY STOPPED',), .005))


class ActionTests(unittest.TestCase):
    def test_refresh_stops_at_first_unknown(self):
        p, s, _, _ = protocol([STATUS, b'ERR UNKNOWN_COMMAND\r\n'])
        with self.assertRaises(panel.PanelError):
            panel.run_action(p, 'refresh')
        self.assertEqual(s.writes, [b'STATUS\n', b'ASSET STATUS\n'])
    def test_calibrate_uses_fresh_values_not_historic_constants(self):
        p, s, _, _ = protocol([ENEMY_IDLE, joy(seq=11, x=14000, y=13000).encode(),
                               joy(seq=12, x=14016, y=13000).encode(), b'PLAYER JOY OK\r\n'])
        panel.run_action(p, 'center', 3)
        self.assertEqual(s.writes[-1], b'PLAYER JOY CENTER 14016 13000 3\n')
    def test_calibrate_rejects_invalid_extreme_moving_stale_or_fault(self):
        bad_pairs = [(joy(valid=0), joy(seq=2)), (joy(x=16), joy(seq=2, x=16)),
                     (joy(x=14000), joy(seq=2, x=15500)), (joy(seq=8), joy(seq=8)),
                     (joy(), joy(seq=2, error=1)), (joy(epoch=1), joy(seq=2, epoch=2))]
        for a, b in bad_pairs:
            with self.subTest(a=a, b=b):
                p, s, _, _ = protocol([ENEMY_IDLE, a.encode(), b.encode()])
                with self.assertRaises(panel.PanelError):
                    panel.run_action(p, 'center')
                self.assertFalse(any(b'CENTER' in w for w in s.writes))
    def test_calibration_sequence_wrap_is_supported(self):
        p, s, _, _ = protocol([ENEMY_IDLE, joy(seq=0xFFFFFFFF).encode(), joy(seq=0).encode(), b'PLAYER JOY OK\r\n'])
        panel.run_action(p, 'center')
        self.assertIn(b'CENTER', s.writes[-1])
    def test_moving_game_blocks_calibration(self):
        p, s, _, _ = protocol([ENEMY_IDLE.replace(b'MOVING=0', b'MOVING=1')])
        with self.assertRaises(panel.PanelError):
            panel.run_action(p, 'center')
        self.assertEqual(len(s.writes), 1)
    def test_prepare_full_order_no_automatic_start(self):
        p, s, _, _ = protocol([STATUS, ASSETS, ENEMY_IDLE, FLOW, b'ENEMY FLOW LIGHT_PRESSURE\r\n'])
        panel.run_action(p, 'prepare')
        self.assertEqual(s.writes, [b'STATUS\n', b'ASSET STATUS\n', b'ENEMY STATUS\n', b'ENEMY FLOW\n', b'ENEMY FLOW LIGHT_PRESSURE\n'])
    def test_missing_asset_blocks_prepare(self):
        p, s, _, _ = protocol([STATUS, ASSETS.replace(b'0FFFFFFE', b'00000000')])
        with self.assertRaises(panel.PanelError):
            panel.run_action(p, 'prepare')
        self.assertEqual(len(s.writes), 2)
    def test_safe_idle_missing_blocks_prepare(self):
        p, s, _, _ = protocol([STATUS.replace(b'SAFE_IDLE=1', b'SAFE_IDLE=0')])
        with self.assertRaises(panel.PanelError):
            panel.run_action(p, 'prepare')
        self.assertEqual(len(s.writes), 1)
    def test_move_waits_shown_and_never_auto_shields(self):
        p, s, _, _ = protocol([ASSETS, ENEMY_IDLE, FLOW, b'ENEMY PREPARING\r\nENEMY SHOWN\r\n'])
        p.flow_prepared = True
        panel.run_action(p, 'move')
        self.assertEqual(s.writes[-1], b'ENEMY MOVE\n')
        self.assertFalse(any(b'SHIELD' in w for w in s.writes))
    def test_move_without_configuration_does_not_start_default_single(self):
        p, s, _, _ = protocol([ASSETS, ENEMY_IDLE])
        with self.assertRaisesRegex(panel.PanelError, '先点击'):
            panel.run_action(p, 'move')
        self.assertNotIn(b'ENEMY MOVE\n', s.writes)
    def test_exit_explicit_then_status(self):
        p, s, _, _ = protocol([b'ENEMY EXIT ACCEPTED\r\nENEMY RESTORING\r\nGAME IDLE_CONFIRMED\r\nENEMY EXITED\r\n', STATUS])
        panel.run_action(p, 'exit')
        self.assertEqual(s.writes, [b'ENEMY EXIT\n', b'STATUS\n'])


class IdentityAndWorkerTests(unittest.TestCase):
    def test_manifest_mismatch_before_serial_import(self):
        with tempfile.TemporaryDirectory() as td:
            Path(td, 'delivery_pairs.json').write_text('{}')
            with self.assertRaisesRegex(panel.PanelError, 'SHA'):
                panel.verify_pair(td)
    def test_pair_hash_gate_with_synthetic_files(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'pair').mkdir()
            expected, rows = {}, {}
            for kind in ('bit', 'elf', 'map'):
                relative = 'pair/test.' + kind
                data = kind.encode()
                (root / relative).write_bytes(data)
                digest = hashlib.sha256(data).hexdigest()
                expected[kind] = (relative, digest, len(data))
                rows[kind] = dict(path=relative, sha256=digest, bytes=len(data))
            doc = dict(candidate=dict(firmware_id=panel.FW, joy_status_abi=2, joy_diag_abi=1, **rows))
            (root / 'delivery_pairs.json').write_text(json.dumps(doc))
            digest = panel.sha256(root / 'delivery_pairs.json')
            with mock.patch.object(panel, 'MANIFEST_SHA', digest), mock.patch.object(panel, 'PAIR', expected):
                self.assertEqual(panel.verify_pair(root)['files']['bit']['bytes'], 3)
                (root / expected['elf'][0]).write_bytes(b'bad')
                with self.assertRaises(panel.PanelError):
                    panel.verify_pair(root)
    def test_worker_open_close_has_no_auto_tx_and_dtr_set_before_open(self):
        import queue
        emitted, requests = queue.Queue(), queue.Queue()
        stop = threading.Event()
        s = FakeSerial()
        def open_port():
            self.assertFalse(s.dtr)
            self.assertFalse(s.rts)
            self.assertEqual(s.port, 'COM15')
            stop.set()
        s.open = open_port
        serial_mock = mock.Mock()
        serial_mock.Serial.return_value = s
        with tempfile.TemporaryDirectory() as td, mock.patch.object(panel, 'verify_pair', return_value={}), mock.patch.dict(sys.modules, serial=serial_mock):
            worker = panel.Worker(td, 'COM15', td, requests, emitted, stop)
            worker.run()
            self.assertTrue(s.closed)
            self.assertEqual(s.writes, [])
            folder = next(Path(td).glob('panel_*'))
            self.assertEqual((folder/'tx.accepted.bin').read_bytes(), b'')
            self.assertTrue((folder/'partial_rx.bin').exists())
    def test_journal_lossless_bytes_and_close(self):
        with tempfile.TemporaryDirectory() as td:
            j = panel.Journal(td, {}, 'TEST_ONLY')
            j.receive(b'\x00\xffERR\r\n')
            j.accepted(b'STATUS\n')
            j.close(b'abc')
            self.assertEqual((j.path/'rx.raw.bin').read_bytes(), b'\x00\xffERR\r\n')
            self.assertEqual((j.path/'partial_rx.bin').read_bytes(), b'abc')
            events = [json.loads(x) for x in (j.path/'events.jsonl').read_text().splitlines()]
            self.assertFalse(events[-1]['automatic_exit_sent'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
