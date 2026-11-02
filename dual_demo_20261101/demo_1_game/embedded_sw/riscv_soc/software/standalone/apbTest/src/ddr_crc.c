#include "ddr_crc.h"
#include "bsp.h"
#include "io.h"
#define DDR_CRC_REG(o) (IO_APB_SLAVE_0_INPUT + (o))
int ddr_crc_present(void){return read_u32(DDR_CRC_REG(0x3E0u))==DDR_CRC_ABI;}
int ddr_crc_start(uint32_t base,uint32_t bytes){
 uint32_t s;
 if(!ddr_crc_present())return -1;
 if(read_u32(DDR_CRC_REG(0x3F0u))&1u)return -1; /* a previous run is still busy: never mix its result */
 write_u32(base,DDR_CRC_REG(0x3E4u));write_u32(bytes,DDR_CRC_REG(0x3E8u));write_u32(1u,DDR_CRC_REG(0x3ECu));
 s=read_u32(DDR_CRC_REG(0x3F0u));
 return (s&8u)?-1:0; /* refused */
}
int ddr_crc_poll(uint32_t *crc){
 uint32_t s=read_u32(DDR_CRC_REG(0x3F0u));
 if(s&1u)return 0;
 if((s&6u)!=2u)return -1;
 *crc=read_u32(DDR_CRC_REG(0x3F4u));return 1;
}
