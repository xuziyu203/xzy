#include "effect_scene.h"
#include "enemy_scene.h"
#include "bsp.h"
#include "player_scene.h"
#include "game.h"
#include "asset_upload.h"
#include <string.h>

/* One pooled animation. age advances in game ticks only (paused time never
 * counts); frame = age / frame_ticks, retired after the last frame. */
typedef struct { int16_t x,y;uint8_t type,priority,active,pad;u64 age; } effect_state_t;
static effect_state_t fx_pool[EFFECT_POOL_CAPACITY];
static u32 fx_pending[EFFECT_CAPACITY][2];
static const uint16_t fx_values[EFFECT_TYPE_COUNT][5]=EFFECT_VALUES;
static u64 fx_frame_ticks[EFFECT_TYPE_COUNT],fx_last_tick;
static u32 fx_enabled,fx_stage,fx_position,fx_sequence,fx_pending_count,fx_pending_active;
static u32 fx_spawned,fx_dropped,fx_evicted;
static u32 fx_display_active,fx_display_spawned,fx_display_dropped,fx_display_evicted;

static u64 spark_cooldown[ENEMY_CAPACITY];
static struct { u64 remaining,flash,gap;u32 mode,charges; } shield;
static effect_snapshot_t fx_pending_snapshot,fx_display_snapshot;
static u32 shield_mode,shield_absorbed,fx_merged,fx_session;
#define FX_TICKS(ms) ((u64)BSP_CLINT_HZ*(ms)/1000u)
static u64 fx_sub(u64 value,u64 dt){return value>dt?value-dt:0;}
u32 effect_shield_mode(void){return shield_mode;}
gpu_result_t effect_shield_select(u32 mode){
 if(fx_stage)return GPU_ERR_BUSY;
 if(mode>SHIELD_GUARD)return GPU_ERR_PARAM;
 if(mode && (!fx_enabled || !player_scene_selected() || (mode==SHIELD_GUARD && !game_selected())))return GPU_ERR_PARAM;
 if(mode==SHIELD_GUARD && (asset_upload_valid_mask()&EFFECT_REQUIRED_ASSETS)!=EFFECT_REQUIRED_ASSETS)return GPU_ERR_PARAM;
 shield_mode=mode;return GPU_OK;
}
static int shield_visible(void){return shield.mode && player_scene_selected() && !game_over() &&
 (shield.mode==SHIELD_VISUAL || shield.mode==SHIELD_MANUAL || shield.remaining || shield.flash);}
gpu_result_t effect_shield_manual_enable(void){
 if(fx_stage)return GPU_ERR_BUSY;
 if(!fx_enabled || !player_scene_selected() || !game_running() || (asset_upload_valid_mask()&EFFECT_REQUIRED_ASSETS)!=EFFECT_REQUIRED_ASSETS)return GPU_ERR_PARAM;
 shield.mode=SHIELD_MANUAL;shield.remaining=0;shield.charges=0;return GPU_OK;
}
static u32 shield_toggles;
gpu_result_t effect_shield_toggle(void){
 gpu_result_t r;
 if(fx_stage)return GPU_ERR_BUSY;
 if(shield.mode==SHIELD_MANUAL){shield.mode=SHIELD_OFF;shield.flash=0;shield_toggles++;return GPU_OK;}
 r=effect_shield_manual_enable();if(r==GPU_OK)shield_toggles++;return r;
}
u32 effect_shield_on(void){return shield.mode==SHIELD_MANUAL && shield_visible();}
u32 effect_shield_toggles(void){return shield_toggles;}
u32 effect_shield_absorbed(void){return shield_absorbed;}
int effect_shield_absorb(void){
 if(!game_player_vulnerable())return 0;
 if(shield.mode!=SHIELD_MANUAL){
  if(shield.mode!=SHIELD_GUARD || !shield.remaining || !shield.charges)return 0;
  shield.charges--;if(!shield.charges)shield.remaining=0;
 }
 /* GHD4: one hit pulse per FX_SHIELD_FLASH_GAP_MS; absorbs during a pulse or its gap are counted but do not extend it */
 shield_absorbed++;if(!shield.gap){shield.flash=FX_TICKS(FX_SHIELD_FLASH_MS);shield.gap=FX_TICKS(FX_SHIELD_FLASH_GAP_MS);}return 1;
}
const effect_snapshot_t *effect_scene_snapshot(void){return &fx_display_snapshot;}

