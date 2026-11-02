"""Portable loader for the unchanged SCOPE_BLOCKCOPY_20261101 basic pair.

Default performs an offline identity/dependency check and writes commands only.
--execute programs FPGA SRAM over JTAG, then loads its paired ELF through OpenOCD.
It does not scan serial ports, write flash, attach to existing servers, or kill processes.
"""
from pathlib import Path
import argparse
import datetime
import os
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
from delivery_common import contained, efinity_env, run_logged, sha256, write_json

PAIR = {'basic_demo.bit': 'ca722493e2abed677c543dec61db9c2c6e95ae29ca39895ee1f3978f2a126998',
        'basic_demo.elf': '4a481583cb33e41a5478d10a9012aa53b44ffca055950293527f30c10f2efb3a'}


def programming_complete(text):
    return ('Device ID read from JTAG: 0x10660A79' in text and
            'finished with JTAG programming' in text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--pair-dir', type=Path, default=Path('pair'), help='Relative to project root; contains basic_demo.bit/.elf')
    parser.add_argument('--efinity-root', type=Path, default=os.environ.get('EFINITY_HOME'))
    parser.add_argument('--riscv-ide-root', type=Path, default=os.environ.get('RISCV_IDE_ROOT'))
    parser.add_argument('--report-dir', type=Path, help='Fresh directory; defaults below project/_logs')
    parser.add_argument('--execute', action='store_true', help='Actually configure FPGA SRAM and load ELF')
    args = parser.parse_args()
    if args.efinity_root is None or args.riscv_ide_root is None:
        parser.error('Set EFINITY_HOME and RISCV_IDE_ROOT, or provide both tool-root options')
    root, efinity, ide = args.project_root.resolve(), args.efinity_root.resolve(), args.riscv_ide_root.resolve()
    pair = contained(root, root / args.pair_dir)
    app = root / 'embedded_sw/riscv_soc/software/standalone/apbTest'
    soc = root / 'embedded_sw/riscv_soc'
    cfg = soc / 'bsp/efinix/EfxSapphireSoc/openocd'
    python = efinity / 'python311/bin/python.exe'
    programmer = efinity / 'pgm/bin/efx_pgm/ftdi_program.py'
    openocd = ide / 'openocd/bin/openocd.exe'
    required = [pair / name for name in PAIR] + [soc / 'cpu0.yaml', cfg / 'ftdi_ti.cfg', cfg / 'debug_ti.cfg', python, programmer, openocd]
    for path in required:
        if not path.is_file(): raise FileNotFoundError(path)
    if not app.is_dir(): raise FileNotFoundError(app)
    for name, expected in PAIR.items():
        if sha256(pair / name) != expected:
            raise ValueError('Pair SHA-256 mismatch: ' + name)
    elf = (pair / 'basic_demo.elf').read_bytes()
    if elf[:7] != b'\x7fELF\x01\x01\x01' or int.from_bytes(elf[24:28], 'little') != 0xF9000000:
        raise ValueError('Expected little-endian ELF32 entry 0xF9000000')
    # debug_ti.cfg searches up from apbTest. Confirm the exact local cpu0.yaml wins.
    found = next((app.joinpath(*(['..'] * n), 'cpu0.yaml').resolve() for n in range(1, 5)
                  if app.joinpath(*(['..'] * n), 'cpu0.yaml').is_file()), None)
    if found != (soc / 'cpu0.yaml').resolve():
        raise ValueError('OpenOCD cpu0.yaml lookup would not select this project')
    elf_path = (pair / 'basic_demo.elf').as_posix()
    if any(c in elf_path for c in '{}\r\n'):
        raise ValueError('ELF path contains characters unsupported by the Tcl brace literal')
    commands = [dict(stage='program_bit', args=[str(python), str(programmer), str(pair / 'basic_demo.bit'), '-m', 'jtag'], cwd=str(programmer.parent)),
                dict(stage='load_elf', args=[str(openocd), '-c', 'gdb_port disabled; tcl_port disabled; telnet_port disabled',
                     '-f', str(cfg / 'ftdi_ti.cfg'), '-f', str(cfg / 'debug_ti.cfg'),
                     '-c', 'reset halt; load_image {' + elf_path + '}; resume 0xF9000000; shutdown'], cwd=str(app))]
    report_dir = args.report_dir.resolve() if args.report_dir else root / '_logs' / ('basic_load_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_') + uuid.uuid4().hex[:6])
    if report_dir.exists(): raise FileExistsError('Preserve previous loader log: ' + str(report_dir))
    report_dir.mkdir(parents=True)
    record = dict(version='SCOPE_BLOCKCOPY_20261101', status='PLAN_ONLY', device_operation=False, commands=commands,
                  project_root=str(root), pair_sha256=PAIR,
                  dependencies=[dict(path=str(p), sha256=sha256(p), bytes=p.stat().st_size) for p in required],
                  note='Local hashes identify files, not board RAM readback. No serial readback or functional verification is performed here.', phases=[])
    write_json(report_dir / 'load_record.json', record)
    if not args.execute:
        print(str(report_dir / 'load_record.json')); return 0
    env = efinity_env(efinity)
    record.update(status='RUNNING', device_operation=True)
    write_json(report_dir / 'load_record.json', record)
    for row in commands:
        if any(sha256(pair / name) != expected for name, expected in PAIR.items()):
            record.update(status='FAIL', error='Pair changed after validation')
            write_json(report_dir / 'load_record.json', record)
            return 1
        result = run_logged(row['args'], row['cwd'], env, report_dir / (row['stage'] + '.log'), timeout=300)
        if row['stage'] == 'program_bit':
            text = (report_dir / 'program_bit.log').read_text(encoding='utf-8', errors='replace')
            result['expected_device_and_completion_marker'] = programming_complete(text)
            if result['returncode'] == 0 and not result['expected_device_and_completion_marker']:
                result.update(returncode=1, error='Vendor tool exited zero without expected device ID and JTAG completion marker')
        record['phases'].append(dict(stage=row['stage'], **result))
        if result['returncode']:
            record['status'] = 'FAIL'
            write_json(report_dir / 'load_record.json', record)
            return 1
        write_json(report_dir / 'load_record.json', record)
    record['status'] = 'LOADED_NOT_FUNCTIONALLY_VERIFIED'
    write_json(report_dir / 'load_record.json', record)
    print(str(report_dir / 'load_record.json'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
