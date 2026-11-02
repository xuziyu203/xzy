"""Read-only FPGA status after JTAG configuration (same call the Efinity Programmer GUI makes for 'Device is in user mode!'):
EfxHwManager.read_device_info -> IDCODE, PROG_STATE (decoded from a SAMPLE/PRELOAD boundary-scan read, IR 0010), CDONE/NSTATUS
registers and USERCODE. Nothing is written to the device configuration."""
import os, sys, json
sys.path.insert(0, os.path.join(os.environ['EFINITY_HOME'], 'pgm', 'bin'))  # parent of the installed efx_pgm package
sys.path.append(os.path.join(os.environ['EFINITY_HOME'], 'debugger', 'bin'))
from efx_pgm.util.gen_util import set_efinity_user_dir_env
set_efinity_user_dir_env()
from efx_pgm.efx_hw_manager import EfxHwManager
from efx_pgm.define import JtagParameters


class Console:
    def write(self, m): print('[pgm]', m)
    def write_error_msg(self, m): print('[pgm ERROR]', m)


m = EfxHwManager(console=Console())
p = JtagParameters(frequency=6e6, url='ftdi://0x0403:0x6011:0:fe/1', target_num=1, devices=None, jtag_user='USER1',
                   jtag_backend='pyftdi', plain_jtag=True, trst=False, exclusive_mode=False)  # single-device chain (Ti60, 0x10660A79)
info = m.read_device_info(p, skip_prog_state=False, read_registers=True, read_usercode=True)
print(json.dumps(info, default=str, indent=1))
print('PROG_STATE=' + str(info.get('PROG_STATE')))
