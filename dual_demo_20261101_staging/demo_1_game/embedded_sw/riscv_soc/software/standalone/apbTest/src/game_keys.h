#ifndef GAME_KEYS_H
#define GAME_KEYS_H
#include "gpu.h"
/* User board manual (2026-10-29): KEY1=P14 fire, KEY2=N14 shield, KEY3=A3 start, active low (schematic contract, not a
 * board measurement). read[1:3] is KEY1/2/3; write/OE bit0 remains the I2C permission owner. KEY0=C4 is the PLL reset input. */
#define GAME_KEYS_BOARD_VALIDATED 1
#define GAME_KEYS_ACTIVE_LOW_MASK 14u
#define GAME_KEYS_HW_VARIANT 0x42330163u /* 0x1E8: the CMD bit wires the keys to GPIO read[3:1] */
static inline u32 game_keys_normalize(u32 levels,u32 active_low){return (levels^active_low)&14u;}
int game_keys_hw_present(void);
void game_keys_init(void);
void game_keys_sample(u64 now,u32 pressed,int permit,int start_permit);
void game_keys_service(u64 now,int permit,int start_permit);
int game_keys_take_shield(void);
void game_keys_request_shield(void);
int game_keys_take_start(void);
int game_keys_take_menu_key2(void); /* GHD3S: KEY2 press while no game input is permitted (menu) */
u32 game_keys_stable(void);
#endif
