#ifndef PLAYER_INPUT_H
#define PLAYER_INPUT_H
#include "gpu.h"
#include <stdint.h>
#define PLAYER_INPUT_ABI 0x50490100u
#define PLAYER_INPUT_TIMEOUT_TICKS (((u64)BSP_CLINT_HZ+4u)/5u)
void player_input_init(void);
void player_input_revoke(u64 now);
u32 player_input_connect(u64 now);
/* Silent data lines: format, generation and sequence must all validate. */
int player_input_is_data(const char *line);
void player_input_line(const char *line,u64 now);
void player_input_service(u64 now,int moving);
void player_input_take(int64_t *dx,int64_t *dy);
void player_input_clip(int clip_x,int clip_y);
u32 player_input_mask(void);
u32 player_input_session(void);
void player_input_local_gate(u64 now,int permit);
void player_input_local_fault(u64 now);
void player_input_local_pair(u64 now,u64 issue,u32 epoch,u32 seq,u32 mask);
u32 player_input_local_epoch(void);
void player_input_local_fire(int held);
#endif
