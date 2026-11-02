/* BGT-A decorative turret shots; see decor_scene.h. Parameters come from config/enemy_scene.json "decor"
 * through tools/generate_enemy_config.py (DECOR_* in enemy_config.h). */
#include "decor_scene.h"
#include "enemy_config.h"
#include "bsp.h"
#include "io.h"
#include "decor_extent.h"

#define DECOR_REG_ABI    0x380u
#define DECOR_REG_ENABLE 0x384u
#define DECOR_REG_SLOT0  0x388u
#define DECOR_REG_ERROR  0x3ACu
#define DECOR_REG_SLOT8  0x3B0u /* CMD: slots 8..15 */
#define DECOR_REG_CAP    0x3D0u /* CMD: 16; an older bit reads 0 -> 8 slots */
#define DECOR_ABI        0x42540101u
#define DECOR_HALF       ((int32_t)DECOR_SIZE/2)

typedef struct { int32_t cx,cy; uint16_t age; uint8_t live,turret; int8_t vx,vy; uint8_t edge; } decor_shot_t; /* GHDA: edge = glow shot */
/* GHD4 light-mode glow shots: edge mode (see decor_scene_edge_mode) */
#define DECOR_EDGE_MAX 10u
static u32 decor_edge,edge_next,edge_side,edge_lcg=0x2468ACE1u;
static u32 edge_rand(u32 n){edge_lcg=edge_lcg*1664525u+1013904223u;return (u32)(((u64)(edge_lcg>>8)*n)>>24);}

static decor_shot_t shots[DECOR_SLOTS];
static const int16_t muzzle[DECOR_TURRET_COUNT][2][2]=DECOR_MUZZLE_VALUES;
static const int8_t velocity[DECOR_TURRET_COUNT][2]=DECOR_VELOCITY_VALUES;
static const uint8_t skin_of[DECOR_TURRET_COUNT]=DECOR_SKIN_VALUES;
static const uint16_t first_tick[2][DECOR_TURRET_COUNT]={DECOR_FIRST_TICKS,DECOR_FIRST_TICKS_2X};
static const uint16_t period_ticks[2]={DECOR_PERIOD_TICKS,DECOR_PERIOD_TICKS_2X};
static const uint8_t max_active[2]={DECOR_MAX_ACTIVE,DECOR_MAX_ACTIVE_2X};
static u32 decor_rate=1u,decor_hw_slots=8u,decor_peak; /* CMD: 0 = 1X, 1 = 2X (default) */
static u32 decor_max(void){u32 m=max_active[decor_rate];return m<decor_hw_slots?m:decor_hw_slots;}
static u32 decor_slot_reg(u32 i){return i<8u?DECOR_REG_SLOT0+4u*i:DECOR_REG_SLOT8+4u*(i-8u);}
static void decor_hw_probe(void){decor_hw_slots=read_u32(IO_APB_SLAVE_0_INPUT+DECOR_REG_CAP)==16u?16u:8u;if(decor_hw_slots<16u)decor_rate=0;}
static const int16_t recycle[4]=DECOR_RECYCLE_CENTER;   /* centre must stay inside (x0,y0)..(x1,y1), open */
static u32 decor_selected,decor_written_enable;
static u64 decor_t0;
static u32 decor_ticks,next_fire[DECOR_TURRET_COUNT];
static uint8_t next_muzzle[DECOR_TURRET_COUNT];
static u32 decor_spawned,decor_skipped,decor_retired,pend_active,disp_active,disp_frames;
static u32 pend_visible,disp_visible; /* GHD: shots with at least one drawn pixel (a4>0) on screen */

static void decor_write(u32 reg,u32 value){write_u32(value,IO_APB_SLAVE_0_INPUT+reg);}
u32 decor_scene_hw_supported(void){return read_u32(IO_APB_SLAVE_0_INPUT+DECOR_REG_ABI)==DECOR_ABI;}

void decor_scene_init(void){u32 i;decor_selected=0;decor_written_enable=0;for(i=0;i<DECOR_SLOTS;i++)shots[i].live=0;}
gpu_result_t decor_scene_select(u32 enable){
 if(enable>1u)return GPU_ERR_PARAM;
 if(enable && !decor_scene_hw_supported())return GPU_ERR_UNSUPPORTED;
 decor_selected=enable;return GPU_OK;
}
u32 decor_scene_selected(void){return decor_selected;}
gpu_result_t decor_scene_rate(u32 two_x){if(two_x>1u)return GPU_ERR_PARAM;decor_hw_probe();if(two_x && decor_hw_slots<16u)return GPU_ERR_UNSUPPORTED;decor_rate=two_x;return GPU_OK;}
u32 decor_scene_rate_2x(void){return decor_rate;}
/* GHD4: 1 = light-mode edge glow shots, 0 = pressure turret shots (default); takes effect at the next decor_scene_begin */
void decor_scene_edge_mode(u32 on){decor_edge=on!=0;}
u32 decor_scene_edge(void){return decor_edge;}
u32 decor_scene_required_assets(void){
 static const uint8_t ids[2]=DECOR_SKIN_IDS;
 return decor_selected?((1u<<ids[0])|(1u<<ids[1])):0u;
}

