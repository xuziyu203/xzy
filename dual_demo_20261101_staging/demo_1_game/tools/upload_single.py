"""Single ELF asset upload (GHD3S/GHD4S) (one COM port; never loads bit/ELF, never resets, never writes Flash).
Protocol: unchanged tools/asset_upload.py (ASSET HELLO / CAPS / BEGIN / 128-byte packets / EOT). The firmware reads every stored
asset back from DDR with the hardware CRC reader before registering it, so a STORED reply now means 'transport CRC and DDR content
both match'. Steps:
  1. pair + payload SHA check (delivery_pairs.json, upload_manifest.json), no device opened
  2. identity: ASSET BUILD must answer ID / HUD / DDRCRC of the verified delivery_pairs record (build_id, abi) and VERIFY=1 (otherwise stop)
  3. upload IDs (default 1..33; --ids for a subset), one retry per ID after a failure (re-sends the whole ID)
  4. ASSET STATUS must show VALID_MASK=FFFFFFFE and VALID_MASK_HI with bits 0,1 (IDs 32,33) when all IDs were sent
Everything received is logged in board_logs/upload_<stamp>/ (rx.raw.bin, summary.json).
Usage: python -E -B tools/upload_single.py --port COM15 [--ids 11,22] [--check-only]"""
from pathlib import Path
import argparse, datetime, hashlib, json, re, sys, time
D = Path(__file__).resolve().parent; R = D.parent
sys.path.insert(0, str(D))
import asset_upload
import upload_assets
BUILD = None  # expected firmware BUILD: delivery_pairs.json candidate.build_id (set by verify_pair)


class Logged:
    """Serial wrapper: every received byte goes to rx.raw.bin."""
    def __init__(self, ser, path): self.ser = ser; self.raw = open(path, 'wb')
    def read(self, n=1):
        b = self.ser.read(n); self.raw.write(b); return b
    def write(self, b): return self.ser.write(b)
    def reset_input_buffer(self): self.ser.reset_input_buffer()
    def close(self): self.raw.close()


def query(ser, cmd, prefix, timeout=3.0):
    ser.write((cmd + '\r\n').encode()); end = time.monotonic() + timeout; buf = bytearray()
    while time.monotonic() < end:
        b = ser.read(1)
        if not b: continue
        if b == b'\n':
            line = buf.decode('ascii', 'replace').strip(); buf.clear()
            if line.startswith(prefix): return line
        elif b != b'\r': buf += b
    raise TimeoutError(f'no reply to {cmd!r} (expected {prefix!r})')


def verify_pair():
    """bit/ELF/map and every file the tools read must match delivery_pairs.json (no device opened)."""
    doc = json.loads((R / 'delivery_pairs.json').read_text(encoding='utf-8-sig')); c = doc['candidate']
    for k in ('bit', 'elf', 'map'):
        if hashlib.sha256((R / c[k]).read_bytes()).hexdigest() != c[k + '_sha256']: raise RuntimeError('pair SHA mismatch: ' + k)
    for name, h in doc.get('tool_dependencies', {}).items():
        if hashlib.sha256((R / name).read_bytes()).hexdigest() != h: raise RuntimeError('dependency SHA mismatch: ' + name)
    return c


def fields(line): return dict(re.findall(r'(\w+)=(\S+)', line))


EXPECT = None  # GHD8: expected HUD / DDRCRC from the verified delivery_pairs abi (no constants)


def check_identity(ser):
    global BUILD, EXPECT
    if BUILD is None or EXPECT is None:
        c = verify_pair(); BUILD = c['build_id']; EXPECT = {'HUD': int(c['abi']['hud_abi_0x1EC'], 16), 'DDRCRC': int(c['abi']['ddrcrc_abi_0x3E0'], 16)}
    v = fields(query(ser, 'ASSET BUILD', 'ASSET BUILD '))
    ok = v.get('ID') == BUILD and int(v.get('HUD', '0'), 16) == EXPECT['HUD'] and int(v.get('DDRCRC', '0'), 16) == EXPECT['DDRCRC'] and v.get('VERIFY') == '1'
    if not ok: raise RuntimeError(f"board identity mismatch: {v} (expected ID={BUILD} HUD={EXPECT['HUD']:08X} DDRCRC={EXPECT['DDRCRC']:08X} VERIFY=1). Load the bit + ELF of this package.")
    return v


def main(argv=None, ser_factory=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port'); ap.add_argument('--ids'); ap.add_argument('--timeout', type=float, default=3.0); ap.add_argument('--check-only', action='store_true')
    a = ap.parse_args(argv)
    pair = verify_pair()
    ids = sorted({int(x) for x in a.ids.split(',')}) if a.ids else list(range(1, 34))
    rows = upload_assets.payloads(set(ids))
    print(f"pair OK (BUILD {pair.get('build_id')}); IDs {','.join(str(i) for i, _ in rows)}; {sum(len(d) for _, d in rows)} bytes")
    if a.check_only: return 0
    if not a.port: ap.error('--port required')
    out = R / 'board_logs' / ('upload_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')); out.mkdir(parents=True)
    if ser_factory is None:
        import serial
        ser_factory = lambda: serial.Serial(a.port, 115200, timeout=0.05, write_timeout=a.timeout)
    summary = {'port': a.port, 'ids': ids, 'results': {}, 'pair': pair}
    raw = ser_factory(); ser = Logged(raw, out / 'rx.raw.bin')
    try:
        summary['identity'] = check_identity(ser); print('board:', summary['identity'])
        for rid, data in rows:
            t0 = time.monotonic()
            for attempt in (1, 2):
                try:
                    asset_upload.upload(ser, rid, data, a.timeout); summary['results'][rid] = {'ok': True, 'attempts': attempt, 'seconds': round(time.monotonic() - t0, 1)}; break
                except (RuntimeError, TimeoutError) as e:
                    summary['results'][rid] = {'ok': False, 'attempts': attempt, 'error': str(e)}; print(f'ID{rid} attempt {attempt}: {e}')
                    if attempt == 2: raise
                    time.sleep(0.5); ser.reset_input_buffer()
        st = query(ser, 'ASSET STATUS', 'ASSET STATUS '); summary['status'] = st; v = fields(st)
        if len(ids) == 33 and not (int(v.get('VALID_MASK', '0'), 16) == 0xFFFFFFFE and (int(v.get('VALID_MASK_HI', '0'), 16) & 3) == 3):
            raise RuntimeError('not every ID is registered: ' + st)
        print(st); print('Upload complete. The board shows the start menu once all 33 IDs are registered. Close this port.')
        summary['pass'] = True; return 0
    except (OSError, ValueError, RuntimeError, TimeoutError) as e:
        summary['pass'] = False; summary['stop'] = str(e); print('Upload stopped:', e, file=sys.stderr); return 1
    finally:
        ser.close(); raw.close(); (out / 'summary.json').write_text(json.dumps(summary, indent=1, default=str), encoding='utf-8'); print('log', out)


if __name__ == '__main__':
    raise SystemExit(main())
