#include "bullet_scene.h"
/* GHD5: LIGHT fire period scaling (definition with the density tiers) */
static u64 legacy_ticks(u32 ms);
#include "enemy_scene.h"
#include "asset_upload.h"
#include "bsp.h"
#include "game.h"
#include <string.h>

/* Q16 position and per-bullet velocity (pixels/second, signed). type is
 * the configured style of the actual plane material. Straight shots have vx=0; fan
 * shots carry a fixed horizontal component, so collision is a 2D sweep. */
typedef struct { int32_t x_q16,y_q16;int16_t vx,vy;uint8_t flags,type;uint16_t pad; } bullet_state_t;
typedef char bullet_state_is_16_bytes[(sizeof(bullet_state_t)==16)?1:-1];
typedef char bullet_descriptor_is_8_bytes[(sizeof(bullet_object_desc_t)==8)?1:-1];
enum { BULLET_ACTIVE=1, BULLET_MOVED=2, BULLET_RETIRE=4 };
enum { BULLET_STAGE_BATCH=8 };
static bullet_state_t bullet_pool[BULLET_CAPACITY];
/* H: every slot >= bullet_high_water is inactive. It rises only when spawn
 * takes the lowest free slot and falls only when the whole pool is cleared,
 * so loops below it visit every live group in the same slot order. */
static u32 bullet_high_water;
static void bullet_pool_clear(void);
static int16_t bullet_previous_x[BULLET_CAPACITY],bullet_previous_y[BULLET_CAPACITY];
static uint8_t bullet_hitbox[BULLET_STYLE_COUNT][4];
/* A: the confirmed table is identified by its sequence and count (D); no
 * code ever read the former full descriptor copy of it.
 * M1: the pending table holds pool slot indices in submission (rank) order; the wire descriptor of
 * rank k is rebuilt from the frozen pool slot at stage time (bullet_desc). */
#define BULLET_PENDING_INDEX 1
static uint16_t bullet_pending[BULLET_CAPACITY];
static u64 bullet_last_tick,bullet_due[ENEMY_CAPACITY],bullet_global_due;
static u32 bullet_enabled,bullet_stage,bullet_position,bullet_sequence,bullet_priority;
static u32 bullet_pending_count,bullet_pending_active,bullet_pending_visible;
static u32 bullet_display_count,bullet_display_active,bullet_display_visible;
static u32 bullet_spawned,bullet_dropped,bullet_skipped,bullet_fan_spawned;
static u32 bullet_display_spawned,bullet_display_dropped,bullet_display_skipped,bullet_display_fan;
static u32 bullet_stress_target,bullet_fire_enabled=1;
static u32 bullet_pattern_mode,bullet_pending_groups,bullet_display_groups,bullet_spawn_groups;
static u32 bullet_lcg=0x12345678u;
/* S1 RICH (pattern 3): role in pad[10:8]; options, schedule state and confirmed diagnostics. */
enum { BULLET_ROLE_SHIFT=8, BULLET_ROLE_MASK=7u<<8 };
typedef struct { u32 events,deferred_budget,deferred_pool,deferred_reserve,late,cease,max_wait,peak_groups,deferred_quota; } rich_stats_t;
static rich_stats_t rich_stats,rich_display_stats;
static uint8_t rich_wait[ENEMY_CAPACITY],rich_rot[ENEMY_CAPACITY];
static u32 rich_rr,rich_lite,rich_stagger,rich_lite_active;static u64 rich_last;
/* BGT-B: effective pool cap (512 or BULLET_CAPACITY), quota switch, per-role floors (set at rich_begin) and the live groups
 * per role counted at the start of each rich_fire (role id in pad[10:8]). */
static u32 rich_cap=BULLET_CAPACITY,rich_quota=1,rich_floor[RICH_ROLE_COUNT],rich_live_role[RICH_ROLE_COUNT];
static u32 bullet_pending_role_visible[RICH_ROLE_COUNT],bullet_display_role_visible[RICH_ROLE_COUNT];
static u32 bullet_pending_style_visible[BULLET_STYLE_COUNT],bullet_display_style_visible[BULLET_STYLE_COUNT];
static void rich_begin(u64 now);
static void rich_fire(u64 now);
static u32 bullet_angles; /* GHDA: defined (ANGLES_ENABLED) with the density tables below */
static uint8_t plane_angle[ENEMY_CAPACITY]; /* GHDA: 0 / 1 / 2 = 30 / 45 / 60 deg, rank of the plane within its type % 3 */
static void plane_angle_assign(void);
/* Stage-1 D: identity of the confirmed table and per-frame stage effort. */
static u32 bullet_stage_calls,bullet_display_stage_calls,bullet_display_sequence,bullet_lcg_begin;
/* Avoid implementation-defined signed shifts; y is always within the clipped
 * 8x12 descriptor domain before conversion. */
static int bullet_floor(int32_t q){return q>=0?q/65536:-1-(int)((-(int64_t)q-1)/65536);}
static int bullet_visible(int x,int y,u32 type){
 const uint8_t *mask=asset_upload_enemy_bullet_mask(type);int sx0=x<0?-x:0,sx1=1920-x;
 int sy0=y<0?-y:0,sy1=1080-y,row;u32 columns;
 if(!mask)return 0;
 if(sx1>8)sx1=8;
 if(sy1>12)sy1=12;
 if(sx0>=sx1 || sy0>=sy1)return 0;
 columns=((1u<<(sx1-sx0))-1u)<<(16-sx1);
 for(row=sy0;row<sy1;row++)if((((u32)mask[row*2]<<8)|mask[row*2+1])&columns)return 1;
 return 0;
}
static void bullet_pool_clear(void){memset(bullet_pool,0,sizeof(bullet_pool));bullet_high_water=0;}
void bullet_scene_init(void){
 bullet_enabled=0;bullet_stage=0;bullet_display_count=bullet_display_active=bullet_display_visible=0;
 bullet_display_spawned=bullet_display_dropped=bullet_display_skipped=bullet_display_fan=0;
 bullet_stress_target=0;bullet_fire_enabled=1;bullet_pattern_mode=2;bullet_display_groups=0;
 bullet_pool_clear();
}
u32 bullet_scene_selected(void){return bullet_enabled;}
gpu_result_t bullet_scene_select(u32 enable){
 u32 cap;gpu_result_t r;
 if(enable>1u)return GPU_ERR_PARAM;
 if(bullet_stage)return GPU_ERR_BUSY;
 if(enable){if(enemy_scene_selected()==ENEMY_SCENE_SINGLE)return GPU_ERR_PARAM;
  r=gpu_bullet_scene_capacity(&cap);if(r!=GPU_OK)return r;if(cap!=BULLET_CAPACITY)return GPU_ERR_UNSUPPORTED;}
 bullet_enabled=enable;return GPU_OK;
}
gpu_result_t bullet_scene_pattern(u32 mode){
 u32 cap;gpu_result_t r;if(mode>3u)return GPU_ERR_PARAM;if(bullet_stage)return GPU_ERR_BUSY;
 r=gpu_bullet_scene_capacity(&cap);if(r!=GPU_OK)return r;
 bullet_pattern_mode=mode;bullet_stress_target=0;return GPU_OK;
}
u32 bullet_scene_dual(void){return bullet_pattern_mode!=0;}
u32 bullet_scene_pattern_mode(void){return bullet_pattern_mode;}
u32 bullet_scene_displayed_groups(void){return bullet_display_groups;}
/* pad: live[2:0], retire[5:3], immutable layout[6]. A surviving mask011
 * in triple layout remains offsets (0,0)/(9,5). Never reconstruct layout
 * from live bits or from the current pattern command. */
