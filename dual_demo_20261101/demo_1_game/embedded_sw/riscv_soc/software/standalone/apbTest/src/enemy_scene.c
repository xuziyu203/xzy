#include "enemy_scene.h"
#include "bsp.h"
#include <string.h>

/* When dead, fraction holds remaining respawn game ticks. reset_time only
 * rebases last_tick, so paused wall time never consumes the countdown. */
typedef struct { u32 x,y,speed,material;u64 fraction,last_tick;
 u32 previous_y;uint16_t hp;uint8_t alive,continuous; } enemy_instance_t;
typedef char enemy_descriptor_is_16_bytes[(sizeof(enemy_object_desc_t)==16)?1:-1];
typedef char enemy_instance_is_40_bytes[(sizeof(enemy_instance_t)==40)?1:-1];
static enemy_instance_t instances[ENEMY_CAPACITY];
static enemy_object_desc_t pending[ENEMY_CAPACITY],displayed[ENEMY_CAPACITY];
static const u32 materials[ENEMY_MATERIAL_COUNT][4]=ENEMY_MATERIAL_VALUES;
static const uint8_t hitboxes[ENEMY_MATERIAL_COUNT][4]=ENEMY_HITBOX_VALUES;
static const uint16_t initial_hp[ENEMY_MATERIAL_COUNT]=ENEMY_HP_VALUES;
static const u32 respawn_ms[ENEMY_MATERIAL_COUNT]=ENEMY_RESPAWN_MS_VALUES;
/* S0: rows stored once; wave_values[w] points at wave w (same indexing as the former padded array). */
static const uint16_t wave_rows[WAVE_ROWS][4]=WAVE_VALUES;
static const uint16_t (*const wave_values[WAVE_COUNT])[4]=WAVE_TABLES;
/* S1: RICH role of every wave row (0 = by material); lane guard only on RICH waves. */
static const uint8_t wave_roles[WAVE_ROWS]=WAVE_ROLE_VALUES;
static u32 lane_guard;
static const uint8_t wave_sizes[WAVE_COUNT]=WAVE_SIZES;
static const char *const wave_names[WAVE_COUNT]=WAVE_NAMES;
static u64 respawn_ticks[ENEMY_MATERIAL_COUNT];
static u32 selected,pending_count,displayed_count,pending_enabled,stage,position,sequence;
static u32 instance_count,displayed_slots;
static u32 pending_boss_hp,displayed_boss_hp,respawn_enabled=1;

