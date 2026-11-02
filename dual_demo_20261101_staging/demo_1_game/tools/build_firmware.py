"""Build either demo with a fresh OBJDIR. Never runs a board operation."""
from pathlib import Path
import argparse
import datetime
import os
import re
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
from delivery_common import contained, hashes, run_logged, sha256, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--riscv-ide-root', type=Path, default=os.environ.get('RISCV_IDE_ROOT'))
    parser.add_argument('--objdir', default='build_delivery_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_') + uuid.uuid4().hex[:6])
    parser.add_argument('--dry-run', action='store_true', help='Write a fresh build plan and hashes without invoking make')
    args = parser.parse_args()
    if args.riscv_ide_root is None:
        parser.error('Set RISCV_IDE_ROOT or --riscv-ide-root to the RISC-V IDE 2026.1 installation')
    if not re.fullmatch(r'build_[A-Za-z0-9_]+', args.objdir):
        parser.error('--objdir must be a simple fresh name beginning build_')
    root, ide = args.project_root.resolve(), args.riscv_ide_root.resolve()
    app = root / 'embedded_sw/riscv_soc/software/standalone/apbTest'
    soc = root / 'embedded_sw/riscv_soc'
    make, shell, gcc = [ide / p for p in ['build_tools/bin/make.exe', 'build_tools/bin/sh.exe', 'toolchain/bin/riscv-none-elf-gcc.exe']]
    for path in [app / 'makefile', soc / 'bsp/efinix/EfxSapphireSoc/linker/default.ld', make, shell, gcc]:
        if not path.is_file():
            raise FileNotFoundError(path)
    # The existing makefiles do not quote the RISCV_BIN prefix. Fail clearly instead
    # of accidentally splitting a tool path. Project paths themselves may contain spaces.
    if any(c.isspace() for c in ide.as_posix()):
        parser.error('The existing BSP makefiles require a RISC-V IDE installation path without spaces')
    out = contained(root, app / args.objdir)
    if out.exists():
        raise FileExistsError('Fresh OBJDIR required; preserved existing output: ' + str(out))
    directories = [app / 'src', app.parent / 'common', app.parent / 'include', app.parent / 'driver', soc / 'bsp/efinix/EfxSapphireSoc']
    def source_hashes():
        paths = [app / 'makefile']
        for directory in directories:
            if not directory.is_dir():
                # The shared makefile includes standalone/include even when all
                # active BSP headers come from driver/. An unused -I may be absent.
                if directory.name == 'include' and directory.parent == app.parent:
                    continue
                raise FileNotFoundError(directory)
            paths += [p for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
        return hashes(root, paths)
    before = source_hashes()
    out.mkdir(parents=True)
    write_json(out / 'source_sha256_before.json', before)
    env = os.environ.copy()
    # Do not inherit IDE launch variables or make flags that can change the recipe.
    for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'CFLAGS_ARGS', 'VARIANT', 'SPINAL_SIM',
                'CFLAGS', 'CXXFLAGS', 'ASFLAGS', 'LDFLAGS', 'LIBS', 'INC'):
        env.pop(key, None)
    env['PATH'] = os.pathsep.join(str(ide / p) for p in ['build_tools/bin', 'toolchain/bin']) + os.pathsep + env.get('PATH', '')
    command = [str(make), 'DEBUG=no', 'BENCH=no', 'BSP=efinix/EfxSapphireSoc',
               'OBJDIR=' + args.objdir, 'RISCV_BIN=' + (ide / 'toolchain/bin/riscv-none-elf-').as_posix(),
               'SHELL=' + shell.as_posix(), args.objdir + '/apbTest.elf']
    record = dict(project_root=str(root), recipe='DEBUG=no BENCH=no BSP=efinix/EfxSapphireSoc; default variant',
                  args=command, cwd=str(app), status='PLAN_ONLY' if args.dry_run else 'RUNNING', device_operation=False,
                  source_files=len(before), tools={p.name: dict(path=str(p), sha256=sha256(p)) for p in [make, shell, gcc]})
    write_json(out / 'build_record.json', record)
    if args.dry_run:
        record['source_drift'] = []
        write_json(out / 'source_sha256_after.json', source_hashes())
        write_json(out / 'build_record.json', record)
        print(str(out / 'build_record.json'))
        return 0
    record['compiler_version'] = subprocess.check_output([str(gcc), '--version'], env=env, text=True).splitlines()[0]
    record['make_version'] = subprocess.check_output([str(make), '--version'], env=env, text=True).splitlines()[0]
    run = run_logged(command, app, env, out / 'build.log', timeout=1800)
    record.update(run)
    after = source_hashes()
    write_json(out / 'source_sha256_after.json', after)
    record['source_drift'] = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    record['artifacts'] = {p.name: dict(bytes=p.stat().st_size, sha256=sha256(p))
                           for p in [out / 'apbTest.elf', out / 'apbTest.map'] if p.is_file()}
    ok = run['returncode'] == 0 and not record['source_drift'] and 'apbTest.elf' in record['artifacts']
    record['status'] = 'PASS' if ok else 'FAIL'
    write_json(out / 'build_record.json', record)
    print(str(out / 'build_record.json'))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
