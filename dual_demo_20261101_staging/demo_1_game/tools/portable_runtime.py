"""Portable support for the LT3 game loader; no board access at import time.

Package root retains embedded_sw/riscv_soc plus pair/ and delivery_pairs.json.
Install paths are external tool dependencies; no source file uses the old workspace.
"""
from pathlib import Path
import json
import os
import socket

HERE = Path(__file__).resolve().parent
CONFIG = HERE / 'tool_paths.json'
settings = json.loads(CONFIG.read_text(encoding='utf-8-sig')) if CONFIG.exists() else {}
IDE = Path(os.environ.get('RISCV_IDE_ROOT') or settings.get('riscv_ide_root', 'C:/Efinity/efinity-riscv-ide-2026.1')).resolve()
EFINITY = Path(os.environ.get('EFINITY_ROOT') or settings.get('efinity_root', 'C:/Efinity/2026.1')).resolve()
PORT = os.environ.get('DEMO_COM_PORT') or settings.get('serial_port', 'COM15')
TC = str(IDE / 'toolchain/bin/riscv-none-elf-')
OCD = IDE / 'openocd/bin/openocd.exe'
GDB = IDE / 'toolchain/bin/riscv-none-elf-gdb.exe'
APB = None
OCD_ARGS = None
ELF = None
ELF_SHA = None


def within(root, relative):
    path = (root / relative).resolve()
    path.relative_to(root)
    return path


def configure(package):
    global APB, OCD_ARGS
    root = Path(package).resolve()
    layout_file = root / 'load_layout.json'
    layout = json.loads(layout_file.read_text(encoding='utf-8-sig')) if layout_file.exists() else {}
    project = within(root, layout.get('project_root', '.'))
    bsp = project / 'embedded_sw/riscv_soc'
    APB = bsp / 'software/standalone/apbTest'
    ftdi = bsp / 'bsp/efinix/EfxSapphireSoc/openocd/ftdi_ti.cfg'
    debug = bsp / 'bsp/efinix/EfxSapphireSoc/openocd/debug_ti.cfg'
    for path in (OCD, GDB, ftdi, debug, bsp / 'cpu0.yaml'):
        if not path.is_file():
            raise FileNotFoundError(f'Missing packaged or installed loader dependency: {path}')
    if not APB.is_dir():
        raise FileNotFoundError(APB)
    # This is the explicit loader, not the no-reset diagnostic tool.
    # The packaged vendor debug cfg performs init/halt; GDB then performs the
    # preserved LT3 reset-init/load/reset-halt/main-breakpoint sequence.
    OCD_ARGS = ['-c', 'gdb_port 3333', '-c', 'telnet_port 4444', '-c', 'tcl_port 6666',
                '-c', 'set bsp_loc {' + bsp.as_posix() + '}', '-f', str(ftdi), '-f', str(debug)]


def efinity_environment():
    """Mirror Efinity 2026.1 setup.bat without interpolating package paths into cmd."""
    env = os.environ.copy()
    home = EFINITY.as_posix()
    for name, suffix in {'EFINITY_HOME': '', 'EFXPT_HOME': '/pt', 'EFXPGM_HOME': '/pgm',
                         'EFXDBG_HOME': '/debugger', 'EFXIPM_HOME': '/ipm',
                         'EFXSVF_HOME': '/debugger/svf_player',
                         'EFXSERDESDBG_HOME': '/debugger/serdes_debug_tool'}.items():
        env[name] = home + suffix
    env['PYTHONHOME'] = str(EFINITY / 'python311')
    env.pop('PYTHONPATH', None)
    bins = ['python311/bin', 'bin', 'pgm/bin', 'debugger/bin', 'debugger/svf_player/bin',
            'debugger/serdes_debug_tool/bin', 'scripts', 'ipm/bin/ip_packager']
    env['PATH'] = os.pathsep.join(str(EFINITY / b) for b in bins) + os.pathsep + env.get('PATH', '')
    env['EFINITY_USER_DIR_INI'] = (Path(env.get('LOCALAPPDATA', '.')) / 'efinity/user_dir.ini').as_posix()
    env['QT_LOGGING_CONF'] = str(EFINITY / 'bin/lc.ini')
    env['QT_PLUGIN_PATH'] = str(EFINITY / 'bin')
    env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(EFINITY / 'python311/platforms')
    env['QTWEBENGINE_CHROMIUM_FLAGS'] = '--disable-gpu'
    return env


class Tcl:
    def __init__(self):
        self.s = socket.create_connection(('127.0.0.1', 6666), timeout=10)
        self.s.settimeout(10)

    def call(self, command):
        self.s.sendall(command.encode() + b'\x1a')
        buf = bytearray()
        while not buf.endswith(b'\x1a'):
            block = self.s.recv(65536)
            if not block:
                raise ConnectionError('OpenOCD closed TCL')
            buf.extend(block)
        return bytes(buf[:-1]).decode(errors='replace')
    def close(self):
        self.s.close()


def assert_debug_ports_free():
    for port in (3333, 4444, 6666):
        sock = socket.socket()
        try:
            if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            sock.bind(('127.0.0.1', port))
        except OSError as exc:
            raise RuntimeError(f'Debug port {port} is already in use; nothing started') from exc
        finally:
            sock.close()