enum { BULLET_LAYOUT=64 };
/* GHD5: triple member offsets and span depend on the style (style 2 = triangle of round disks, styles 0/1 = V) */
/* GHD5R: triple groups turn with their motion direction (16 directions, generator LUT, shapes and spacing kept). Member m top-left =
 * group reference + rot_t[shape][dir][m]; the table anchor is the rotated bbox top-left (reference + rot_min); dir lives in pad[15:12]. */
static const int8_t rot_t[2][16][3][2]=BULLET_TRI_ROT_T,rot_min[2][16][2]=BULLET_TRI_ROT_MIN;
static const uint8_t rot_w[2][16]=BULLET_TRI_ROT_W,rot_h[2][16]=BULLET_TRI_ROT_H;
enum { BULLET_DIR_SHIFT=12, BULLET_DIR_MASK=15u<<12 };
#define TRI_SHAPE(t) (((t)&3u)==BULLET_TRI_ALT_STYLE?1u:0u)
#define PDIR(p) (((p)->pad>>BULLET_DIR_SHIFT)&15u)
/* Sector of (vx,vy) measured from +y toward +x, 22.5 deg steps; integer compares only (tan 11.25/33.75/56.25/78.75 x 1024). */
u32 bullet_dir16(int vx,int vy,u32 prev){
 u32 ax=(u32)(vx<0?-vx:vx),ay=(u32)(vy<0?-vy:vy),k=0;
 if(!ax && !ay)return prev&15u;
 k=(ax*1024u>ay*204u)+(ax*1024u>ay*684u)+(ax*1024u>ay*1533u)+(ax*1024u>ay*5148u);
 if(vx>=0)return vy>=0?k:8u-k;
 return vy<0?8u+k:(16u-k)&15u;
}
static int bullet_dx(u32 type,u32 layout,u32 dir,u32 member){
 return layout?rot_t[TRI_SHAPE(type)][dir&15u][member][0]:(int)member*BULLET_MEMBER_DX;
}
static int bullet_dy(u32 type,u32 layout,u32 dir,u32 member){
 return layout?rot_t[TRI_SHAPE(type)][dir&15u][member][1]:0;
}
static void bullet_reclaim(bullet_state_t *p){
 p->pad=(p->pad&(BULLET_LAYOUT|BULLET_ROLE_MASK|BULLET_DIR_MASK))|((p->pad & ~(p->pad>>3))&7u);
 if(!(p->pad&7u) || (p->flags&BULLET_RETIRE))p->flags=0;
}
void bullet_scene_set_stress(u32 target){bullet_stress_target=target>BULLET_CAPACITY?BULLET_CAPACITY:target;}
u32 bullet_scene_stress(void){return bullet_stress_target;}
void bullet_scene_set_fire(u32 enable){bullet_fire_enabled=enable!=0;}
void bullet_scene_reset_time(u64 now){
 u32 i;for(i=0;i<ENEMY_CAPACITY;i++){
  u64 remaining=bullet_due[i]>bullet_last_tick?bullet_due[i]-bullet_last_tick:0;
  bullet_due[i]=remaining>~(u64)0-now?~(u64)0:now+remaining;
 }
 {u64 remaining=bullet_global_due>bullet_last_tick?bullet_global_due-bullet_last_tick:0;
  bullet_global_due=remaining>~(u64)0-now?~(u64)0:now+remaining;}
 bullet_last_tick=now;
}
/* Stable y order is also the paste order for equal-y bullets.  Keep that
 * order exactly. Small populations use insertion sort; larger disordered
 * populations use two bounded radix passes over y+32 (range 1..1111).
 * Scratch lives in the existing CPU RAM, never in the FPGA sprite cache.
 * M1: entries are pool slots; the key is the frozen pool y (same value as the former (int16_t)word[1]). */
static uint16_t bullet_sort_scratch[BULLET_CAPACITY];
static int bullet_key(u32 slot){const bullet_state_t *p=&bullet_pool[slot]; /* GHD5R: table y = rotated bbox top */
 return (int16_t)bullet_floor(p->y_q16)+((p->pad&BULLET_LAYOUT)?rot_min[TRI_SHAPE(p->type)][PDIR(p)][1]:0);}
static void bullet_sort_pending(void){
 u32 i,k,pass,n=bullet_pending_count,ordered=1,moves=0;
 for(i=1;i<n;i++)if(bullet_key(bullet_pending[i-1u])>bullet_key(bullet_pending[i])){ordered=0;break;}
 if(ordered)return;
 /* Retain cheap insertion for small or nearly sorted pools. Only switch
  * after a completed insertion, so equal-y source order remains intact. */
 for(i=1;i<n;i++){
   uint16_t d=bullet_pending[i];int y=bullet_key(d);
   for(k=i;k>0 && bullet_key(bullet_pending[k-1u])>y;k--){bullet_pending[k]=bullet_pending[k-1u];moves++;}
   bullet_pending[k]=d;
   if(n>=64u && moves>n){i++;break;}
 }
 if(i<n){
  uint16_t bins[64];
  for(pass=0;pass<2;pass++){
   uint16_t *src=pass?bullet_sort_scratch:bullet_pending;
   uint16_t *dst=pass?bullet_pending:bullet_sort_scratch;
   u32 shift=pass*6u,total=0;
   memset(bins,0,sizeof(bins));
   for(i=0;i<n;i++){u32 key=((u32)(bullet_key(src[i])+32)>>shift)&63u;bins[key]++;}
   for(i=0;i<64u;i++){u32 count=bins[i];bins[i]=(uint16_t)total;total+=count;}
   for(i=0;i<n;i++){u32 key=((u32)(bullet_key(src[i])+32)>>shift)&63u;dst[bins[key]++]=src[i];}
  }
 }
}
static gpu_result_t bullet_collect(void){
 u32 i,k;uint8_t opaque[BULLET_STYLE_COUNT];
 /* O4: a member wholly inside the screen is visible iff its style mask has
  * any opaque pixel; recomputed here from the same mask bullet_visible reads. */
 for(k=0;k<BULLET_STYLE_COUNT;k++){const uint8_t *m=asset_upload_enemy_bullet_mask(k);u32 r;opaque[k]=0;
  if(m)for(r=0;r<12;r++)if(m[r*2]){opaque[k]=1;break;}}
 bullet_pending_count=bullet_pending_active=bullet_pending_visible=bullet_pending_groups=0;
 memset(bullet_pending_role_visible,0,sizeof(bullet_pending_role_visible));memset(bullet_pending_style_visible,0,sizeof(bullet_pending_style_visible));
 for(i=0;i<bullet_high_water;i++)if(bullet_pool[i].flags&BULLET_ACTIVE){
  bullet_state_t *p=&bullet_pool[i];int x=bullet_floor(p->x_q16),y=bullet_floor(p->y_q16);u32 type=p->type;
  /* Preserve retiring paths until combat, then reclaim before collection. */
  u32 mask=p->pad&7u,layout=p->pad&BULLET_LAYOUT,visible=0;
  if(p->flags&BULLET_RETIRE || !mask){p->flags=0;continue;}
  bullet_pending_groups++;
  bullet_pending_active+=(mask&1u)+((mask>>1)&1u)+((mask>>2)&1u);
  for(u32 member=0;member<BULLET_MEMBER_MAX;member++)if(mask&(1u<<member)){
   int mx=x+bullet_dx(type,layout,PDIR(p),member),my=y+bullet_dy(type,layout,PDIR(p),member);
   visible+=mx>=0 && mx<=1920-8 && my>=0 && my<=1080-12?(type<BULLET_STYLE_COUNT?opaque[type]:0u):(u32)bullet_visible(mx,my,type);}
  bullet_pending_visible+=visible;bullet_pending_style_visible[type&3u]+=visible;bullet_pending_role_visible[(p->pad>>BULLET_ROLE_SHIFT)&7u]+=visible;
  if(layout){const int8_t *mn=rot_min[TRI_SHAPE(type)][PDIR(p)];int ax=x+mn[0],ay=y+mn[1];
   if(ax<=-(int)rot_w[TRI_SHAPE(type)][PDIR(p)] || ax>=1920 || ay<=-(int)rot_h[TRI_SHAPE(type)][PDIR(p)] || ay>=1080)continue;}
  else if(x<=-28 || x>=1920 || y<=-12 || y>=1080)continue;
  bullet_pending[bullet_pending_count++]=(uint16_t)i;
 }
 if(bullet_pending_groups>rich_stats.peak_groups)rich_stats.peak_groups=bullet_pending_groups;
 bullet_sort_pending();
 bullet_position=0;bullet_sequence=gpu_enemy_scene_frame_sequence()+1u;bullet_stage=1;bullet_stage_calls=0;return GPU_OK;
}

