#include "player_bullet_scene.h"
#include "player_scene.h"
#include "player_input.h"
#include "enemy_scene.h"
#include "asset_upload.h"
#include "bsp.h"
#include <string.h>
typedef struct { int32_t y_q16;int16_t x;uint16_t active; } player_bullet_state_t;
typedef char player_bullet_state_is_8_bytes[(sizeof(player_bullet_state_t)==8)?1:-1];
typedef char player_bullet_descriptor_is_8_bytes[(sizeof(player_bullet_object_desc_t)==8)?1:-1];
static player_bullet_state_t pb_pool[PLAYER_BULLET_CAPACITY];
static int16_t pb_previous_y[PLAYER_BULLET_CAPACITY];
static uint8_t pb_hitbox[4];
static player_bullet_object_desc_t pb_pending[PLAYER_BULLET_CAPACITY],pb_display[PLAYER_BULLET_CAPACITY];
static u64 pb_last_tick,pb_fraction,pb_due;
static u32 pb_enabled,pb_stage,pb_position,pb_sequence,pb_fire_enabled=1;
void player_bullet_scene_set_fire(u32 enable){pb_fire_enabled=enable!=0;}
static u32 pb_pending_count,pb_pending_active,pb_pending_visible;
static u32 pb_display_count,pb_display_active,pb_display_visible;
static u32 pb_spawned,pb_dropped,pb_skipped,pb_display_spawned,pb_display_dropped,pb_display_skipped;
static int pb_floor(int32_t q){return q>=0?q/65536:-1-(int)((-(int64_t)q-1)/65536);}
static int pb_visible(int x,int y){
 const uint8_t *mask=asset_upload_bullet_mask(PLAYER_BULLET_MATERIAL_TYPE);
 int sx0=x<0?-x:0,sx1=1920-x,sy0=y<0?-y:0,sy1=1080-y,row;u32 columns;
 if(!mask)return 0;
 if(sx1>16)sx1=16;
 if(sy1>32)sy1=32;
 if(sx0>=sx1 || sy0>=sy1)return 0;
 columns=((1u<<(sx1-sx0))-1u)<<(16-sx1);
 for(row=sy0;row<sy1;row++)if((((u32)mask[row*2]<<8)|mask[row*2+1])&columns)return 1;
 return 0;
}
void player_bullet_scene_init(void){
 pb_enabled=pb_stage=pb_display_count=pb_display_active=pb_display_visible=0;
 pb_display_spawned=pb_display_dropped=pb_display_skipped=0;
 memset(pb_pool,0,sizeof(pb_pool));
}
u32 player_bullet_scene_selected(void){return pb_enabled;}
gpu_result_t player_bullet_scene_select(u32 enable){
 u32 cap;gpu_result_t r;if(enable>1u)return GPU_ERR_PARAM;if(pb_stage)return GPU_ERR_BUSY;
 if(enable){
  if(enemy_scene_selected()==ENEMY_SCENE_SINGLE || !player_scene_selected())return GPU_ERR_PARAM;
  r=gpu_player_bullet_scene_capacity(&cap);if(r!=GPU_OK)return r;
  if(cap!=PLAYER_BULLET_CAPACITY)return GPU_ERR_UNSUPPORTED;
 }
 pb_enabled=enable;return GPU_OK;
}
void player_bullet_scene_reset_time(u64 now){
 u64 remaining=pb_due>pb_last_tick?pb_due-pb_last_tick:0;
 pb_due=remaining>~(u64)0-now?~(u64)0:now+remaining;pb_last_tick=now;
}
static gpu_result_t pb_collect(void){
 u32 i,k;pb_pending_count=pb_pending_active=pb_pending_visible=0;
 for(i=0;i<PLAYER_BULLET_CAPACITY;i++)if(pb_pool[i].active){
  player_bullet_state_t *p=&pb_pool[i];int y=pb_floor(p->y_q16);
  /* Preserve retiring paths until combat, then reclaim before collection. */
  if(p->active&4u){p->active=0;continue;}
  pb_pending_active++;
  if(p->x<=-16 || p->x>=1920 || y<=-32 || y>=1080)continue;
  /* PB2.0: non-decreasing y; the slot field is the sorted position. */
  for(k=pb_pending_count;k>0 && (int16_t)(pb_pending[k-1u].word[1]&65535u)>y;k--)pb_pending[k]=pb_pending[k-1u];
  pb_pending[k].word[0]=(uint16_t)p->x;
  pb_pending[k].word[1]=(uint16_t)y|(PLAYER_BULLET_MATERIAL_TYPE<<16);
  pb_pending_count++;
  if(pb_visible(p->x,y))pb_pending_visible++;
 }
 for(k=0;k<pb_pending_count;k++)pb_pending[k].word[1]|=k<<17;
 pb_position=0;pb_sequence=gpu_enemy_scene_frame_sequence()+1u;pb_stage=1;return GPU_OK;
}
gpu_result_t player_bullet_scene_begin(u64 now){
 gpu_result_t r;if(!pb_enabled)return gpu_player_bullet_scene_mode(0);
 if(!asset_upload_bullet_mask(PLAYER_BULLET_MATERIAL_TYPE))return GPU_ERR_PARAM;
 r=gpu_player_bullet_scene_mode(1);if(r!=GPU_OK)return r;
 memset(pb_pool,0,sizeof(pb_pool));pb_spawned=pb_dropped=pb_skipped=0;
 {const uint8_t *mask=asset_upload_bullet_mask(PLAYER_BULLET_MATERIAL_TYPE);u32 x,y;
  pb_hitbox[0]=16;pb_hitbox[1]=32;pb_hitbox[2]=pb_hitbox[3]=0;
  for(y=0;y<32;y++)for(x=0;x<16;x++)if(mask[y*2+x/8]&(128u>>(x&7))){
   if(x<pb_hitbox[0])pb_hitbox[0]=(uint8_t)x;
   if(y<pb_hitbox[1])pb_hitbox[1]=(uint8_t)y;
   if(x+1u>pb_hitbox[2])pb_hitbox[2]=(uint8_t)(x+1u);
   if(y+1u>pb_hitbox[3])pb_hitbox[3]=(uint8_t)(y+1u);
  }
 }
 pb_last_tick=pb_due=now;pb_fraction=0;return pb_collect();
}
gpu_result_t player_bullet_scene_advance(u64 now){
 u32 i,fire;u64 dt,dy;
 if(!pb_enabled)return GPU_OK;
 if(pb_stage)return GPU_ERR_BUSY;
 dt=now>=pb_last_tick?now-pb_last_tick:0;pb_last_tick=now;
 if(dt/BSP_CLINT_HZ>=2u){dy=2048ull*65536u;pb_fraction=0;}
 else{u64 scaled=dt*(600ull*65536u)+pb_fraction;dy=scaled/BSP_CLINT_HZ;pb_fraction=scaled%BSP_CLINT_HZ;}
 for(i=0;i<PLAYER_BULLET_CAPACITY;i++)if(pb_pool[i].active){
  /* floor(y_q16) <= -32 is fully clipped. The first fraction below -31
   * already crosses that integer pixel boundary for upward travel. */
  pb_previous_y[i]=(int16_t)pb_floor(pb_pool[i].y_q16);pb_pool[i].active=3;
  if(pb_pool[i].y_q16 < -31*65536 || dy>(u64)((int64_t)pb_pool[i].y_q16+31*65536)){
   pb_pool[i].y_q16=-32*65536;pb_pool[i].active|=4u;
  }else pb_pool[i].y_q16-=(int32_t)dy;
 }
 /* Only the latest fresh Space can fire. Release/repress cannot bypass the
  * 160ms cooldown; an expired opportunity is never queued for later replay. */
 fire=(player_input_mask()&32u)!=0 && pb_fire_enabled;
 if(fire && now>=pb_due){
  int x=(int)player_scene_pending_x()+40,y=(int)player_scene_pending_y()-32;u32 slot;
  pb_due=now+((u64)BSP_CLINT_HZ*160u)/1000u;
  if(y<=-32 || y>=1080 || x<=-16 || x>=1920)pb_skipped++;
  else{
   for(slot=0;slot<PLAYER_BULLET_CAPACITY;slot++)if(!pb_pool[slot].active)break;
   if(slot==PLAYER_BULLET_CAPACITY)pb_dropped++;
   else{pb_pool[slot].x=(int16_t)x;pb_pool[slot].y_q16=y*65536;pb_pool[slot].active=1;pb_spawned++;}
  }
 }
 return GPU_OK;
}
gpu_result_t player_bullet_scene_collect(void){
 if(!pb_enabled)return GPU_OK;
 if(pb_stage)return GPU_ERR_BUSY;
 return pb_collect();
}
gpu_result_t player_bullet_scene_next(u64 now){
 gpu_result_t r=player_bullet_scene_advance(now);return r==GPU_OK?player_bullet_scene_collect():r;
}
int player_bullet_scene_active_box(u32 slot,combat_box_t *b){
 player_bullet_state_t *p;int y;
 if(!pb_enabled || !b || slot>=PLAYER_BULLET_CAPACITY || pb_hitbox[0]>=pb_hitbox[2])return 0;
 p=&pb_pool[slot];if(!(p->active&1u))return 0;y=pb_floor(p->y_q16);
 b->x0=p->x+pb_hitbox[0];b->x1=p->x+pb_hitbox[2];
 b->y0=y+pb_hitbox[1];b->y1=y+pb_hitbox[3];
 b->previous_x0=b->x0;b->previous_y0=pb_previous_y[slot]+pb_hitbox[1];b->continuous=(p->active&2u)!=0;
 return 1;
}
void player_bullet_scene_kill(u32 slot){if(!pb_stage && slot<PLAYER_BULLET_CAPACITY)pb_pool[slot].active=0;}
int player_bullet_scene_stage_service(void){
 gpu_result_t r;u32 n;if(!pb_enabled)return 1;
 if(pb_stage==1){r=gpu_player_bullet_scene_open(pb_pending_count,pb_sequence);if(r!=GPU_OK)return -1;pb_stage=2;return 0;}
 if(pb_stage==2){for(n=0;n<8 && pb_position<pb_pending_count;n++,pb_position++){
   r=gpu_player_bullet_scene_write(pb_pending[pb_position].word);if(r!=GPU_OK)return -1;}
  if(pb_position==pb_pending_count)pb_stage=3;
  return 0;}
 if(pb_stage==3){r=gpu_player_bullet_scene_seal(pb_pending_count,pb_sequence);if(r!=GPU_OK)return -1;pb_stage=4;}
 return pb_stage==4?1:-1;
}
/* S (PIPE ON): snapshot at claim. */
static player_bullet_object_desc_t pb_if[PLAYER_BULLET_CAPACITY];static u32 pb_if_n[7],pb_if_valid;
void player_bullet_scene_claimed(void){if(pb_enabled && pb_stage==4){memcpy(pb_if,pb_pending,pb_pending_count*sizeof(*pb_pending));
 pb_if_n[0]=pb_pending_count;pb_if_n[1]=pb_pending_active;pb_if_n[2]=pb_pending_visible;pb_if_n[3]=pb_spawned;pb_if_n[4]=pb_dropped;pb_if_n[5]=pb_skipped;pb_if_valid=1;pb_stage=0;}}
