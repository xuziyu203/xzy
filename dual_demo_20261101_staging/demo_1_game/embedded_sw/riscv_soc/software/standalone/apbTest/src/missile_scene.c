#include "missile_scene.h"
#include "enemy_scene.h"
#include "bullet_scene.h"
#include "asset_upload.h"
#include "bsp.h"
#include <string.h>
typedef struct { int32_t x_q16,y_q16;int16_t vx,vy;uint8_t flags,type;uint16_t pad; } missile_state_t;
typedef char missile_state_size[(sizeof(missile_state_t)==16)?1:-1];
enum { MS_ACTIVE=1,MS_MOVED=2,MS_RETIRE=4 };
static missile_state_t missile_pool[MISSILE_CAPACITY];
static int16_t missile_previous_x[MISSILE_CAPACITY],missile_previous_y[MISSILE_CAPACITY];
static missile_object_desc_t missile_pending[MISSILE_CAPACITY],missile_display[MISSILE_CAPACITY];
static u64 missile_due[ENEMY_CAPACITY],missile_last_tick,missile_type_due[2],missile_shared_due;
static u32 missile_enabled,missile_stage,missile_position,missile_sequence,missile_count;
static u32 missile_fire_enabled=1,missile_priority[2],missile_next_type;
static u32 missile_spawned[2],missile_dropped[2];
/* field order: active, visible, cumulative spawned, cumulative dropped. */
static u32 missile_stats[2][4],missile_display_stats[2][4];
/* S1: in RICH the blue type never spawns; its slot timers still run, so red keeps the dual-colour schedule. */
static u32 missile_blue_enabled=1,missile_blue_suppressed;
static const uint8_t missile_widths[2]=MISSILE_WIDTH_VALUES;
static const uint8_t missile_inners[2][4]=MISSILE_HITBOX_VALUES;
static const uint16_t missile_speeds[2]=MISSILE_SPEED_VALUES;
static const uint16_t missile_emitter_ms[2]=MISSILE_EMITTER_MS_VALUES;
static const uint16_t missile_type_ms[2]=MISSILE_TYPE_MS_VALUES;
static u64 missile_after(u64 now,u32 ms){u64 dt=((u64)BSP_CLINT_HZ*ms)/1000u;return dt>~(u64)0-now?~(u64)0:now+dt;}
static int missile_floor(int32_t q){return q>=0?q/65536:-1-(int)((-(int64_t)q-1)/65536);}
static int missile_visible(int x,int y,u32 type){
 const uint8_t *mask=asset_upload_missile_mask(type);int x0=x<0?-x:0,x1=1920-x,y0=y<0?-y:0,y1=1080-y,r;u32 cols;
 if(!mask)return 0;if(x1>missile_widths[type])x1=missile_widths[type];if(y1>32)y1=32;
 if(x0>=x1 || y0>=y1)return 0;cols=((1u<<(x1-x0))-1u)<<(16-x1);
 for(r=y0;r<y1;r++)if((((u32)mask[r*2]<<8)|mask[r*2+1])&cols)return 1;return 0;
}
void missile_scene_init(void){missile_enabled=missile_stage=0;missile_fire_enabled=1;missile_scene_restored();}
u32 missile_scene_selected(void){return missile_enabled;}
gpu_result_t missile_scene_select(u32 enable){
 u32 cap;gpu_result_t r;if(enable>1)return GPU_ERR_PARAM;if(missile_stage)return GPU_ERR_BUSY;
 if(enable){if(!enemy_scene_selected())return GPU_ERR_PARAM;r=gpu_missile_scene_capacity(&cap);if(r!=GPU_OK)return r;}
 missile_enabled=enable;return GPU_OK;
}
void missile_scene_set_fire(u32 enable){missile_fire_enabled=enable!=0;}
void missile_scene_reset_time(u64 now){
 u32 i;for(i=0;i<ENEMY_CAPACITY+3u;i++){
  u64 *due=i<ENEMY_CAPACITY?&missile_due[i]:(i<ENEMY_CAPACITY+2u?&missile_type_due[i-ENEMY_CAPACITY]:&missile_shared_due);
  u64 remain=*due>missile_last_tick?*due-missile_last_tick:0;*due=remain>~(u64)0-now?~(u64)0:now+remain;
 }missile_last_tick=now;
}
gpu_result_t missile_scene_collect(void){
 u32 i,k;missile_object_desc_t d;if(!missile_enabled)return GPU_OK;if(missile_stage)return GPU_ERR_BUSY;
 memset(missile_stats,0,sizeof(missile_stats));missile_count=0;
 for(i=0;i<MISSILE_CAPACITY;i++)if(missile_pool[i].flags&MS_ACTIVE){
  missile_state_t *p=&missile_pool[i];int x=missile_floor(p->x_q16),y=missile_floor(p->y_q16);u32 t=p->type;
  if(p->flags&MS_RETIRE){p->flags=0;continue;}
  missile_stats[t][0]++;missile_stats[t][1]+=missile_visible(x,y,t);
  if(x<=-(int)missile_widths[t] || x>=1920 || y<=-32 || y>=1080)continue;
  d.word[0]=(uint16_t)x|(t<<16)|(1u<<17);d.word[1]=(uint16_t)y;
  for(k=missile_count;k>0 && (int16_t)missile_pending[k-1].word[1]>y;k--)missile_pending[k]=missile_pending[k-1];
  missile_pending[k]=d;missile_count++;
 }
 for(k=0;k<missile_count;k++)missile_pending[k].word[1]|=k<<16;
 for(i=0;i<2;i++){missile_stats[i][2]=missile_spawned[i];missile_stats[i][3]=missile_dropped[i];}
 missile_position=0;missile_sequence=gpu_enemy_scene_frame_sequence()+1u;missile_stage=1;return GPU_OK;
}
gpu_result_t missile_scene_begin(u64 now){
 u32 t,x,y,i;gpu_result_t r;if(!missile_enabled)return gpu_missile_scene_mode(0);
 if((asset_upload_valid_mask()&MISSILE_REQUIRED_ASSETS)!=MISSILE_REQUIRED_ASSETS)return GPU_ERR_PARAM;
 for(t=0;t<2;t++){
  const uint8_t *mask=asset_upload_missile_mask(t);const uint8_t *h=missile_inners[t];u32 any=0;if(!mask)return GPU_ERR_PARAM;
  for(y=0;y<32;y++)for(x=0;x<16;x++)if(mask[y*2+x/8]&(128u>>(x&7))){if(x>=missile_widths[t])return GPU_ERR_PARAM;any=1;}
  if(!any)return GPU_ERR_PARAM;
  for(y=h[1];y<h[3];y++)for(x=h[0];x<h[2];x++)if(!(mask[y*2+x/8]&(128u>>(x&7))))return GPU_ERR_PARAM;
 }
 r=gpu_missile_scene_mode(1);if(r!=GPU_OK)return r;
 missile_blue_enabled=bullet_scene_pattern_mode()!=3u;missile_blue_suppressed=0;
 memset(missile_pool,0,sizeof(missile_pool));memset(missile_spawned,0,sizeof(missile_spawned));memset(missile_dropped,0,sizeof(missile_dropped));
 missile_priority[0]=missile_priority[1]=missile_next_type=0;missile_last_tick=now;missile_shared_due=missile_after(now,MISSILE_SHARED_PERIOD_MS);
 for(t=0;t<2;t++)missile_type_due[t]=missile_after(now,missile_type_ms[t]);
 for(i=0;i<ENEMY_CAPACITY;i++){u32 m=enemy_scene_instance_material(i);missile_due[i]=missile_after(now,missile_emitter_ms[m==3u]);}
 return missile_scene_collect();
}
gpu_result_t missile_scene_advance_motion(u64 now){
 u32 i;u64 dt;if(!missile_enabled)return GPU_OK;if(missile_stage)return GPU_ERR_BUSY;
 dt=now>=missile_last_tick?now-missile_last_tick:0;missile_last_tick=now;if(dt/BSP_CLINT_HZ>=8u)dt=(u64)BSP_CLINT_HZ*8u;
 for(i=0;i<MISSILE_CAPACITY;i++)if(missile_pool[i].flags&MS_ACTIVE){
  missile_state_t *p=&missile_pool[i];int64_t ny=(int64_t)p->y_q16+((int64_t)p->vy*65536*(int64_t)dt)/(int64_t)BSP_CLINT_HZ;
  missile_previous_x[i]=(int16_t)missile_floor(p->x_q16);missile_previous_y[i]=(int16_t)missile_floor(p->y_q16);
  p->flags=MS_ACTIVE|MS_MOVED;if(ny>=(int64_t)1080*65536){ny=(int64_t)1080*65536;p->flags|=MS_RETIRE;}p->y_q16=(int32_t)ny;
 }return GPU_OK;
}
static void missile_spawn(u32 emitter,u32 t,const enemy_object_desc_t *plane){
 u32 i,n=0,free=MISSILE_CAPACITY;int x=(int)(plane->word[0]&65535u)+(int)(plane->word[2]&65535u)/2-(int)missile_widths[t]/2;
 int y=(int)(plane->word[0]>>16)+(int)(plane->word[2]>>16);(void)emitter;
 if(!t && !missile_blue_enabled){missile_blue_suppressed++;return;}
 for(i=0;i<MISSILE_CAPACITY;i++)if(missile_pool[i].flags&MS_ACTIVE)n+=missile_pool[i].type==t;else if(free==MISSILE_CAPACITY)free=i;
 if(n>=MISSILE_TYPE_CAPACITY || free==MISSILE_CAPACITY || x<=-(int)missile_widths[t] || x>=1920 || y<=-32 || y>=1080){missile_dropped[t]++;return;}
 missile_pool[free]=(missile_state_t){x*65536,y*65536,0,(int16_t)missile_speeds[t],MS_ACTIVE,(uint8_t)t,0};missile_spawned[t]++;
}
gpu_result_t missile_scene_advance_fire(u64 now){
 u32 i,t,tries,count;if(!missile_enabled)return GPU_OK;if(missile_stage)return GPU_ERR_BUSY;
 for(i=0;i<MISSILE_CAPACITY;i++)if(missile_pool[i].flags&MS_RETIRE)missile_pool[i].flags=0;
 count=enemy_scene_instance_count();
 for(i=0;i<count;i++)if(enemy_scene_emitter_restarted(i)){u32 m=enemy_scene_instance_material(i);missile_due[i]=missile_after(now,missile_emitter_ms[m==3u]);}
 if(!missile_fire_enabled || !count || now<missile_shared_due)return GPU_OK;
 for(i=0;i<2;i++){
  t=(missile_next_type+i)&1u;if(now<missile_type_due[t])continue;
  for(tries=0;tries<count;tries++){
   u32 e=(missile_priority[t]+tries)%count;enemy_object_desc_t plane;
   if(enemy_scene_instance_material(e)!=t+2u || enemy_scene_emitter_restarted(e) || now<missile_due[e] || !enemy_scene_pending_emitter(e,&plane))continue;
   missile_priority[t]=(e+1u)%count;missile_due[e]=missile_after(now,missile_emitter_ms[t]);missile_type_due[t]=missile_after(now,missile_type_ms[t]);
   missile_shared_due=missile_after(now,MISSILE_SHARED_PERIOD_MS);missile_next_type=t^1u;missile_spawn(e,t,&plane);return GPU_OK;
  }
 }return GPU_OK;
}
int missile_scene_active_box(u32 slot,combat_box_t *b){
 missile_state_t *p;const uint8_t *h;int x,y;if(!missile_enabled || slot>=MISSILE_CAPACITY || !b)return 0;p=&missile_pool[slot];if(!(p->flags&MS_ACTIVE))return 0;
 h=missile_inners[p->type];x=missile_floor(p->x_q16);y=missile_floor(p->y_q16);b->x0=x+h[0];b->x1=x+h[2];b->y0=y+h[1];b->y1=y+h[3];
 b->previous_x0=missile_previous_x[slot]+h[0];b->previous_y0=missile_previous_y[slot]+h[1];b->continuous=(p->flags&MS_MOVED)!=0;return 1;
}
void missile_scene_kill(u32 slot){if(!missile_stage && slot<MISSILE_CAPACITY)missile_pool[slot].flags=0;}
int missile_scene_stage_service(void){
 gpu_result_t r;u32 n;if(!missile_enabled)return 1;
 if(missile_stage==1){r=gpu_missile_scene_open(missile_count,missile_sequence);if(r!=GPU_OK)return -1;missile_stage=2;return 0;}
 if(missile_stage==2){for(n=0;n<2 && missile_position<missile_count;n++,missile_position++){r=gpu_missile_scene_write(missile_pending[missile_position].word);if(r!=GPU_OK)return -1;}if(missile_position==missile_count)missile_stage=3;return 0;}
 if(missile_stage==3){r=gpu_missile_scene_seal(missile_count,missile_sequence);if(r!=GPU_OK)return -1;missile_stage=4;}return missile_stage==4?1:-1;
}
/* S (PIPE ON): snapshot at claim. */
static missile_object_desc_t missile_if[MISSILE_CAPACITY];static u32 missile_if_stats[2][4],missile_if_count,missile_if_valid;
void missile_scene_claimed(void){if(missile_enabled && missile_stage==4){memcpy(missile_if,missile_pending,missile_count*sizeof(*missile_pending));
 memcpy(missile_if_stats,missile_stats,sizeof(missile_stats));missile_if_count=missile_count;missile_if_valid=1;missile_stage=0;}}
