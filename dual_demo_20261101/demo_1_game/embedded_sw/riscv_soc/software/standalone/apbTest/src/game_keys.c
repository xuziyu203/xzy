#include "game_keys.h"
#include "player_input.h"
#include "bsp.h"
#include "soc.h"
/* Normalized pressed bits only. No board polarity assumption, no GPIO writes.
 * Debounce is elapsed CLINT time; missed/gapped service never invents an edge. */
/* Each key has its own timer. The full-width last-service check below
 * bounds these low32 differences to <40ms, including natural low32 wrap. */
/* CMD: KEY3 = start. Same 20 ms debounce; a press counts once and only after a debounced release (a key held across a
 * service gap, a reload or the menu->game switch never fires); outside start_permit any pending start is dropped. */
static u64 key_last;
static u32 key_since[3];
static u32 key_candidate,key_stable,key_seeded,key_edge_armed,key_toggle,key_start_armed,key_start,key_menu2;
static int key_hw=-1;
int game_keys_hw_present(void){if(key_hw<0)key_hw=read_u32(IO_APB_SLAVE_0_INPUT+0x1E8u)==GAME_KEYS_HW_VARIANT;return key_hw;}
void game_keys_init(void){key_since[0]=key_since[1]=key_since[2]=0;key_last=0;key_candidate=key_stable=key_seeded=key_edge_armed=key_toggle=key_start_armed=key_start=key_menu2=0;key_hw=-1;}
void game_keys_sample(u64 now,u32 pressed,int permit,int start_permit){
 u32 old;pressed&=14u;
 if(!key_seeded || now<key_last || now-key_last>=(u64)BSP_CLINT_HZ/25u){
  key_seeded=1;key_candidate=pressed;key_stable=key_edge_armed=key_toggle=key_start_armed=key_start=0;key_since[0]=key_since[1]=key_since[2]=(u32)now;
 }
 key_last=now;
 old=key_stable;
 for(u32 i=0;i<3;i++){
  u32 bit=2u<<i;
  if((pressed^key_candidate)&bit){key_candidate^=bit;key_since[i]=(u32)now;}
  if((u32)((u32)now-key_since[i])>=BSP_CLINT_HZ/50u)key_stable=(key_stable&~bit)|(key_candidate&bit);
 }
 if((u32)((u32)now-key_since[1])>=BSP_CLINT_HZ/50u){
  if(!(key_stable&4u))key_edge_armed=1;
  if((key_stable&~old&4u) && key_edge_armed){if(permit)key_toggle=1;else key_menu2=1;key_edge_armed=0;}
 }
 if((u32)((u32)now-key_since[2])>=BSP_CLINT_HZ/50u){
  if(!(key_stable&8u))key_start_armed=1;
  if((key_stable&~old&8u) && key_start_armed){if(start_permit)key_start=1;key_start_armed=0;}
 }
 if(!permit)key_toggle=0;
 if(!start_permit)key_start=0;
 player_input_local_fire(permit && (key_stable&2u));
}
void game_keys_service(u64 now,int permit,int start_permit){
 u32 pressed=0;
#if GAME_KEYS_BOARD_VALIDATED
 if(game_keys_hw_present())pressed=game_keys_normalize(read_u32(SYSTEM_GPIO_0_IO_CTRL),GAME_KEYS_ACTIVE_LOW_MASK);
#endif
 game_keys_sample(now,pressed,permit,start_permit);
}
int game_keys_take_shield(void){u32 n=key_toggle;key_toggle=0;return n!=0;}
void game_keys_request_shield(void){key_toggle=1;}
int game_keys_take_menu_key2(void){u32 n=key_menu2;key_menu2=0;return n!=0;}
int game_keys_take_start(void){u32 n=key_start;key_start=0;return n!=0;}
u32 game_keys_stable(void){return key_stable;}