gpu_result_t bullet_scene_begin(u64 now){
 gpu_result_t r;u32 i,t;
 if(!bullet_enabled)return gpu_bullet_scene_mode(0);
 /* Validate all tiles before enabling the PL segment or changing CPU state. */
 for(t=0;t<BULLET_STYLE_COUNT;t++){
  static const uint8_t inners[BULLET_STYLE_COUNT][4]=BULLET_STYLE_HITBOX_VALUES;
  const uint8_t *mask=asset_upload_enemy_bullet_mask(t),*inner=inners[t];u32 x,y;
  if(!mask)return GPU_ERR_PARAM;
  for(y=inner[1];y<inner[3];y++)for(x=inner[0];x<inner[2];x++)
   if(!(mask[y*2]&(128u>>x)))return GPU_ERR_PARAM;
 }
 r=gpu_bullet_scene_mode(1);if(r!=GPU_OK)return r;
 bullet_lcg_begin=bullet_lcg;
 bullet_pool_clear();
 bullet_spawned=bullet_dropped=bullet_skipped=bullet_priority=bullet_fan_spawned=bullet_spawn_groups=0;
 for(t=0;t<BULLET_STYLE_COUNT;t++){const uint8_t *mask=asset_upload_enemy_bullet_mask(t);u32 x,y;
  bullet_hitbox[t][0]=8;bullet_hitbox[t][1]=12;bullet_hitbox[t][2]=bullet_hitbox[t][3]=0;
  for(y=0;y<12;y++)for(x=0;x<8;x++)if(mask[y*2+x/8]&(128u>>(x&7))){
   if(x<bullet_hitbox[t][0])bullet_hitbox[t][0]=(uint8_t)x;
   if(y<bullet_hitbox[t][1])bullet_hitbox[t][1]=(uint8_t)y;
   if(x+1u>bullet_hitbox[t][2])bullet_hitbox[t][2]=(uint8_t)(x+1u);
   if(y+1u>bullet_hitbox[t][3])bullet_hitbox[t][3]=(uint8_t)(y+1u);
  }
  /* A deliberately conservative all-opaque inner box; never collide with
   * transparent corners. The fixed packed sprite CRC is checked at upload. */
  {static const uint8_t inners[BULLET_STYLE_COUNT][4]=BULLET_STYLE_HITBOX_VALUES;
   const uint8_t *inner=inners[t];
   if(bullet_hitbox[t][0]<inner[0])bullet_hitbox[t][0]=inner[0];
   if(bullet_hitbox[t][1]<inner[1])bullet_hitbox[t][1]=inner[1];
   if(bullet_hitbox[t][2]>inner[2])bullet_hitbox[t][2]=inner[2];
   if(bullet_hitbox[t][3]>inner[3])bullet_hitbox[t][3]=inner[3];}
 }
 bullet_last_tick=now;bullet_global_due=now+legacy_ticks(FIRE_GLOBAL_PERIOD_MS);
 for(i=0;i<ENEMY_CAPACITY;i++)bullet_due[i]=now+legacy_ticks(FIRE_LARGE_PERIOD_MS);
 bullet_angles=ANGLES_ENABLED;memset(&rich_stats,0,sizeof(rich_stats));if(bullet_pattern_mode==3u)rich_begin(now); /* GHDA: rich_begin may set the ladder angles */
 plane_angle_assign();
 return bullet_collect();
}
int bullet_spawn_count_from(u32 emitter,u32 *live);
static u32 bullet_free_slot(void){
 u32 slot;for(slot=0;slot<bullet_high_water;slot++)if(!(bullet_pool[slot].flags&BULLET_ACTIVE))return slot;
 return bullet_high_water;
}
/* S1: role of the groups being spawned (pad[10:8]); non-zero only inside rich_fire, so the legacy
 * five-argument spawn and every existing caller keep their exact behaviour (role 0). */
static u32 bullet_spawn_role;
/* GHD3S density tiers, RICH pattern (pressure) only: shots of the styles in the tier mask are fired as triple groups (the hardware
 * table accepts the triple layout for the triple style only, so they take that style). Same pool, quota, periods and emitters:
 * only members per group (2 -> 3) change. L = original pattern (GHD1/CMD), M and H raise the on-screen member count. */
/* GHD5: the tier triple mask is baked into the RICH rows (generator) */
static u32 density_tier=BULLET_DENSITY_DEFAULT;
/* GHD5: LIGHT density = pattern 0/1/2 fire periods x pct/100 (flow_profile sets it; 100 = original cadence, pattern 3 never reads it) */
static u32 legacy_period_pct=100u;
void bullet_scene_legacy_period(u32 pct){legacy_period_pct=pct>=25u && pct<=100u?pct:100u;}
u32 bullet_scene_legacy_period_pct(void){return legacy_period_pct;}
static u64 legacy_ticks(u32 ms){return ((u64)BSP_CLINT_HZ*ms*legacy_period_pct)/100000u;}
/* GHD4: tiers keep the M style mix; they differ only in the pressure pool cap and emission period (pressure RICH only). */
static const uint16_t density_pool[3]=BULLET_DENSITY_POOL_CAPS;static const uint16_t density_period_permille[3]=BULLET_DENSITY_PERIOD_PERMILLE;
static const uint8_t density_glow[3]=BULLET_DENSITY_GLOW;
/* GHDA probe ladder (tier BULLET_TIER_LADDER, only with PROBE_LADDER_ENABLED): pressure levels stepped every LADDER_LEVEL_SECONDS */
static const uint16_t ladder_pool[LADDER_LEVEL_COUNT]=LADDER_POOL,ladder_permille[LADDER_LEVEL_COUNT]=LADDER_PERIOD_PERMILLE;
static const uint8_t ladder_angles[LADDER_LEVEL_COUNT]=LADDER_ANGLES,ladder_glow[LADDER_LEVEL_COUNT]=LADDER_GLOW;
static const char ladder_tags[LADDER_LEVEL_COUNT][6]=LADDER_TAGS;
static u32 ladder_level;static u64 ladder_acc; /* pressure ticks in the ladder tier (kept across game over / restart, reset by a tier change) */
/* GHDA: oblique rows / light fan side shots at the firing plane's angle (instance index % 3 -> 30 / 45 / 60 deg from straight down) */
static u32 bullet_angles=ANGLES_ENABLED;
static const int16_t rich_angle_v[RICH_ANGLE_COUNT][2]=RICH_ANGLE_VXVY;
static const int16_t fire_angle_v[2][RICH_ANGLE_COUNT][2]=FIRE_ANGLE_VXVY;
static int density_ladder(void){return BULLET_TIER_COUNT>3u && density_tier==BULLET_TIER_LADDER;}
static u32 rich_permille_now(void){return density_ladder()?ladder_permille[ladder_level]:density_period_permille[density_tier<3u?density_tier:BULLET_DENSITY_DEFAULT];}
u32 bullet_scene_density_pool(void){return density_ladder()?ladder_pool[ladder_level]:density_pool[density_tier<3u?density_tier:BULLET_DENSITY_DEFAULT];}
void bullet_scene_density_select(u32 tier){u32 t=tier<BULLET_TIER_COUNT?tier:BULLET_DENSITY_DEFAULT;if(t!=density_tier){ladder_level=0;ladder_acc=0;}density_tier=t;}
u32 bullet_scene_density_tier(void){return density_tier;}
static int bullet_spawn(int x,int y,int vx,int vy,u32 type,u32 layout){
 u32 slot,mask=0,members=0;
 layout=bullet_pattern_mode>=2u && layout && type<BULLET_STYLE_COUNT && ((BULLET_TRIPLE_STYLE_MASK>>type)&1u);
 u32 dir=layout?bullet_dir16(vx,vy,0u):0u; /* GHD5R: initial direction from the spawn velocity */
 for(u32 member=0;member<(layout?3u:(bullet_pattern_mode?2u:1u));member++){
  int mx=x+bullet_dx(type,layout,dir,member),my=y+bullet_dy(type,layout,dir,member);
  if(my>=1080 || my<=-12 || mx<=-8 || mx>=1920)bullet_skipped++;
  else{mask|=1u<<member;members++;}
 }
 if(!mask)return 0;
 slot=bullet_free_slot();
 if(slot==BULLET_CAPACITY){bullet_dropped+=members;return 0;}
 if(slot>=bullet_high_water)bullet_high_water=slot+1u;
 bullet_pool[slot]=(bullet_state_t){x*65536,y*65536,(int16_t)vx,(int16_t)vy,BULLET_ACTIVE,(uint8_t)type,(uint16_t)(mask|(layout?BULLET_LAYOUT:0)|(bullet_spawn_role<<BULLET_ROLE_SHIFT)|(dir<<BULLET_DIR_SHIFT))};
 bullet_spawned+=members;bullet_spawn_groups++;return 1;
}
/* Fan: centre + two side shots at +/- the configured angle (vx = speed*sin, vy = speed*cos).
 * Returns the number of shots attempted (1 or 3; 0 for a dead emitter); each
 * attempt is spawned, dropped (pool full) or skipped (off screen) and counted. */