static u32 decor_if_active,decor_if_valid;
void decor_scene_begin(u64 now){
 u32 i;
 for(i=0;i<DECOR_SLOTS;i++)shots[i].live=0;
 decor_hw_probe();
 for(i=0;i<DECOR_TURRET_COUNT;i++){next_fire[i]=first_tick[decor_rate][i];next_muzzle[i]=0;}
 edge_next=12u;edge_side=0;
 decor_t0=now;decor_ticks=0;decor_spawned=decor_skipped=decor_retired=pend_active=disp_active=disp_frames=decor_peak=0;decor_if_valid=0;
 pend_visible=disp_visible=0;
}
/* Keep the processed tick count across STOP: tick k is at decor_t0 + ceil(k*HZ/60). */
void decor_scene_reset_time(u64 now){
 u64 span=((u64)decor_ticks*BSP_CLINT_HZ+59u)/60u;
 decor_t0=now>=span?now-span:0;
}
static void decor_spawn(u32 t){
 u32 i,m=next_muzzle[t];
 next_muzzle[t]=(uint8_t)(m^1u);
 for(i=0;i<decor_max();i++)if(!shots[i].live)break;
 if(i==decor_max()){decor_skipped++;return;}
 shots[i].cx=muzzle[t][m][0];shots[i].cy=muzzle[t][m][1];shots[i].age=0;shots[i].turret=(uint8_t)t;shots[i].live=1;
 shots[i].vx=velocity[t][0];shots[i].vy=velocity[t][1];shots[i].edge=0;
 decor_spawned++;
}
/* GHD4 edge glow shot spawner; GHDA: also used in pressure (turret mode) for up to decor_glow_extra glow shots (edge_only counts only
 * glow shots against the cap; light mode counts every live shot as before, same RNG sequence). */
static u32 decor_glow_extra;
static void decor_edge_spawn(u32 cap,u32 edge_only){
 u32 i;
 if(decor_ticks<edge_next)return;
 {
  /* edge glow shot: enters from outside the left/right edge (centre -15 / 1935 keeps the top-left inside the RTL range) */
  u32 live=0,slot=DECOR_SLOTS;
  for(i=0;i<decor_max();i++){if(shots[i].live){if(!edge_only || shots[i].edge)live++;}else if(slot==DECOR_SLOTS)slot=i;}
  edge_next=decor_ticks+18u+edge_rand(25u);
  if(live<cap && slot<DECOR_SLOTS){
   decor_shot_t *s=&shots[slot];u32 right;
   if(edge_rand(4u)!=0u)edge_side^=1u;right=edge_side;
   s->cx=right?1935:-15;s->cy=120+(int32_t)edge_rand(701u);
   s->vx=(int8_t)(right?-(int32_t)(4u+edge_rand(3u)):(int32_t)(4u+edge_rand(3u)));s->vy=(int8_t)((int32_t)edge_rand(3u)-1);
   s->age=0;s->turret=(uint8_t)(right?1u:0u);s->live=1;s->edge=1;decor_spawned++;
  }else decor_skipped++;
 }
}
static void decor_tick(void){
 u32 i,t,n=0;
 for(i=0;i<DECOR_SLOTS;i++)if(shots[i].live){
  decor_shot_t *s=&shots[i];
  s->cx+=s->vx;s->cy+=s->vy;s->age++;
  if(s->age>=DECOR_LIFE_TICKS || s->cx<=recycle[0] || s->cy<=recycle[1] || s->cx>=recycle[2] || s->cy>=recycle[3]){s->live=0;decor_retired++;}
 }
 if(!decor_edge){for(t=0;t<DECOR_TURRET_COUNT;t++)if(decor_ticks==next_fire[t]){decor_spawn(t);next_fire[t]+=period_ticks[decor_rate];}
  if(decor_glow_extra)decor_edge_spawn(decor_glow_extra,1u);} /* GHDA: pressure glow shots */
 else decor_edge_spawn(DECOR_EDGE_MAX,0u);
 for(i=0;i<DECOR_SLOTS;i++)n+=shots[i].live;if(n>decor_peak)decor_peak=n;
 decor_ticks++;
}
void decor_scene_advance(u64 now){
 u64 target=now>decor_t0?((now-decor_t0)*60u)/BSP_CLINT_HZ:0;
 u32 n=0;
 if(!decor_selected)return;
 /* At most one second of ticks per frame; a longer gap (STOP is handled by reset_time) is not replayed. */
 while(decor_ticks<target && n<60u){decor_tick();n++;}
 if(decor_ticks<target)decor_scene_reset_time(now);
}
int decor_scene_slot_word(u32 slot,u32 *word){
 const decor_shot_t *s;int32_t x,y;
 if(slot>=DECOR_SLOTS || !word)return -1;
 s=&shots[slot];
 if(!decor_selected || slot>=decor_max() || !s->live){*word=0;return 0;}
 x=s->cx-DECOR_HALF;y=s->cy-DECOR_HALF;
 /* Recycle bounds keep the top-left inside the RTL range x -31..1919, y -31..1079. */
 if(x<-31 || x>1919 || y<-31 || y>1079){*word=0;return -1;}
 *word=0x80000000u|((u32)skin_of[s->turret]<<30)|(((u32)y&0xfffu)<<12)|((u32)x&0xfffu);
 return 1;
}
/* GHD: one written slot word = one turret shot; it is displayed when a drawn pixel of its skin (decor_extent.h rows) is on screen. */
u32 decor_word_visible(u32 w){
 int32_t x,y;u32 skin,r;
 if(!(w&0x80000000u))return 0;
 skin=(w>>30)&1u;x=(int32_t)((w&0xfffu)^0x800u)-0x800;y=(int32_t)(((w>>12)&0xfffu)^0x800u)-0x800;
 if(x>=0 && x<=1920-32 && y>=0 && y<=1080-32)return 1; /* whole sprite on screen; both skins have drawn rows */
 for(r=0;r<32;r++){
  int32_t yy=y+(int32_t)r;const uint8_t *e=decor_extent[skin][r];
  if(yy<0 || yy>1079 || e[0]>e[1])continue;
  if(x+(int32_t)e[0]<=1919 && x+(int32_t)e[1]>=0)return 1;
 }
 return 0;
}
void decor_scene_collect(void){
 u32 i,w,n=0,v=0;
 if(decor_selected!=decor_written_enable){decor_write(DECOR_REG_ENABLE,decor_selected);decor_written_enable=decor_selected;}
 if(!decor_selected && !decor_scene_hw_supported())return;
 for(i=0;i<decor_hw_slots;i++){if(decor_scene_slot_word(i,&w)>0){n++;v+=decor_word_visible(w);}decor_write(decor_slot_reg(i),w);}
 pend_active=n;pend_visible=v;
}
/* S (PIPE ON): snapshot at claim (state declared above decor_scene_begin) */
static u32 decor_if_visible;
void decor_scene_claimed(void){decor_if_active=pend_active;decor_if_visible=pend_visible;decor_if_valid=1;}
void decor_scene_confirmed(void){if(decor_if_valid){disp_active=decor_if_active;disp_visible=decor_if_visible;decor_if_valid=0;disp_frames++;return;}
 disp_active=pend_active;disp_visible=pend_visible;disp_frames++;}