void player_bullet_scene_confirmed(void){if(pb_if_valid){memcpy(pb_display,pb_if,pb_if_n[0]*sizeof(*pb_if));
 pb_display_count=pb_if_n[0];pb_display_active=pb_if_n[1];pb_display_visible=pb_if_n[2];pb_display_spawned=pb_if_n[3];pb_display_dropped=pb_if_n[4];pb_display_skipped=pb_if_n[5];pb_if_valid=0;return;}
 if(pb_enabled && pb_stage==4){
 memcpy(pb_display,pb_pending,pb_pending_count*sizeof(*pb_pending));
 pb_display_count=pb_pending_count;pb_display_active=pb_pending_active;pb_display_visible=pb_pending_visible;
 pb_display_spawned=pb_spawned;pb_display_dropped=pb_dropped;pb_display_skipped=pb_skipped;pb_stage=0;}}
void player_bullet_scene_cancel(void){if(pb_enabled)gpu_player_bullet_scene_cancel();pb_stage=0;pb_if_valid=0;}
gpu_result_t player_bullet_scene_disable(void){return gpu_player_bullet_scene_mode(0);}
void player_bullet_scene_restored(void){
 pb_if_valid=0;
 memset(pb_pool,0,sizeof(pb_pool));pb_display_count=pb_display_active=pb_display_visible=0;
 pb_display_spawned=pb_display_dropped=pb_display_skipped=0;
}
u32 player_bullet_scene_displayed_active(void){return pb_display_active;}
u32 player_bullet_scene_displayed_visible(void){return pb_display_visible;}
u32 player_bullet_scene_displayed_spawned(void){return pb_display_spawned;}
u32 player_bullet_scene_displayed_dropped(void){return pb_display_dropped;}
u32 player_bullet_scene_displayed_skipped(void){return pb_display_skipped;}
int player_bullet_scene_displayed(u32 slot,player_bullet_object_desc_t *object){
 u32 i;if(!object || slot>=PLAYER_BULLET_CAPACITY)return 0;
 for(i=0;i<pb_display_count;i++)if((pb_display[i].word[1]>>17)==slot){*object=pb_display[i];return 1;}
 return 0;
}