void enemy_scene_init(void){selected=0;respawn_enabled=1;pending_count=displayed_count=0;stage=position=0;instance_count=displayed_slots=displayed_boss_hp=pending_boss_hp=0;}
u32 enemy_scene_selected(void){return selected;}
const char *enemy_scene_name(void){return selected?wave_names[selected-1u]:"SINGLE";}
u32 enemy_scene_displayed_objects(void){return displayed_count;}
u32 enemy_scene_required_assets(void){return selected?0x3FEu:14u;}
gpu_result_t enemy_scene_select(u32 scene){
 u32 cap;gpu_result_t r;
 if(scene>ENEMY_SCENE_LAST)return GPU_ERR_PARAM;
 if(stage)return GPU_ERR_BUSY;
 if(scene){r=gpu_enemy_scene_capacity(&cap);if(r!=GPU_OK)return r;
  if(cap<wave_sizes[scene-1u] || ENEMY_CAPACITY<wave_sizes[scene-1u])return GPU_ERR_UNSUPPORTED;}
 selected=scene;return GPU_OK;
}
gpu_result_t enemy_scene_submit(const enemy_object_desc_t *objects,u32 count){
 u32 cap,i,previous=0,enabled=0;gpu_result_t r;
 if(stage)return GPU_ERR_BUSY;
 if(count>ENEMY_CAPACITY || (count && !objects))return GPU_ERR_PARAM;
 r=gpu_enemy_scene_capacity(&cap);if(r!=GPU_OK)return r;
 if(count>cap)return GPU_ERR_PARAM;
 for(i=0;i<count;i++){
  /* ME1.2: the table must be staged in non-decreasing y. */
  const u32 *d=objects[i].word;u32 mat=d[1]&255u,w=d[2]&65535u,h=d[2]>>16,y=d[0]>>16;
  if((d[1]&~0x1ffu) || mat>=ENEMY_MATERIAL_COUNT || w!=materials[mat][2] || h!=materials[mat][3] ||
     (d[0]&65535u)+w>1920u || y+h>1080u || (i && y<previous))return GPU_ERR_PARAM;
  previous=y;if(d[1]&256u)enabled++;
 }
 if(count && objects!=pending)memcpy(pending,objects,count*sizeof(*objects));
 pending_count=count;pending_enabled=enabled;position=0;
 sequence=gpu_enemy_scene_frame_sequence()+1u;stage=1;return GPU_OK;
}
gpu_result_t enemy_scene_begin(void){
 u32 i;gpu_result_t r=gpu_enemy_scene_mode(selected!=0);
 if(r!=GPU_OK)return r;
 displayed_count=displayed_slots=displayed_boss_hp=pending_boss_hp=instance_count=0;lane_guard=0;if(!selected)return GPU_OK;
 memset(instances,0,sizeof(instances));displayed_count=0;
 for(i=0;i<ENEMY_MATERIAL_COUNT;i++)respawn_ticks[i]=(u64)BSP_CLINT_HZ*respawn_ms[i]/1000u;
 instance_count=wave_sizes[selected-1u];respawn_enabled=1;lane_guard=(WAVE_RICH_MASK>>(selected-1u))&1u;
 for(i=0;i<instance_count;i++){
  const uint16_t *w=wave_values[selected-1u][i];
  instances[i].x=w[0];instances[i].y=w[1];instances[i].material=w[2];instances[i].speed=w[3];
  instances[i].hp=initial_hp[instances[i].material];instances[i].alive=1;
  instances[i].previous_y=instances[i].y;
 }
 enemy_scene_reset_time(clint_getTime(BSP_CLINT));
 return enemy_scene_collect();
}
gpu_result_t enemy_scene_phase_begin(void){
 u32 count=displayed_count,slots=displayed_slots,hp=displayed_boss_hp;
 gpu_result_t r=enemy_scene_begin();
 displayed_count=count;displayed_slots=slots;displayed_boss_hp=hp;return r;
}
int enemy_scene_stage_service(void){
 gpu_result_t r;u32 n;
 if(!selected)return 1;
 if(stage==1){r=gpu_enemy_scene_open(pending_count,sequence);if(r!=GPU_OK)return -1;stage=2;return 0;}
 if(stage==2){
  /* Four descriptors = sixteen APB data writes at most per service call. */
  for(n=0;n<4 && position<pending_count;n++,position++){
   r=gpu_enemy_scene_write(pending[position].word);if(r!=GPU_OK)return -1;
  }
  if(position==pending_count)stage=3;
  return 0;
 }
 if(stage==3){r=gpu_enemy_scene_seal(pending_count,sequence);if(r!=GPU_OK)return -1;stage=4;}
 return stage==4?1:-1;
}
/* S (PIPE ON): snapshot of the claimed frame; enemy_scene_confirmed() publishes it. Never set with PIPE OFF. */
static enemy_object_desc_t inflight[ENEMY_CAPACITY];
static u32 inflight_valid,inflight_count,inflight_slots,inflight_boss_hp;
void enemy_scene_claimed(void){
 if(selected && stage==4){if(pending_count)memcpy(inflight,pending,pending_count*sizeof(*pending));
  inflight_count=pending_enabled;inflight_slots=pending_count;inflight_boss_hp=pending_boss_hp;inflight_valid=1;stage=0;}
}
void enemy_scene_confirmed(void){
 if(inflight_valid){if(inflight_slots)memcpy(displayed,inflight,inflight_slots*sizeof(*inflight));
  displayed_count=inflight_count;displayed_slots=inflight_slots;displayed_boss_hp=inflight_boss_hp;inflight_valid=0;return;}
 if(!selected){displayed_count=1;return;}
 if(stage==4){if(pending_count)memcpy(displayed,pending,pending_count*sizeof(*pending));
  displayed_count=pending_enabled;displayed_slots=pending_count;
  displayed_boss_hp=pending_boss_hp;stage=0;}
}
void enemy_scene_cancel(void){
 if(selected)gpu_enemy_scene_cancel();
 stage=0;inflight_valid=0;
}

void enemy_scene_restored(void){inflight_valid=0;displayed_count=displayed_slots=displayed_boss_hp=pending_boss_hp=instance_count=0;}

void enemy_scene_reset_time(u64 now){
 u32 i;for(i=0;i<instance_count;i++)instances[i].last_tick=now;
}
/* Lane guard (RICH waves): a respawn at y=0 waits while a live plane of the same lane is within
 * size+gap of the top or of the bottom, so two planes of one lane never overlap (same speed). */