void missile_scene_confirmed(void){if(missile_if_valid){memcpy(missile_display,missile_if,missile_if_count*sizeof(*missile_if));
 memcpy(missile_display_stats,missile_if_stats,sizeof(missile_display_stats));missile_if_valid=0;return;}
 if(missile_enabled && missile_stage==4){memcpy(missile_display,missile_pending,missile_count*sizeof(*missile_pending));memcpy(missile_display_stats,missile_stats,sizeof(missile_stats));missile_stage=0;}}
void missile_scene_cancel(void){if(missile_enabled)gpu_missile_scene_cancel();missile_stage=0;missile_if_valid=0;}
gpu_result_t missile_scene_disable(void){return gpu_missile_scene_mode(0);}
void missile_scene_restored(void){missile_if_valid=0;memset(missile_pool,0,sizeof(missile_pool));memset(missile_display_stats,0,sizeof(missile_display_stats));}
u32 missile_scene_displayed_type(u32 type,u32 field){return type<2 && field<4?missile_display_stats[type][field]:0;}
u32 missile_scene_displayed_active(void){return missile_display_stats[0][0]+missile_display_stats[1][0];}
u32 missile_scene_displayed_visible(void){return missile_display_stats[0][1]+missile_display_stats[1][1];}
u32 missile_scene_displayed_spawned(void){return missile_display_stats[0][2]+missile_display_stats[1][2];}
u32 missile_scene_displayed_dropped(void){return missile_display_stats[0][3]+missile_display_stats[1][3];}
u32 missile_scene_blue_enabled(void){return missile_blue_enabled;}
u32 missile_scene_blue_suppressed(void){return missile_blue_suppressed;}
