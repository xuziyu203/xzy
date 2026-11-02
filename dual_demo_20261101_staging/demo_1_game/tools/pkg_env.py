# GHD7 LT3 copy (demo_count_20261031/scripts/make_load_tool_lt3.py, from LT2 pool850_ram144_20261031/board/load_tool; identity from the verified package record) - LT2 header follows: GHD6 LT2 copy (scripts/make_load_tool_g6.py) of gold_hud_dualelf_20261030/board/load_tool/pkg_env.py; additions: 144 KiB RAM gates.
"""Package constants for the load tool, from <package>/delivery_pairs.json (bit/ELF SHA verified here)."""
from pathlib import Path
import hashlib, json, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from portable_runtime import TC


def verify_sums(pkg):
    """LT3: every entry of SHA256SUMS.txt must match (the identity expectation is read from a verified delivery_pairs.json)."""
    rows = [l.split(' *', 1) for l in (pkg / 'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines() if l.strip()]
    names = {n for _, n in rows}
    if 'delivery_pairs.json' not in names: raise SystemExit('SHA256SUMS.txt does not cover delivery_pairs.json')
    bad = [n for h, n in rows if hashlib.sha256((pkg / n).read_bytes()).hexdigest() != h]
    if bad: raise SystemExit(f'package SHA256SUMS mismatch: {bad[:5]}')
    return len(rows)


def load(pkg):
    pkg = Path(pkg).resolve(); nsum = verify_sums(pkg); c = json.loads((pkg / 'delivery_pairs.json').read_text(encoding='utf-8-sig'))['candidate']
    env = {'pkg': pkg, 'build_id': c['build_id'], 'bit': pkg / c['bit'], 'bit_sha256': c['bit_sha256'], 'elf': pkg / c['elf'], 'elf_sha256': c['elf_sha256']}
    for k in ('bit', 'elf'):
        h = hashlib.sha256(env[k].read_bytes()).hexdigest()
        if h != env[k + '_sha256']: raise SystemExit(f'{k} SHA mismatch: {h} != {env[k + "_sha256"]}')
    a = c.get('abi', {})
    if not c.get('build_id') or 'hud_abi_0x1EC' not in a or 'ddrcrc_abi_0x3E0' not in a: raise SystemExit('package record lacks build_id / abi.hud_abi_0x1EC / abi.ddrcrc_abi_0x3E0')
    env['identity'] = {'ID': c['build_id'], 'HUD': int(a['hud_abi_0x1EC'], 16), 'DDRCRC': int(a['ddrcrc_abi_0x3E0'], 16), 'VERIFY': '1'}
    env['sums_verified'] = nsum
    ram_gate(env, c)
    return env


def identity_ok(env, reply):
    """LT3: (ok, detail) for an 'ASSET BUILD ID=.. HUD=.. DDRCRC=.. VERIFY=..' reply against env['identity'] (from the package record)."""
    if not reply or not reply.startswith('ASSET BUILD '): return False, {'reply': reply}
    f = dict(t.split('=', 1) for t in reply[len('ASSET BUILD '):].split() if '=' in t); e = env['identity']
    try: hud, crc = int(f.get('HUD', ''), 16), int(f.get('DDRCRC', ''), 16)
    except ValueError: return False, {'reply': reply, 'parse': 'HUD/DDRCRC not hex'}
    chk = {'ID': f.get('ID') == e['ID'], 'HUD': hud == e['HUD'], 'DDRCRC': crc == e['DDRCRC'], 'VERIFY': f.get('VERIFY') == e['VERIFY']}
    return all(chk.values()), {'reply': reply, 'checks': chk, 'expected': {'ID': e['ID'], 'HUD': f"{e['HUD']:08X}", 'DDRCRC': f"{e['DDRCRC']:08X}", 'VERIFY': e['VERIFY']}}


def ram_gate(env, c):
    """GHD6: static 144 KiB layout gate (no device): ELF LOAD segments inside [base, bootloader), stack and reserve."""
    r = c['ram']; base, boot, end = int(r['base'], 16), int(r['bootloader_start'], 16), int(r['end'], 16)
    env['ram'] = r; env['bootloader_expected'] = env['pkg'] / r['bootloader_region_file']
    b = env['bootloader_expected'].read_bytes()
    if len(b) != end - boot or hashlib.sha256(b).hexdigest() != r['bootloader_region_sha256']: raise SystemExit('bootloader region file mismatch')
    ph = subprocess.run([TC + 'readelf.exe', '-lW', str(env['elf'])], capture_output=True, text=True, check=True).stdout
    segs = [(int(m[0], 16), int(m[1], 16)) for m in __import__('re').findall(r'LOAD\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)', ph)]
    if not segs or any(v < base or v + n > boot for v, n in segs): raise SystemExit(f'ELF LOAD segment outside [{base:#x}, {boot:#x}): {segs}')
    nm = subprocess.run([TC + 'nm.exe', str(env['elf'])], capture_output=True, text=True, check=True).stdout
    sym = {p[2]: int(p[0], 16) for p in (l.split() for l in nm.splitlines()) if len(p) == 3}
    if sym['_sp'] + r['reserve_min'] > boot or sym['__stack_size'] != r['stack'] or sym['_sp'] - sym['_end'] < r['stack']:
        raise SystemExit(f"stack / reserve gate failed: _sp {sym['_sp']:#x} _end {sym['_end']:#x} stack {sym['__stack_size']}")
    env['ram_gate'] = {'segments': [(hex(v), n) for v, n in segs], '_sp': hex(sym['_sp']), 'gap': boot - sym['_sp'], 'stack': sym['__stack_size']}
    # LT2: the fingerprint compares only the immutable bootloader code [code_start, code_end) (vendor bootloader_144K .init/.text;
    # its .data/.bss/stack in the same 1 KiB are written while it runs between configuration and the JTAG halt)
    cs, ce = (int(x, 16) for x in r['bootloader_code_range'])
    if not (boot <= cs < ce <= end) or not any(b[cs - boot:ce - boot]): raise SystemExit(f'bootloader code range invalid: {cs:#x}..{ce:#x}')
    env['bootloader_code_range'] = (cs, ce)


def fingerprint(env, got):
    """LT2: compare the read-back bootloader region with the package's expected bytes. Pass = same length and the immutable code range
    equal; every differing word of the full region is reported (a 160 KiB bit reads zeros over the 144K code range -> fail)."""
    exp = env['bootloader_expected'].read_bytes(); boot = int(env['ram']['bootloader_start'], 16); cs, ce = env['bootloader_code_range']
    n = min(len(got), len(exp)) // 4 * 4
    diff = [(hex(boot + i), got[i:i + 4][::-1].hex(), exp[i:i + 4][::-1].hex()) for i in range(0, n, 4) if got[i:i + 4] != exp[i:i + 4]]
    code_ok = len(got) == len(exp) and got[cs - boot:ce - boot] == exp[cs - boot:ce - boot]
    return {'ok': code_ok, 'code_range': [hex(cs), hex(ce)], 'code_equal': code_ok, 'full_region_equal': got == exp, 'bytes': len(got),
            'differing_words': diff, 'differing_words_in_code': [d for d in diff if cs <= int(d[0], 16) < ce]}


def expected_image(env, out_dir):
    out = Path(out_dir) / 'expected_image.bin'
    subprocess.run([TC + 'objcopy.exe', '-O', 'binary', str(env['elf']), str(out)], check=True)
    return out