static u32 bullet_fire_from(u32 emitter,u32 pattern){
 enemy_object_desc_t plane;int x,y;u32 large,speed,style,material,triple;int32_t side;
 if(!enemy_scene_pending_emitter(emitter,&plane))return 0;
 {static const uint8_t styles[ENEMY_MATERIAL_COUNT]=BULLET_STYLE_BY_MATERIAL;
  material=enemy_scene_instance_material(emitter);style=styles[material];
  large=material==ENEMY_MATERIAL_COUNT-1u;}
 speed=large?FIRE_LARGE_SPEED:FIRE_SMALL_SPEED;
 triple=bullet_pattern_mode==2u && material==BULLET_TRIPLE_MATERIAL;
 x=(int)(plane.word[0]&65535u)+(triple?BULLET_TRIPLE_ANCHOR_X:((int)(plane.word[2]&65535u)-(bullet_pattern_mode?28:8))/2);
 y=(int)(plane.word[0]>>16)+(triple?BULLET_TRIPLE_ANCHOR_Y:(int)(plane.word[2]>>16)+2);
 (void)bullet_spawn(x,y,0,(int)speed,style,style==BULLET_TRIPLE_STYLE);
 if(!pattern || triple)return 1;
 if(bullet_angles){const int16_t *a=fire_angle_v[large?1u:0u][emitter<ENEMY_CAPACITY?plane_angle[emitter]:0u]; /* GHDA: plane angle, exact nominal speed */
  (void)bullet_spawn(x,y,-a[0],a[1],style,style==BULLET_TRIPLE_STYLE);(void)bullet_spawn(x,y,a[0],a[1],style,style==BULLET_TRIPLE_STYLE);}
 else{side=(int32_t)(((int64_t)speed*FIRE_FAN_HALF_ANGLE_Q16)>>16);
 (void)bullet_spawn(x,y,-side,(int)speed-side/4,style,style==BULLET_TRIPLE_STYLE);
 (void)bullet_spawn(x,y,side,(int)speed-side/4,style,style==BULLET_TRIPLE_STYLE);}
 bullet_fan_spawned++;
 return 3;
}
/* S1 RICH: every live plane fires its role's pattern from its own muzzle on its own period.
 * An event spawns all of its groups in one frame or waits (frame budget / free slots); the
 * longest-waiting event (>= RICH_AGE_FRAMES) is tried first and, if it still does not fit,
 * reserves the frame; an event later than half a period is dropped, never caught up.
 * Events are generated rows {muzzle dx,dy, style, velocity}; one spawn site keeps -O3 code small. */
typedef struct { uint8_t dx,dy,style,pad;int16_t vx,vy; } rich_shot_t;
static const rich_shot_t rich_shots[RICH_EVENT_ROWS]=RICH_EVENT_VALUES;
static const uint8_t rich_index[RICH_ROLE_COUNT][4]=RICH_EVENT_INDEX;
static const uint16_t rich_period_ms[RICH_ROLE_COUNT]=RICH_PERIOD_MS_VALUES;
static u32 rich_ticks[RICH_ROLE_COUNT];      /* role period in CLINT ticks for this session (<= 1.1e8) */
static uint8_t rich_role[ENEMY_CAPACITY];      /* role of every instance, fixed per session */
static __attribute__((noinline)) void rich_next(u64 *due,u32 T,u64 now){*due+=T;if(*due<=now)*due=now+T;}
static __attribute__((noinline)) void rich_hold(u32 e){if(rich_wait[e]<255u)rich_wait[e]++;if(rich_wait[e]>rich_stats.max_wait)rich_stats.max_wait=rich_wait[e];}
/* Phase fraction (Q16) of instance e: golden ratio; used for the first-shot jitter and the STAGGER slot. */
static u32 rich_golden(u32 e){return (e*40503u)&65535u;}
static __attribute__((noinline,optimize("Os"))) void rich_begin(u64 now){
 u32 n=enemy_scene_instance_count(),i;uint8_t cnt[RICH_ROLE_COUNT],seen[RICH_ROLE_COUNT];
 rich_lite_active=rich_lite && game_selected();rich_rr=0;rich_last=now;
 bullet_angles=density_ladder()?ladder_angles[ladder_level]:ANGLES_ENABLED; /* GHDA: the ladder resumes its level */
 for(i=0;i<RICH_ROLE_COUNT;i++){cnt[i]=seen[i]=0;rich_ticks[i]=BSP_CLINT_HZ/1000u*(i==RICH_ROLE_FANX && rich_lite_active?RICH_LITE_FANX_PERIOD_MS:rich_period_ms[i]);
  if(!rich_lite_active)rich_ticks[i]=(u32)(((u64)rich_ticks[i]*rich_permille_now())/1000u);} /* GHDA: per mille */
 for(i=0;i<n;i++){rich_role[i]=(uint8_t)enemy_scene_instance_role(i);cnt[rich_role[i]]++;}
 {static const uint8_t per[RICH_ROLE_COUNT]=RICH_QUOTA_PER_PLANE_VALUES;u32 sum=0,lim=rich_cap>RICH_QUOTA_SHARED_MIN?rich_cap-RICH_QUOTA_SHARED_MIN:0;
  for(i=0;i<RICH_ROLE_COUNT;i++){rich_floor[i]=(u32)per[i]*cnt[i];sum+=rich_floor[i];}
  if(sum>lim)for(i=0;i<RICH_ROLE_COUNT;i++)rich_floor[i]=(u32)(((u64)rich_floor[i]*lim)/sum);}
 /* First shot: 250 ms, then the role's planes spread over one period by rank (+0.3 x golden jitter). */
 for(i=0;i<n;i++){u32 r=rich_role[i],ph=((((u32)seen[r]++)<<16)+((rich_golden(i)*19661u)>>16))/cnt[r];
  bullet_due[i]=now+BSP_CLINT_HZ/1000u*RICH_FIRST_SHOT_MS+(u32)(((u64)rich_ticks[r]*(ph&65535u))>>16);
  rich_rot[i]=(uint8_t)(i&1u);rich_wait[i]=0;}
}
static void plane_angle_assign(void){u32 n=enemy_scene_instance_count(),i;uint8_t rank[16];
 for(i=0;i<16u;i++)rank[i]=0;
 for(i=0;i<ENEMY_CAPACITY;i++){u32 k=0;
  if(i<n){k=bullet_pattern_mode==3u?rich_role[i]:8u+enemy_scene_instance_material(i);if(k>15u)k=15u;}
  plane_angle[i]=(uint8_t)(i<n?(rank[k]++)%RICH_ANGLE_COUNT:0u);}
}
/* GHDA ladder step: pool cap / period / angle switch from the next event on; due times, roles and groups in flight are kept. */
static __attribute__((noinline,optimize("Os"))) void rich_retune(void){
 u32 n=enemy_scene_instance_count(),i;uint8_t cnt[RICH_ROLE_COUNT];
 for(i=0;i<RICH_ROLE_COUNT;i++){cnt[i]=0;rich_ticks[i]=BSP_CLINT_HZ/1000u*(i==RICH_ROLE_FANX && rich_lite_active?RICH_LITE_FANX_PERIOD_MS:rich_period_ms[i]);
  if(!rich_lite_active)rich_ticks[i]=(u32)(((u64)rich_ticks[i]*rich_permille_now())/1000u);}
 for(i=0;i<n;i++)cnt[rich_role[i]]++;
 {static const uint8_t per[RICH_ROLE_COUNT]=RICH_QUOTA_PER_PLANE_VALUES;u32 sum=0,lim=rich_cap>RICH_QUOTA_SHARED_MIN?rich_cap-RICH_QUOTA_SHARED_MIN:0;
  for(i=0;i<RICH_ROLE_COUNT;i++){rich_floor[i]=(u32)per[i]*cnt[i];sum+=rich_floor[i];}
  if(sum>lim)for(i=0;i<RICH_ROLE_COUNT;i++)rich_floor[i]=(u32)(((u64)rich_floor[i]*lim)/sum);}
}
static void ladder_step(u64 dt){u32 l,ft=BSP_CLINT_HZ/60u;
 if(!density_ladder())return;
 ladder_acc+=dt>2u*ft?2u*ft:dt;l=(u32)((ladder_acc/((u64)BSP_CLINT_HZ*LADDER_LEVEL_SECONDS))%LADDER_LEVEL_COUNT);
 if(l==ladder_level)return;
 ladder_level=l;rich_cap=ladder_pool[l]>BULLET_CAPACITY?BULLET_CAPACITY:ladder_pool[l];bullet_angles=ladder_angles[l];rich_retune();
}
/* BGT-B admission: never above the effective cap; with the quota, a role above its floor may not use groups that
 * other roles still miss to reach their floors. */