static int enemy_lane_clear(u32 i){
 u32 j,s=materials[instances[i].material][3],g=s+RICH_LANE_GUARD_GAP,b=1080u-s;
 for(j=0;j<instance_count;j++)if(j!=i && instances[j].alive && instances[j].x==instances[i].x && (instances[j].y<g || instances[j].y+g>b))return 0;
 return 1;
}
gpu_result_t enemy_scene_advance(u64 now){
 u32 i;
 if(!selected)return GPU_OK;
 if(stage)return GPU_ERR_BUSY;
 /* Called only after the preceding frame was confirmed and its owner drained.
  * The complete bottom and top are each submitted, even after a long stall. */
 for(i=0;i<instance_count;i++){
  enemy_instance_t *p=&instances[i];u32 bottom=1080u-materials[p->material][3];
  u64 dt=now>=p->last_tick?now-p->last_tick:0;
  p->last_tick=now;p->previous_y=p->y;p->continuous=1;
  if(!p->alive){
   if(!respawn_enabled)continue;
   if(dt<p->fraction){p->fraction-=dt;continue;}
   if(lane_guard && !enemy_lane_clear(i)){p->fraction=0;continue;}
   p->alive=1;p->hp=initial_hp[p->material];p->y=p->previous_y=0;
   p->fraction=0;p->continuous=2;continue;
  }
  {
  u64 scaled=(dt%BSP_CLINT_HZ)*p->speed+p->fraction;
  /* Only screen-sized motion matters; bound before multiplying whole seconds. */
  u64 pixels=dt/BSP_CLINT_HZ>=1080u?1080u:(dt/BSP_CLINT_HZ)*p->speed+scaled/BSP_CLINT_HZ;
  p->fraction=scaled%BSP_CLINT_HZ;
  if(p->speed){
   if(p->y==bottom){p->y=p->previous_y=0;p->fraction=0;p->continuous=0;}
   else if(pixels>=bottom-p->y){p->y=bottom;p->fraction=0;}
   else p->y+=(u32)pixels;
  }
  }
 }
 return GPU_OK;
}
static void enemy_scene_descriptor(u32 index,enemy_object_desc_t *d){
 enemy_instance_t *p=&instances[index];u32 w=materials[p->material][2],h=materials[p->material][3];
 d->word[0]=(p->y<<16)|p->x;d->word[1]=p->material|256u;
 d->word[2]=(h<<16)|w;d->word[3]=(index<<16)|index;
}
gpu_result_t enemy_scene_collect(void){
 u32 i,n=0,k;if(!selected)return GPU_OK;if(stage)return GPU_ERR_BUSY;
 pending_boss_hp=0;
 for(i=0;i<instance_count;i++){
  if(instances[i].material==ENEMY_MATERIAL_COUNT-1u && !pending_boss_hp)pending_boss_hp=instances[i].hp;
  if(!instances[i].alive)continue;
  /* Stable insertion by y: the RTL scans each row through a sorted window. */
  for(k=n;k>0 && (pending[k-1u].word[0]>>16)>instances[i].y;k--)pending[k]=pending[k-1u];
  enemy_scene_descriptor(i,&pending[k]);n++;
 }
 return enemy_scene_submit(pending,n);
}
gpu_result_t enemy_scene_next(u64 now){
 gpu_result_t r=enemy_scene_advance(now);return r==GPU_OK?enemy_scene_collect():r;
}
int enemy_scene_displayed(u32 index,enemy_object_desc_t *object){
 u32 i;if(!object)return 0;
 for(i=0;i<displayed_slots;i++)if((displayed[i].word[3]&65535u)==index){*object=displayed[i];return 1;}
 return 0;
}

int enemy_scene_pending_emitter(u32 index,enemy_object_desc_t *object){
 if(!selected || !object || index>=instance_count || !instances[index].alive)return 0;
 enemy_scene_descriptor(index,object);return 1;
}
u32 enemy_scene_instance_count(void){return selected?instance_count:0;}
u32 enemy_scene_instance_role(u32 index){
 static const uint8_t by_material[ENEMY_MATERIAL_COUNT]=RICH_ROLE_BY_MATERIAL;u32 r;
 if(!selected || index>=instance_count)return 0;
 r=wave_roles[(u32)(wave_values[selected-1u]-wave_rows)+index];return r?r:by_material[instances[index].material];
}
u32 enemy_scene_lane_guard(void){return lane_guard;}
u32 enemy_scene_instance_material(u32 index){return index<instance_count?instances[index].material:0u;}
int enemy_scene_instance_box(u32 index,combat_box_t *b){
 enemy_instance_t *p;const uint8_t *h;
 if(!b || !selected || index>=instance_count)return 0;
 p=&instances[index];if(!p->alive)return 0;h=hitboxes[p->material];
 b->x0=(int32_t)p->x+h[0];b->x1=(int32_t)p->x+h[2];
 b->y0=(int32_t)p->y+h[1];b->y1=(int32_t)p->y+h[3];
 b->previous_x0=b->x0;b->previous_y0=(int32_t)p->previous_y+h[1];b->continuous=p->continuous==1;
 return 1;
}
int enemy_scene_instance_center(u32 index,int32_t *cx,int32_t *cy){
 enemy_instance_t *p;u32 s;
 if(!selected || index>=instance_count || !cx || !cy)return 0;
 p=&instances[index];s=materials[p->material][2];
 *cx=(int32_t)(p->x+s/2u);*cy=(int32_t)(p->y+s/2u);return 1;
}
int enemy_scene_hit(u32 index,u32 damage,u64 now){
 enemy_instance_t *p;
 if(!selected || stage || index>=instance_count || !damage)return 0;
 p=&instances[index];if(!p->alive)return 0;
 if(damage<p->hp){p->hp=(uint16_t)(p->hp-damage);return 0;}
 p->hp=p->alive=0;p->last_tick=now;p->fraction=respawn_ticks[p->material];
 return 1;
}
void enemy_scene_set_respawn(u32 enable){respawn_enabled=enable!=0;}
int enemy_scene_emitter_restarted(u32 index){return index<instance_count && instances[index].alive && instances[index].continuous==2;}
u32 enemy_scene_alive_count(void){return displayed_count;}
u32 enemy_scene_boss_hp(void){return displayed_boss_hp;}