u32 decor_scene_displayed_visible(void){return disp_visible;}
void decor_scene_disable(void){
 u32 i;
 if(!decor_scene_hw_supported())return;
 decor_write(DECOR_REG_ENABLE,0);decor_written_enable=0;
 decor_hw_probe();for(i=0;i<decor_hw_slots;i++)decor_write(decor_slot_reg(i),0);
}
void decor_scene_status(void (*text)(const char *),void (*dec)(u64)){
 text("ENEMY DECOR ABI=");text(decor_scene_hw_supported()?"42540101":"00000000");
 text(" ENABLED=");dec(decor_selected);text(" ACTIVE=");dec(disp_active);text(" VISIBLE=");dec(disp_visible);text(" FRAMES=");dec(disp_frames);
 text(" TICKS=");dec(decor_ticks);text(" SPAWNED=");dec(decor_spawned);text(" SKIP_FULL=");dec(decor_skipped);
 text(" RETIRED=");dec(decor_retired);text(" MAX_ACTIVE=");dec(decor_max());text(" RATE=");text(decor_rate?"2X":"1X");
 text(" PERIOD_TICKS=");dec(period_ticks[decor_rate]);text(" SLOTS_HW=");dec(decor_hw_slots);text(" PEAK_ACTIVE=");dec(decor_peak);
 text(" HW_ERROR=");dec(decor_scene_hw_supported()?read_u32(IO_APB_SLAVE_0_INPUT+DECOR_REG_ERROR):0u);text("\r\n");
}
void decor_scene_pressure_glow(u32 k){decor_glow_extra=k>4u?4u:k;}
u32 decor_scene_glow_extra(void){return decor_glow_extra;}
u32 decor_scene_live_edge(void){u32 i,n=0;for(i=0;i<DECOR_SLOTS;i++)n+=shots[i].live && shots[i].edge;return n;}
u32 decor_scene_live_mask(void){u32 i,m=0;for(i=0;i<DECOR_SLOTS;i++)if(shots[i].live)m|=1u<<i;return m;}
u32 decor_scene_ticks(void){return decor_ticks;}
u32 decor_scene_spawned(void){return decor_spawned;}
u32 decor_scene_skipped(void){return decor_skipped;}
u32 decor_scene_retired(void){return decor_retired;}