static __attribute__((noinline)) int rich_admit(u32 role,u32 g,u32 live){
 u32 r,res=0,free=live<rich_cap?rich_cap-live:0;
 if(g>free)return 0;
 if(!rich_quota || role>=RICH_ROLE_COUNT)return 1;
 if(rich_live_role[role]+g<=rich_floor[role])return 1;
 for(r=1;r<RICH_ROLE_COUNT;r++)if(r!=role && rich_floor[r]>rich_live_role[r])res+=rich_floor[r]-rich_live_role[r];
 return g+res<=free;
}
/* Pass 0 serves events waiting >= RICH_AGE_FRAMES (rotating order); if one of them still does not fit,
 * the frame is reserved: no other due event may spawn (it waits, counted as deferred_reserve). */
static __attribute__((noinline,optimize("Os"))) void rich_fire(u64 now){
 u32 n=enemy_scene_instance_count(),k,pass,live=0,budget,reserve=0,ft=BSP_CLINT_HZ/60u,d32;enemy_object_desc_t plane;
 u64 dt=now>=rich_last?now-rich_last:0;
 rich_last=now;if(!n)return;
 ladder_step(dt); /* GHDA: bounded frame time accumulates in the ladder */
 for(k=0;k<RICH_ROLE_COUNT;k++)rich_live_role[k]=0;
 for(k=0;k<bullet_high_water;k++)if(bullet_pool[k].flags&BULLET_ACTIVE){u32 r=(bullet_pool[k].pad&BULLET_ROLE_MASK)>>BULLET_ROLE_SHIFT;live++;if(r<RICH_ROLE_COUNT)rich_live_role[r]++;}
 /* Budget scales with the frame interval (at least one frame, at most two frames' worth). */
 d32=dt>2u*ft?2u*ft:(u32)dt;budget=(RICH_FRAME_BUDGET*d32+ft/2u)/ft;if(budget<RICH_FRAME_BUDGET)budget=RICH_FRAME_BUDGET;
 for(pass=0;pass<2;pass++)for(k=0;k<n;k++){
  u32 e=rich_rr+k,role,g,y,s,T;const uint8_t *ix;u64 *due,late;
  if(e>=n)e-=n;
  if((rich_wait[e]>=RICH_AGE_FRAMES)==pass || !enemy_scene_pending_emitter(e,&plane))continue;
  role=rich_role[e];T=rich_ticks[role];due=&bullet_due[e];late=now-*due;
  /* A respawned plane waits one whole period (plus its phase slot with STAGGER) before its first shot. */
  if(enemy_scene_emitter_restarted(e)){*due=now+T+(rich_stagger?(u32)(((u64)T*rich_golden(e))>>16):0);rich_wait[e]=0;continue;}
  if((int64_t)late<0)continue;
  if(reserve){rich_stats.deferred_reserve++;rich_hold(e);continue;}
  y=plane.word[0]>>16;
  if(role==RICH_ROLE_RING2?y+32u>RICH_RING_MAX_CENTER_Y:y>RICH_CEASE_Y){rich_stats.cease++;rich_next(due,T,now);rich_wait[e]=0;continue;}
  if(late>T/2u){rich_stats.late++;rich_next(due,T,now);rich_wait[e]=0;continue;}
  ix=rich_index[role]+(rich_rot[e]?2u:0u);g=ix[1];
  if(g>budget || !rich_admit(role,g,live)){if(g>budget)rich_stats.deferred_budget++;else if(live+g>rich_cap)rich_stats.deferred_pool++;else rich_stats.deferred_quota++;rich_hold(e);if(!pass)reserve=1;continue;}
  budget-=g;live+=g;rich_live_role[role]+=g;rich_wait[e]=0;rich_stats.events++;
  bullet_spawn_role=role;
  {static const uint8_t mat_style[ENEMY_MATERIAL_COUNT]=BULLET_STYLE_BY_MATERIAL;u32 m=enemy_scene_instance_material(e);
  for(s=ix[0];s<ix[0]+g;s++){const rich_shot_t *r=&rich_shots[s];
   /* GHD5: colour of the firing aircraft (material style) unless the row's triple layout is not available for that style */
   u32 tri=r->pad&1u;int vx=r->vx,vy=r->vy; /* GHDA: pad bit0 triple, bit1 angle row (the plane's 30 / 45 / 60 deg, left / right kept) */
   if((r->pad&2u) && bullet_angles){const int16_t *a=rich_angle_v[plane_angle[e]];vx=r->vx<0?-a[0]:a[0];vy=a[1];}
   u32 st=RICH_COLOUR_BY_AIRCRAFT && m<ENEMY_MATERIAL_COUNT?mat_style[m]:r->style;if(tri && !((BULLET_TRIPLE_STYLE_MASK>>st)&1u))st=r->style;
   (void)bullet_spawn((int)(plane.word[0]&65535u)+r->dx,(int)y+r->dy,vx,vy,st,tri);}}
  bullet_spawn_role=0;
  if(role==RICH_ROLE_FANX || role==RICH_ROLE_RING2 || role==RICH_ROLE_CURTAIN)rich_rot[e]^=1u;
  rich_next(due,T,now);
 }
 if(++rich_rr>=n)rich_rr=0;
}
/* Per-call cache of common absolute velocities. These fixed values are an
 * optimization only: all other int16 velocities retain the original formula.
 * With the existing four-second dt bound, the largest cached Q16 displacement
 * is 480*65536*4 = 125829120, so each cached positive result fits int32_t.
 * Do not cache a rounded time factor: truncation belongs after v*65536*dt. */
