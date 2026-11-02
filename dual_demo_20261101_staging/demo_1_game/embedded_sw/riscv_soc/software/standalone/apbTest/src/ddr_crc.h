#ifndef DDR_CRC_H
#define DDR_CRC_H
#include <stdint.h>
/* GHD: rtl/ddr_crc_reader.v (APB 0x3E0..0x3FC through ddr_crc_port.v). CRC-32 (zlib) of DDR [base, base+bytes), read through the
 * compositor port while the PL is idle. start returns 0 when the run started, -1 when refused (an owner or a compositor read is
 * outstanding) or the bit has no reader; poll returns 0 while busy, 1 done, -1 error. */
#define DDR_CRC_ABI 0x44435201u
int ddr_crc_present(void);
int ddr_crc_start(uint32_t base, uint32_t bytes);
int ddr_crc_poll(uint32_t *crc);
#endif
