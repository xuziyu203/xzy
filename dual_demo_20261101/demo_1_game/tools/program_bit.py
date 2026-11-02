"""Portable LT3 programmer draft. This script programs hardware when executed.

Usage: python program_bit.py PACKAGE LOG_DIR
Requires the packaged bit/ELF/hash/RAM checks to pass before device access.
Retains LT3 program and USER_MODE markers for load_session_pkg.py.
"""
from pathlib import Path
import argparse
import datetime
import json
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pkg_env
import portable_runtime as runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('log_dir', nargs='?', type=Path)
    parser.add_argument('--check-only', action='store_true', help='Verify package and installed tool paths without any device access')
    args = parser.parse_args()
    package = pkg_env.load(args.package)
    if args.check_only:
        for path in (runtime.EFINITY / 'python311/bin/python.exe', runtime.EFINITY / 'pgm/bin/efx_pgm/ftdi_program.py',
                     Path(__file__).resolve().parent / 'read_fpga_status.py'):
            if not path.is_file(): raise SystemExit(f'Missing programmer dependency: {path}')
        print('PROGRAM_CHECK_ONLY PASS', package['build_id'], package['sums_verified'], 'files; no device access')
        return 0
    if args.log_dir is None: parser.error('log_dir is required for hardware programming')
    output = args.log_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any((output / name).exists() for name in ('step2_program_bit.log', 'step2b_status.log')):
        raise SystemExit('Refusing to overwrite a previous programming session')
    processes = subprocess.run(['tasklist'], capture_output=True, text=True, check=True, timeout=10).stdout.lower()
    if 'openocd.exe' in processes or 'gdb.exe' in processes:
        raise SystemExit('A debugger is running; do not share JTAG')
    python = runtime.EFINITY / 'python311/bin/python.exe'
    cwd = runtime.EFINITY / 'pgm/bin/efx_pgm'
    programmer = cwd / 'ftdi_program.py'
    status = Path(__file__).resolve().parent / 'read_fpga_status.py'
    for path in (python, programmer, status):
        if not path.is_file():
            raise SystemExit(f'Missing programmer dependency: {path}')
    env = runtime.efinity_environment()
    command = [str(python), str(programmer), str(package['bit']), '-m', 'jtag']
    (output / 'program_command.json').write_text(json.dumps({'command': command, 'cwd': str(cwd)}, indent=2), encoding='utf-8')
    with (output / 'step2_program_bit.log').open('w', encoding='utf-8') as f:
        f.write(f'START {datetime.datetime.now().isoformat()}\nBIT_SHA256 {package["bit_sha256"]}\n')
        f.flush()
        run = subprocess.run(command, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=120,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        f.write(f'STEP2_EXIT={run.returncode}\n')
    if run.returncode != 0:
        raise SystemExit('Programming failed; ELF load is blocked')
    with (output / 'step2b_status.log').open('w', encoding='utf-8') as f:
        run = subprocess.run([str(python), str(status)], cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT,
                             timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        f.write(f'STEP2B_EXIT={run.returncode}\n')
    text = (output / 'step2_program_bit.log').read_text(encoding='utf-8', errors='replace')
    state = (output / 'step2b_status.log').read_text(encoding='utf-8', errors='replace')
    ok = ('Device ID read from JTAG: 0x10660A79' in text and 'finished with JTAG programming' in text and
          'STEP2_EXIT=0' in text and 'PROG_STATE=USER_MODE' in state and 'STEP2B_EXIT=0' in state)
    print('PROGRAM', 'PASS' if ok else 'FAIL', flush=True)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