typedef struct { int32_t delta[26];u32 valid; } bullet_motion_cache_t;
static int64_t bullet_motion_delta(int16_t velocity,u64 dt,bullet_motion_cache_t *cache){
 /* S1: RICH components added (fan, 45-degree, blue three-way, 200 px/s ring). */
 static const uint16_t common_speeds[26]={120,161,330,360,440,480,123,338,231,276,63,355,180,312,255,93,348,200,193,173,141,100,52,240,416,339}; /* GHDA: + light 480 angles */
 u32 i,magnitude;
 if(!velocity)return 0;
 /* Promote before negation so INT16_MIN is handled by the exact fallback. */
 magnitude=velocity<0?(u32)(-(int32_t)velocity):(u32)velocity;
 for(i=0;i<26;i++)if(magnitude==common_speeds[i]){
  u32 bit=1u<<i;
  if(!(cache->valid&bit)){
   cache->delta[i]=(int32_t)(((int64_t)magnitude*65536*(int64_t)dt)/(int64_t)BSP_CLINT_HZ);
   cache->valid|=bit;
  }
  /* Signed division truncates toward zero, hence q(-v) == -q(v). */
  return velocity<0?-(int64_t)cache->delta[i]:(int64_t)cache->delta[i];
 }
 return ((int64_t)velocity*65536*(int64_t)dt)/(int64_t)BSP_CLINT_HZ;
}

gpu_result_t bullet_scene_advance_motion(u64 now){
 u32 i;u64 dt;bullet_motion_cache_t cache;
 if(!bullet_enabled)return GPU_OK;
 if(bullet_stage)return GPU_ERR_BUSY;
 dt=now>=bullet_last_tick?now-bullet_last_tick:0;bullet_last_tick=now;
 /* Over four seconds every existing shot has exited; saturate first. */
 if(dt/BSP_CLINT_HZ>=4u)dt=(u64)BSP_CLINT_HZ*4u;
 /* No entry survives this time step; inactive/empty pools compute no deltas. */
 cache.valid=0;
 for(i=0;i<bullet_high_water;i++)if(bullet_pool[i].flags&BULLET_ACTIVE){
  bullet_state_t *p=&bullet_pool[i];
  int64_t dx=bullet_motion_delta(p->vx,dt,&cache);
  int64_t dy=bullet_motion_delta(p->vy,dt,&cache);
  int64_t nx=(int64_t)p->x_q16+dx,ny=(int64_t)p->y_q16+dy;
  bullet_previous_x[i]=(int16_t)bullet_floor(p->x_q16);bullet_previous_y[i]=(int16_t)bullet_floor(p->y_q16);
  u32 mask=p->pad&7u,layout=p->pad&BULLET_LAYOUT,retire=0;
  u32 dir=layout?bullet_dir16(p->vx,p->vy,PDIR(p)):0u; /* GHD5R: direction of the current motion (zero velocity keeps the last) */
  int width=layout?rot_w[TRI_SHAPE(p->type)][dir]:(bullet_pattern_mode?28:8),height=layout?rot_h[TRI_SHAPE(p->type)][dir]:12;
  p->flags=BULLET_ACTIVE|BULLET_MOVED;
  for(u32 member=0;member<BULLET_MEMBER_MAX;member++)if(mask&(1u<<member)){
   int64_t mx=nx+(int64_t)bullet_dx(p->type,layout,dir,member)*65536,my=ny+(int64_t)bullet_dy(p->type,layout,dir,member)*65536;
   if(my>=(int64_t)1080*65536 || my<=-(int64_t)12*65536 || mx>=(int64_t)1920*65536 || mx<=-(int64_t)8*65536)retire|=1u<<member;
  }
  p->pad=(uint16_t)((p->pad&BULLET_ROLE_MASK)|layout|mask|(retire<<3)|(dir<<BULLET_DIR_SHIFT));
  if(retire==mask)p->flags|=BULLET_RETIRE;
  /* Keep both final paths through combat. Clamp only the fully clipped group.
   * GHD5R: clamp the rotated bbox (reference + rot_min), so a clamped group is wholly off screen in every direction. */
  {int64_t ox=layout?(int64_t)rot_min[TRI_SHAPE(p->type)][dir][0]*65536:0,oy=layout?(int64_t)rot_min[TRI_SHAPE(p->type)][dir][1]*65536:0;
  if(nx+ox>(int64_t)1920*65536)nx=(int64_t)1920*65536-ox;
  if(nx+ox<-(int64_t)width*65536)nx=-(int64_t)width*65536-ox;
  if(ny+oy>(int64_t)1080*65536)ny=(int64_t)1080*65536-oy;
  if(ny+oy<-(int64_t)height*65536)ny=-(int64_t)height*65536-oy;}
  p->x_q16=(int32_t)nx;p->y_q16=(int32_t)ny;
 }
 return GPU_OK;
}
gpu_result_t bullet_scene_advance_fire(u64 now){
 u32 i,shots=0,count,tries;
 if(!bullet_enabled)return GPU_OK;
 if(bullet_stage)return GPU_ERR_BUSY;
 /* Combat has consumed the final paths of exiting shots: free their slots now. */
 for(i=0;i<bullet_high_water;i++)if(bullet_pool[i].flags&BULLET_ACTIVE)bullet_reclaim(&bullet_pool[i]);
 if(bullet_pattern_mode==3u){if(bullet_fire_enabled)rich_fire(now);else rich_last=now;return GPU_OK;}
 count=enemy_scene_instance_count();
 if(bullet_fire_enabled && count){
  /* Stress mode: keep the requested number of live bullets regardless of budget. */
  if(bullet_stress_target){
   u32 live=0;for(i=0;i<bullet_high_water;i++)live+=(bullet_pool[i].flags&BULLET_ACTIVE)!=0;
   for(tries=0;live<bullet_stress_target && tries<count*2u;tries++){
    u32 e=bullet_priority%count;bullet_priority=(bullet_priority+1u)%count;
    if(enemy_scene_emitter_restarted(e))continue;
    if(bullet_spawn_count_from(e,&live))break;
   }
  }else{
   /* Restarted emitters wait one full period before firing again. */
   for(i=0;i<count;i++)if(enemy_scene_emitter_restarted(i))
    bullet_due[i]=now+legacy_ticks(FIRE_LARGE_PERIOD_MS);
   /* A pattern starts only when all of its shots fit in this frame's budget;
    * a deferred emitter stays due for a later frame (never a catch-up burst). */
   for(i=0;i<count;i++)
    if(enemy_scene_instance_material(i)==ENEMY_MATERIAL_COUNT-1u && now>=bullet_due[i] && !enemy_scene_emitter_restarted(i)){
     if(shots+3u>FIRE_MAX_SPAWN_PER_FRAME)break;
     bullet_due[i]=now+legacy_ticks(FIRE_LARGE_PERIOD_MS);
     shots+=bullet_fire_from(i,1);
    }
   /* Global budget: the next live small emitter (round robin) fires once per
    * period. A full pool still consumes the period and counts its drops. */
   if(now>=bullet_global_due){
    enemy_object_desc_t plane;
    for(tries=0;tries<count;tries++){
     u32 e=bullet_priority%count,pattern;
     if(enemy_scene_instance_material(e)==ENEMY_MATERIAL_COUNT-1u || enemy_scene_emitter_restarted(e) ||
        !enemy_scene_pending_emitter(e,&plane)){bullet_priority=(bullet_priority+1u)%count;continue;}
     bullet_lcg=bullet_lcg*1664525u+1013904223u;pattern=(bullet_lcg>>28)<3u;
     if(bullet_pattern_mode==2u && enemy_scene_instance_material(e)==BULLET_TRIPLE_MATERIAL)pattern=0;
     if(shots+(pattern?3u:1u)>FIRE_MAX_SPAWN_PER_FRAME)break;
     bullet_priority=(bullet_priority+1u)%count;
     bullet_global_due=now+legacy_ticks(FIRE_GLOBAL_PERIOD_MS);
     shots+=bullet_fire_from(e,pattern);break;
    }
   }
  }
 }
 return GPU_OK;
}
/* Stress helper: fire one emitter (straight or fan at random; a fan only while it
 * cannot overshoot the target); returns 1 when the pool is full. */