void effect_scene_init(void){
 fx_enabled=fx_stage=0;shield_mode=fx_session=0;effect_scene_clear();memset(&fx_display_snapshot,0,sizeof(fx_display_snapshot));
 fx_display_active=fx_display_spawned=fx_display_dropped=fx_display_evicted=0;
}
u32 effect_scene_selected(void){return fx_enabled;}
gpu_result_t effect_scene_select(u32 enable){
 u32 cap;gpu_result_t r;if(enable>1u)return GPU_ERR_PARAM;if(fx_stage)return GPU_ERR_BUSY;
 if(!enable && shield_mode)return GPU_ERR_PARAM;
 if(enable){
  if(enemy_scene_selected()==ENEMY_SCENE_SINGLE)return GPU_ERR_PARAM;
  r=gpu_effect_scene_capacity(&cap);if(r!=GPU_OK)return r;
 }
 fx_enabled=enable;return GPU_OK;
}
void effect_scene_clear(void){
 memset(fx_pool,0,sizeof(fx_pool));memset(spark_cooldown,0,sizeof(spark_cooldown));
 memset(&shield,0,sizeof(shield));memset(&fx_pending_snapshot,0,sizeof(fx_pending_snapshot));
 shield_absorbed=fx_merged=0;
}
void effect_scene_reset_time(u64 now){fx_last_tick=now;}
gpu_result_t effect_scene_begin(u64 now){
 gpu_result_t r;u32 t;
 if(shield_mode && (!fx_enabled || !player_scene_selected() || (shield_mode==SHIELD_GUARD && !game_selected())))return GPU_ERR_PARAM;
 if(!fx_enabled)return gpu_effect_scene_mode(0);
 r=gpu_effect_scene_mode(1);if(r!=GPU_OK)return r;
 for(t=0;t<EFFECT_TYPE_COUNT;t++)fx_frame_ticks[t]=(u64)BSP_CLINT_HZ*fx_values[t][4]/1000u;
 effect_scene_clear();fx_spawned=fx_dropped=fx_evicted=0;fx_last_tick=now;fx_session++;shield_toggles=0;
 shield.mode=shield_mode;shield.charges=shield_mode==SHIELD_GUARD?FX_SHIELD_CHARGES:0;
 shield.remaining=shield_mode==SHIELD_GUARD?FX_TICKS(FX_SHIELD_DURATION_MS):0;
 return effect_scene_collect();
}
gpu_result_t effect_scene_phase_begin(u64 now){
 gpu_result_t r=gpu_effect_scene_mode(fx_enabled);if(r!=GPU_OK)return r;
 memset(fx_pool,0,sizeof(fx_pool));memset(spark_cooldown,0,sizeof(spark_cooldown));
 fx_last_tick=now;return effect_scene_collect();
}
int effect_scene_spawn(u32 type,int32_t cx,int32_t cy,u32 priority,u64 now){
 u32 i,victim=EFFECT_POOL_CAPACITY,size;int32_t x,y;(void)now;
 if(!fx_enabled || type>=EFFECT_SHIELD)return 0;
 size=fx_values[type][2];
 /* Top-left from the centre, clamped fully on screen; x snapped to 8 px. */
 x=cx-(int32_t)size/2;y=cy-(int32_t)size/2;
 if(x<0)x=0;
 if(x>1920-(int32_t)size)x=1920-(int32_t)size;
 if(y<0)y=0;
 if(y>1080-(int32_t)size)y=1080-(int32_t)size;
 x&=~7;
 /* Rendering only: damage/score is already settled by combat. Do not refresh
  * merged age: sustained fire must not create immortal visual instances. */
 {u32 count=0,near=EFFECT_POOL_CAPACITY,limit=type==EFFECT_SMALL?FX_SMALL:(type==EFFECT_LARGE?FX_LARGE:FX_SPARK);
  for(i=0;i<EFFECT_POOL_CAPACITY;i++)if(fx_pool[i].active && fx_pool[i].type==type){
   int32_t dx=x-fx_pool[i].x,dy=y-fx_pool[i].y;count++;
   if(dx>=-32 && dx<=32 && dy>=-32 && dy<=32)near=i;
  }
  if(count>=limit){if(near<EFFECT_POOL_CAPACITY){fx_merged++;return 2;}fx_dropped++;return 0;}
 }
 for(i=0;i<EFFECT_POOL_CAPACITY;i++)if(!fx_pool[i].active){victim=i;break;}
 if(victim==EFFECT_POOL_CAPACITY){
  /* Pool full: a death replaces the oldest lower-priority effect, else drops. */
  u64 oldest=0;
  for(i=0;i<EFFECT_POOL_CAPACITY;i++)
   if(fx_pool[i].priority<priority && (victim==EFFECT_POOL_CAPACITY || fx_pool[i].age>oldest)){victim=i;oldest=fx_pool[i].age;}
  if(victim==EFFECT_POOL_CAPACITY){fx_dropped++;return 0;}
  fx_evicted++;
 }
 fx_pool[victim]=(effect_state_t){(int16_t)x,(int16_t)y,(uint8_t)type,(uint8_t)priority,1,0,0};
 fx_spawned++;return 1;
}
void effect_scene_spark(u32 enemy,int32_t cx,int32_t cy,u64 now){
 if(enemy>=ENEMY_CAPACITY || spark_cooldown[enemy])return;
 if(effect_scene_spawn(EFFECT_SPARK,cx,cy,EFFECT_PRIORITY_SPARK,now))spark_cooldown[enemy]=FX_TICKS(FX_SPARK_COOLDOWN_MS);
}
gpu_result_t effect_scene_advance(u64 now){
 u32 i;u64 dt;
 if(!fx_enabled)return GPU_OK;
 if(fx_stage)return GPU_ERR_BUSY;
 dt=now>=fx_last_tick?now-fx_last_tick:0;fx_last_tick=now;
 for(i=0;i<ENEMY_CAPACITY;i++)spark_cooldown[i]=enemy_scene_emitter_restarted(i)?0:fx_sub(spark_cooldown[i],dt);
 shield.remaining=fx_sub(shield.remaining,dt);shield.flash=fx_sub(shield.flash,dt);shield.gap=fx_sub(shield.gap,dt);
 if(game_over()){shield.remaining=shield.flash=shield.gap=0;shield.mode=0;}
 for(i=0;i<EFFECT_POOL_CAPACITY;i++)if(fx_pool[i].active){
  effect_state_t *p=&fx_pool[i];u64 life=fx_frame_ticks[p->type]*fx_values[p->type][3];
  if(dt>=life-p->age)p->active=0;else p->age+=dt;
 }
 return GPU_OK;
}
static int32_t fx_y(u32 word){return (int32_t)((word&0xfffu)^0x800u)-0x800;}
static void fx_insert(u32 n,u32 w0,u32 w1){
 u32 k;for(k=n;k>0 && fx_y(fx_pending[k-1u][1])>fx_y(w1);k--){fx_pending[k][0]=fx_pending[k-1u][0];fx_pending[k][1]=fx_pending[k-1u][1];}
 fx_pending[k][0]=w0;fx_pending[k][1]=w1;
}
gpu_result_t effect_scene_collect(void){
 u32 i,n=0,active=0,counts=0;
 if(!fx_enabled)return GPU_OK;
 if(fx_stage)return GPU_ERR_BUSY;
 for(i=0;i<EFFECT_POOL_CAPACITY;i++)if(fx_pool[i].active){
  effect_state_t *p=&fx_pool[i];u32 frame=(u32)(p->age/fx_frame_ticks[p->type]),w0,w1;
  if(frame>=fx_values[p->type][3])frame=fx_values[p->type][3]-1u;
  w0=((u32)p->type<<15)|(frame<<12)|(u32)p->x;w1=(u32)p->y;active++;
  fx_insert(n,w0,w1);n++;counts+=1u<<(p->type*8u);
 }
 if(game_over()){shield.remaining=shield.flash=0;shield.mode=0;}
 if(shield_visible()){
  int32_t x=(int32_t)player_scene_pending_x()+(int32_t)PLAYER_WIDTH/2-64;
  int32_t y=(int32_t)player_scene_pending_y()+(int32_t)PLAYER_HEIGHT/2-64;
  fx_insert(n,(EFFECT_SHIELD<<15)|((shield.flash?1u:0u)<<12)|((u32)x&0xfffu),(u32)y&0xfffu);n++;
 }
 fx_pending_snapshot.remaining=shield.remaining;fx_pending_snapshot.session=fx_session;
 fx_pending_snapshot.absorbed=shield_absorbed;fx_pending_snapshot.merged=fx_merged;
 fx_pending_snapshot.flags=shield.mode|(shield.charges<<8)|(shield_visible()?1u<<16:0)|(shield.flash?1u<<17:0);
 fx_pending_snapshot.counts=counts;

 fx_pending_count=n;fx_pending_active=active;fx_position=0;
 fx_sequence=gpu_enemy_scene_frame_sequence()+1u;fx_pending_snapshot.sequence=fx_sequence;fx_stage=1;return GPU_OK;
}
int effect_scene_stage_service(void){
 gpu_result_t r;u32 n;if(!fx_enabled)return 1;
 if(fx_stage==1){r=gpu_effect_scene_open(fx_pending_count,fx_sequence);if(r!=GPU_OK)return -1;fx_stage=2;return 0;}
 if(fx_stage==2){for(n=0;n<8 && fx_position<fx_pending_count;n++,fx_position++){
   r=gpu_effect_scene_write(fx_pending[fx_position]);if(r!=GPU_OK)return -1;}
  if(fx_position==fx_pending_count)fx_stage=3;
  return 0;}
 if(fx_stage==3){r=gpu_effect_scene_seal(fx_pending_count,fx_sequence);if(r!=GPU_OK)return -1;fx_stage=4;}
 return fx_stage==4?1:-1;
}
/* S (PIPE ON): snapshot at claim. */
static effect_snapshot_t fx_if_snapshot;static u32 fx_if_n[4],fx_if_valid;
void effect_scene_claimed(void){if(fx_enabled && fx_stage==4){fx_if_snapshot=fx_pending_snapshot;fx_if_n[0]=fx_pending_active;fx_if_n[1]=fx_spawned;fx_if_n[2]=fx_dropped;fx_if_n[3]=fx_evicted;fx_if_valid=1;fx_stage=0;}}
void effect_scene_confirmed(void){if(fx_if_valid){fx_display_snapshot=fx_if_snapshot;fx_display_active=fx_if_n[0];fx_display_spawned=fx_if_n[1];fx_display_dropped=fx_if_n[2];fx_display_evicted=fx_if_n[3];fx_if_valid=0;return;}
 if(fx_enabled && fx_stage==4){
 fx_display_snapshot=fx_pending_snapshot;fx_display_active=fx_pending_active;fx_display_spawned=fx_spawned;fx_display_dropped=fx_dropped;fx_display_evicted=fx_evicted;fx_stage=0;}}
void effect_scene_cancel(void){if(fx_enabled)gpu_effect_scene_cancel();fx_stage=0;fx_if_valid=0;}
gpu_result_t effect_scene_disable(void){return gpu_effect_scene_mode(0);}
void effect_scene_restored(void){
 fx_if_valid=0;
 effect_scene_clear();memset(&fx_display_snapshot,0,sizeof(fx_display_snapshot));fx_display_active=fx_display_spawned=fx_display_dropped=fx_display_evicted=0;
}
u32 effect_scene_displayed_active(void){return fx_display_active;}
u32 effect_scene_displayed_spawned(void){return fx_display_spawned;}
u32 effect_scene_displayed_dropped(void){return fx_display_dropped;}
u32 effect_scene_displayed_evicted(void){return fx_display_evicted;}
