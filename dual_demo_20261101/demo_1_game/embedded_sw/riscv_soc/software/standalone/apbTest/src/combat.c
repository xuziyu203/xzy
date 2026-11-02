#include "combat.h"
#include "enemy_scene.h"
#include "player_bullet_scene.h"
#include "player_scene.h"
#include "bullet_scene.h"
#include "missile_scene.h"
#include "effect_scene.h"
#include "game.h"
#include "bsp.h"
static u32 combat_pb_kills,combat_pb_kills_display;
static u32 combat_hits,combat_kills,combat_player_hits,combat_resolved;
static u32 combat_hits_display,combat_kills_display,combat_player_hits_display;
static u32 combat_ticks,combat_max_ticks,combat_ticks_display,combat_max_ticks_display;
static const u32 combat_scores[ENEMY_MATERIAL_COUNT]=ENEMY_SCORE_VALUES;

/* t = num/den with den > 0. */
typedef struct { int64_t num,den; } combat_time_t;
static int combat_less(combat_time_t a,combat_time_t b){return a.num*b.den<b.num*a.den;}
/* Open interval of t where lo < r0 + t*delta < hi; returns 0 when never inside. */
static int combat_axis(int32_t r0,int32_t delta,int32_t lo,int32_t hi,combat_time_t *enter,combat_time_t *exit,int *bounded){
 if(delta==0){*bounded=0;return r0>lo && r0<hi;}
 *bounded=1;
 if(delta>0){enter->num=(int64_t)lo-r0;exit->num=(int64_t)hi-r0;enter->den=exit->den=delta;}
 else{enter->num=(int64_t)r0-hi;exit->num=(int64_t)r0-lo;enter->den=exit->den=-(int64_t)delta;}
 return 1;
}
int combat_contact(const combat_box_t *a,const combat_box_t *b,u32 *n,u32 *d){
 int32_t aw=a->x1-a->x0,ah=a->y1-a->y0,bw=b->x1-b->x0,bh=b->y1-b->y0;
 int32_t r1x=a->x0-b->x0,r1y=a->y0-b->y0,r0x,r0y;
 combat_time_t lo={0,1},hi={1,1},ex,xx,ey,xy;int bx,by;
 if(!a->continuous || !b->continuous){
  if(r1x<=-aw || r1x>=bw || r1y<=-ah || r1y>=bh)return 0;
  *n=*d=1;return 1;
 }
 r0x=a->previous_x0-b->previous_x0;r0y=a->previous_y0-b->previous_y0;
 if(!combat_axis(r0x,r1x-r0x,-aw,bw,&ex,&xx,&bx) || !combat_axis(r0y,r1y-r0y,-ah,bh,&ey,&xy,&by))return 0;
 if(bx){if(combat_less(lo,ex))lo=ex;if(combat_less(xx,hi))hi=xx;}
 if(by){if(combat_less(lo,ey))lo=ey;if(combat_less(xy,hi))hi=xy;}
 if(!combat_less(lo,hi))return 0;
 *n=(u32)lo.num;*d=(u32)lo.den;return 1;
}
static u32 combat_if_valid;
void combat_init(void){
 combat_if_valid=0;
 combat_pb_kills=combat_pb_kills_display=0;
 combat_hits=combat_kills=combat_player_hits=combat_resolved=0;
 combat_hits_display=combat_kills_display=combat_player_hits_display=0;
 combat_ticks=combat_max_ticks=combat_ticks_display=combat_max_ticks_display=0;
}
void combat_begin_frame(void){combat_resolved=0;}
static void combat_enemy_died(u32 index,u64 now){
 int32_t cx,cy;u32 material=enemy_scene_instance_material(index);
 if(combat_pb_kills!=0xffffffffu)combat_pb_kills++;
 combat_kills++;game_add_score(combat_scores[material]);
 if(enemy_scene_instance_center(index,&cx,&cy))
  effect_scene_spawn(material==ENEMY_MATERIAL_COUNT-1u?EFFECT_LARGE:EFFECT_SMALL,cx,cy,
                     material==ENEMY_MATERIAL_COUNT-1u?EFFECT_PRIORITY_BOSS:EFFECT_PRIORITY_DEATH,now);
}
void combat_resolve(u64 now){
 u32 slot,i,count;u64 start,end;combat_box_t player;int player_live;
 if(combat_resolved)return;
 combat_resolved=1;start=clint_getTime(BSP_CLINT);count=enemy_scene_instance_count();
 /* 1. Player bullets: each one damages only its first contact (stable index ties). */
 for(slot=0;slot<PLAYER_BULLET_CAPACITY;slot++){
  combat_box_t b,e;u32 best=ENEMY_CAPACITY,bn=0,bd=1;
  if(!player_bullet_scene_active_box(slot,&b))continue;
  for(i=0;i<count;i++){
   u32 n,d;
   if(!enemy_scene_instance_box(i,&e) || !combat_contact(&b,&e,&n,&d))continue;
   if(best==ENEMY_CAPACITY || (u64)n*bd<(u64)bn*d){best=i;bn=n;bd=d;}
  }
  if(best!=ENEMY_CAPACITY){
   player_bullet_scene_kill(slot);combat_hits++;
   if(enemy_scene_hit(best,1,now))combat_enemy_died(best,now);
   else if(enemy_scene_instance_material(best)==ENEMY_MATERIAL_COUNT-1u){
    combat_box_t e;int32_t cx=(b.x0+b.x1)/2,cy=(b.y0+b.y1)/2;
    if(enemy_scene_instance_box(best,&e)){
     if(cx<e.x0)cx=e.x0;if(cx>=e.x1)cx=e.x1-1;
     if(cy<e.y0)cy=e.y0;if(cy>=e.y1)cy=e.y1-1;
     effect_scene_spark(best,cx,cy,now);
    }
   }
  }
 }
 /* 2/3. The player, while the game rules run and not invulnerable. */
 player_live=game_player_vulnerable() && player_scene_box(&player);
 if(player_live){
  /* Body contact: a small enemy is destroyed by ramming (no score); a BOSS is not. */
  for(i=0;i<count && game_player_vulnerable();i++){
   combat_box_t e;u32 n,d;
   if(!enemy_scene_instance_box(i,&e) || !combat_contact(&player,&e,&n,&d))continue;
   if(game_player_hit(now)){
    combat_player_hits++;
    if(enemy_scene_instance_material(i)!=ENEMY_MATERIAL_COUNT-1u &&
       enemy_scene_hit(i,0xffffu,now)){int32_t cx,cy;combat_kills++;
     if(enemy_scene_instance_center(i,&cx,&cy))effect_scene_spawn(EFFECT_SMALL,cx,cy,EFFECT_PRIORITY_DEATH,now);}
   }
  }
  /* Enemy bullets: the first contact consumes the bullet; invulnerable players let bullets pass. */
  u32 limit=bullet_scene_scan_limit();
  for(slot=0;slot<limit && game_player_vulnerable();slot++)if(bullet_scene_group_may_touch(slot,&player))for(u32 member=0;member<BULLET_MEMBER_MAX && game_player_vulnerable();member++){
   combat_box_t b;u32 n,d;
   if(!bullet_scene_member_box(slot,member,&b) || !combat_contact(&b,&player,&n,&d))continue;
   if(effect_shield_absorb()){bullet_scene_kill_member(slot,member);}
   else if(game_player_hit(now)){combat_player_hits++;bullet_scene_kill_member(slot,member);}
  }
  /* Independent missiles use their opaque inner box and final swept path.
   * Newborn missiles have continuous=0; PB never intercepts these slots. */
  for(slot=0;slot<MISSILE_CAPACITY && game_player_vulnerable();slot++){
   combat_box_t b;u32 n,d;
   if(!missile_scene_active_box(slot,&b) || !combat_contact(&b,&player,&n,&d))continue;
   if(effect_shield_absorb())missile_scene_kill(slot);
   else if(game_player_hit(now)){combat_player_hits++;missile_scene_kill(slot);}
  }
  if(game_over()){int32_t cx,cy;player_scene_center(&cx,&cy);
   effect_scene_spawn(EFFECT_LARGE,cx,cy,EFFECT_PRIORITY_BOSS,now);}
 }
 end=clint_getTime(BSP_CLINT)-start;
 combat_ticks=end>0xffffffffu?0xffffffffu:(u32)end;
 if(combat_ticks>combat_max_ticks)combat_max_ticks=combat_ticks;
}
u32 combat_displayed_pb_kills(void){return combat_pb_kills_display;}
static u32 combat_if[6]; /* S (PIPE ON): snapshot at claim; combat_if_valid declared above combat_init */
void combat_claimed(void){combat_if[0]=combat_pb_kills;combat_if[1]=combat_hits;combat_if[2]=combat_kills;combat_if[3]=combat_player_hits;combat_if[4]=combat_ticks;combat_if[5]=combat_max_ticks;combat_if_valid=1;}
void combat_confirmed(void){if(combat_if_valid){combat_pb_kills_display=combat_if[0];combat_hits_display=combat_if[1];combat_kills_display=combat_if[2];combat_player_hits_display=combat_if[3];
 combat_ticks_display=combat_if[4];combat_max_ticks_display=combat_if[5];combat_if_valid=0;return;}
 combat_pb_kills_display=combat_pb_kills;combat_hits_display=combat_hits;combat_kills_display=combat_kills;combat_player_hits_display=combat_player_hits;
 combat_ticks_display=combat_ticks;combat_max_ticks_display=combat_max_ticks;}
void combat_restored(void){combat_init();}
u32 combat_displayed_hits(void){return combat_hits_display;}
u32 combat_displayed_kills(void){return combat_kills_display;}
u32 combat_displayed_player_hits(void){return combat_player_hits_display;}
u32 combat_displayed_ticks(void){return combat_ticks_display;}
u32 combat_displayed_max_ticks(void){return combat_max_ticks_display;}