int bullet_spawn_count_from(u32 emitter,u32 *live){
 u32 before=bullet_spawn_groups;bullet_lcg=bullet_lcg*1664525u+1013904223u;
 (void)bullet_fire_from(emitter,(bullet_lcg>>31)!=0 && *live+3u<=bullet_stress_target);*live+=bullet_spawn_groups-before;
 return bullet_free_slot()==BULLET_CAPACITY;
}
gpu_result_t bullet_scene_collect(void){
 if(!bullet_enabled)return GPU_OK;
 if(bullet_stage)return GPU_ERR_BUSY;
 return bullet_collect();
}
gpu_result_t bullet_scene_advance(u64 now){gpu_result_t r=bullet_scene_advance_motion(now);return r==GPU_OK?bullet_scene_advance_fire(now):r;}
gpu_result_t bullet_scene_next(u64 now){gpu_result_t r=bullet_scene_advance(now);return r==GPU_OK?bullet_scene_collect():r;}
int bullet_scene_member_box(u32 slot,u32 member,combat_box_t *b){
 bullet_state_t *p;int x,y;const uint8_t *h;
 if(!bullet_enabled || !b || slot>=BULLET_CAPACITY || member>=BULLET_MEMBER_MAX)return 0;
 p=&bullet_pool[slot];if(!(p->flags&BULLET_ACTIVE) || !(p->pad&(1u<<member)))return 0;h=bullet_hitbox[p->type];
 if(h[0]>=h[2])return 0;
 x=bullet_floor(p->x_q16)+bullet_dx(p->type,p->pad&BULLET_LAYOUT,PDIR(p),member);y=bullet_floor(p->y_q16)+bullet_dy(p->type,p->pad&BULLET_LAYOUT,PDIR(p),member);
 b->x0=x+h[0];b->x1=x+h[2];b->y0=y+h[1];b->y1=y+h[3];
 b->previous_x0=bullet_previous_x[slot]+h[0]+bullet_dx(p->type,p->pad&BULLET_LAYOUT,PDIR(p),member);b->previous_y0=bullet_previous_y[slot]+h[1]+bullet_dy(p->type,p->pad&BULLET_LAYOUT,PDIR(p),member);
 b->continuous=(p->flags&BULLET_MOVED)!=0;
 return 1;
}
int bullet_scene_active_box(u32 slot,combat_box_t *b){return bullet_scene_member_box(slot,0,b);}
int bullet_scene_group_may_touch(u32 slot,const combat_box_t *b){
 bullet_state_t *p;int x,y,px,py,x0,y0,x1,y1,bx0,by0,bx1,by1;u32 layout;
 if(slot>=BULLET_CAPACITY)return 0;p=&bullet_pool[slot];if(!(p->flags&BULLET_ACTIVE) || !(p->pad&7u))return 0;
 layout=p->pad&BULLET_LAYOUT;x=bullet_floor(p->x_q16);y=bullet_floor(p->y_q16);
 px=(p->flags&BULLET_MOVED)?bullet_previous_x[slot]:x;py=(p->flags&BULLET_MOVED)?bullet_previous_y[slot]:y;
 /* Group hull of both endpoints: every member's whole 8x12 cell, a superset of its hitbox. */
 {int ox=layout?rot_min[TRI_SHAPE(p->type)][PDIR(p)][0]:0,oy=layout?rot_min[TRI_SHAPE(p->type)][PDIR(p)][1]:0; /* GHD5R: rotated bbox */
 x0=(x<px?x:px)+ox;y0=(y<py?y:py)+oy;x1=(x>px?x:px)+ox+(layout?(int)rot_w[TRI_SHAPE(p->type)][PDIR(p)]:BULLET_MEMBER_DX+8);y1=(y>py?y:py)+oy+(layout?(int)rot_h[TRI_SHAPE(p->type)][PDIR(p)]:12);}
 bx0=b->x0<b->previous_x0?b->x0:b->previous_x0;by0=b->y0<b->previous_y0?b->y0:b->previous_y0;
 bx1=(b->x0>b->previous_x0?b->x0:b->previous_x0)+(b->x1-b->x0);by1=(b->y0>b->previous_y0?b->y0:b->previous_y0)+(b->y1-b->y0);
 return x0<bx1 && bx0<x1 && y0<by1 && by0<y1;
}
void bullet_scene_kill_member(u32 slot,u32 member){
 if(!bullet_stage && slot<BULLET_CAPACITY && member<BULLET_MEMBER_MAX){bullet_state_t *p=&bullet_pool[slot];
  p->pad&=~(9u<<member);if(!(p->pad&7u))p->flags=0;}
}
void bullet_scene_kill(u32 slot){if(!bullet_stage && slot<BULLET_CAPACITY)bullet_pool[slot].flags=0;}
void bullet_scene_clear(void){if(!bullet_stage)bullet_pool_clear();}
/* M1: wire descriptor of rank k, from the frozen pool slot; identical to the former collect-time copy:
 * word0 = x | live mask<<16 | layout<<19, word1 = y | (type&3)<<16 | rank<<18. */
static bullet_object_desc_t bullet_desc(u32 k){
 const bullet_state_t *p=&bullet_pool[bullet_pending[k]];bullet_object_desc_t d;
 {int ox=0,oy=0;u32 dw=0; /* GHD5R: triple anchor = rotated bbox top-left, direction in word0[23:20] */
  if(p->pad&BULLET_LAYOUT){ox=rot_min[TRI_SHAPE(p->type)][PDIR(p)][0];oy=rot_min[TRI_SHAPE(p->type)][PDIR(p)][1];dw=PDIR(p)<<20;}
 d.word[0]=(uint16_t)(bullet_floor(p->x_q16)+ox)|((u32)(p->pad&7u)<<16)|((p->pad&BULLET_LAYOUT)?1u<<19:0)|dw;
 d.word[1]=(uint16_t)(bullet_floor(p->y_q16)+oy)|(((u32)p->type<<16)&0x30000u)|(k<<18);}
 return d;
}
int bullet_scene_stage_service(void){
 gpu_result_t r;u32 n;if(!bullet_enabled)return 1;
 if(bullet_stage>=1 && bullet_stage<=3)bullet_stage_calls++;
 if(bullet_stage==1){r=gpu_bullet_scene_open(bullet_pending_count,bullet_sequence);if(r!=GPU_OK)return -1;bullet_stage=2;return 0;}
 if(bullet_stage==2){u32 k;bullet_object_desc_t batch[BULLET_STAGE_BATCH];
  /* O3: same 8 descriptors (16 data writes) per call; checks once per batch. */
  n=bullet_pending_count-bullet_position;if(n>BULLET_STAGE_BATCH)n=BULLET_STAGE_BATCH;
  if(n){
   for(k=0;k<n;k++){batch[k]=bullet_desc(bullet_position+k);if(!gpu_bullet_scene_words_ok(batch[k].word))return -1;}
   if(gpu_bullet_scene_batch_open()!=GPU_OK)return -1;
   for(k=0;k<n;k++)gpu_bullet_scene_put(batch[k].word);
   if(gpu_bullet_scene_batch_close(bullet_position+n)!=GPU_OK)return -1;
   bullet_position+=n;
  }
  if(bullet_position==bullet_pending_count)bullet_stage=3;
  return 0;}
 if(bullet_stage==3){r=gpu_bullet_scene_seal(bullet_pending_count,bullet_sequence);if(r!=GPU_OK)return -1;bullet_stage=4;}
 return bullet_stage==4?1:-1;
}
/* S (PIPE ON): snapshot at claim of everything bullet_scene_confirmed() publishes. */
static struct{rich_stats_t rs;u32 role[RICH_ROLE_COUNT],style[BULLET_STYLE_COUNT],groups,count,active,visible,spawned,dropped,skipped,fan,sequence,stage_calls;}bullet_if;
static u32 bullet_if_valid;
void bullet_scene_claimed(void){if(bullet_enabled && bullet_stage==4){
  bullet_if.rs=rich_stats;memcpy(bullet_if.role,bullet_pending_role_visible,sizeof(bullet_if.role));memcpy(bullet_if.style,bullet_pending_style_visible,sizeof(bullet_if.style));
  bullet_if.groups=bullet_pending_groups;bullet_if.count=bullet_pending_count;bullet_if.active=bullet_pending_active;bullet_if.visible=bullet_pending_visible;
  bullet_if.spawned=bullet_spawned;bullet_if.dropped=bullet_dropped;bullet_if.skipped=bullet_skipped;bullet_if.fan=bullet_fan_spawned;
  bullet_if.sequence=bullet_sequence;bullet_if.stage_calls=bullet_stage_calls;bullet_if_valid=1;bullet_stage=0;}}
u32 bullet_scene_lcg(void){return bullet_lcg;}
void bullet_scene_lcg_restore(u32 v){bullet_lcg=v;}
void bullet_scene_confirmed(void){
 if(bullet_if_valid){rich_display_stats=bullet_if.rs;memcpy(bullet_display_role_visible,bullet_if.role,sizeof(bullet_display_role_visible));
  memcpy(bullet_display_style_visible,bullet_if.style,sizeof(bullet_display_style_visible));
  bullet_display_groups=bullet_if.groups;bullet_display_count=bullet_if.count;bullet_display_active=bullet_if.active;bullet_display_visible=bullet_if.visible;
  bullet_display_spawned=bullet_if.spawned;bullet_display_dropped=bullet_if.dropped;bullet_display_skipped=bullet_if.skipped;bullet_display_fan=bullet_if.fan;
  bullet_display_sequence=bullet_if.sequence;bullet_display_stage_calls=bullet_if.stage_calls;bullet_if_valid=0;return;}
 if(bullet_enabled && bullet_stage==4){
  rich_display_stats=rich_stats;memcpy(bullet_display_role_visible,bullet_pending_role_visible,sizeof(bullet_display_role_visible));
  memcpy(bullet_display_style_visible,bullet_pending_style_visible,sizeof(bullet_display_style_visible));
  bullet_display_groups=bullet_pending_groups;bullet_display_count=bullet_pending_count;bullet_display_active=bullet_pending_active;bullet_display_visible=bullet_pending_visible;
 bullet_display_spawned=bullet_spawned;bullet_display_dropped=bullet_dropped;bullet_display_skipped=bullet_skipped;bullet_display_fan=bullet_fan_spawned;
 bullet_display_sequence=bullet_sequence;bullet_display_stage_calls=bullet_stage_calls;bullet_stage=0;}}
void bullet_scene_cancel(void){if(bullet_enabled)gpu_bullet_scene_cancel();bullet_stage=0;bullet_if_valid=0;}
gpu_result_t bullet_scene_disable(void){return gpu_bullet_scene_mode(0);}
void bullet_scene_restored(void){bullet_if_valid=0;bullet_display_groups=0;bullet_pool_clear();memset(&rich_display_stats,0,sizeof(rich_display_stats));
 memset(bullet_display_role_visible,0,sizeof(bullet_display_role_visible));memset(bullet_display_style_visible,0,sizeof(bullet_display_style_visible));bullet_display_count=bullet_display_active=bullet_display_visible=0;
 bullet_display_spawned=bullet_display_dropped=bullet_display_skipped=bullet_display_fan=0;bullet_display_sequence=bullet_display_stage_calls=0;}
u32 bullet_scene_displayed_active(void){return bullet_display_active;}
u32 bullet_scene_displayed_visible(void){return bullet_display_visible;}
u32 bullet_scene_displayed_spawned(void){return bullet_display_spawned;}
u32 bullet_scene_displayed_dropped(void){return bullet_display_dropped;}
u32 bullet_scene_displayed_skipped(void){return bullet_display_skipped;}
u32 bullet_scene_displayed_fan(void){return bullet_display_fan;}
u32 bullet_scene_confirmed_count(void){return bullet_display_count;}
u32 bullet_scene_confirmed_sequence(void){return bullet_display_sequence;}
u32 bullet_scene_confirmed_stage_calls(void){return bullet_display_stage_calls;}
u32 bullet_scene_lcg_at_begin(void){return bullet_lcg_begin;}
u32 bullet_scene_scan_limit(void){return bullet_enabled?bullet_high_water:0u;}
/* S1 RICH options (idle only) and confirmed diagnostics. */
void bullet_scene_rich_options(u32 lite,u32 stagger){rich_lite=lite!=0;rich_stagger=stagger!=0;}
u32 bullet_scene_rich_option(u32 which){return which==0?rich_lite:(which==1?rich_stagger:rich_lite_active);}
u32 bullet_scene_rich_stat(u32 k){const u32 *s=&rich_display_stats.events;return k<9u?s[k]:0;}
void bullet_scene_rich_pool(u32 cap,u32 quota){rich_cap=cap>BULLET_CAPACITY?BULLET_CAPACITY:cap;rich_quota=quota!=0;}
/* GHDA */
u32 bullet_scene_angles_on(void){return bullet_angles;}
u32 bullet_scene_plane_angle(u32 e){return e<ENEMY_CAPACITY?plane_angle[e]:0u;}
void bullet_scene_set_angles(u32 on){bullet_angles=on!=0;}
u32 bullet_scene_ladder_active(void){return density_ladder() && bullet_enabled && bullet_pattern_mode==3u && bullet_fire_enabled;} /* not on GAME OVER */
u32 bullet_scene_ladder_level(void){return ladder_level;}
const char *bullet_scene_ladder_tag(void){return bullet_scene_ladder_active()?ladder_tags[ladder_level]:LADDER_TIER_TAG;}
u32 bullet_scene_pressure_glow(void){if(!bullet_enabled || bullet_pattern_mode!=3u)return 0;
 return density_ladder()?ladder_glow[ladder_level]:density_glow[density_tier<3u?density_tier:BULLET_DENSITY_DEFAULT];}
u32 bullet_scene_rich_pool_cap(void){return rich_cap;}
u32 bullet_scene_rich_quota(void){return rich_quota;}
u32 bullet_scene_rich_floor(u32 r){return r<RICH_ROLE_COUNT?rich_floor[r]:0;}
u32 bullet_scene_rich_live_role(u32 r){return r<RICH_ROLE_COUNT?rich_live_role[r]:0;}
u32 bullet_scene_displayed_role_visible(u32 role){return role<RICH_ROLE_COUNT?bullet_display_role_visible[role]:0;}
u32 bullet_scene_displayed_style_visible(u32 style){return style<BULLET_STYLE_COUNT?bullet_display_style_visible[style]:0;}
