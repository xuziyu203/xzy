#include "game.h"
#include "enemy_config.h"
#include "bsp.h"

/* Every duration is measured in game ticks accumulated by game_advance(),
 * so STOP (no frames) freezes invulnerability, survival time and blinking. */
static u32 game_enabled,game_lives,game_score,game_over_flag;
static u64 game_time,game_last_tick,game_invulnerable_until,game_invulnerable_ticks;
static u32 game_display_lives,game_display_score,game_display_over,game_display_invulnerable,game_display_survival_ms;

void game_init(void){game_enabled=0;game_restored();}
void game_select(u32 enable){game_enabled=enable!=0;}
u32 game_selected(void){return game_enabled;}
static u32 game_if_valid;
void game_begin(u64 now){
 game_if_valid=0;
 game_lives=PLAYER_LIVES;game_score=0;game_over_flag=0;game_time=0;game_last_tick=now;
 game_invulnerable_ticks=(u64)BSP_CLINT_HZ*PLAYER_INVULNERABLE_MS/1000u;
 /* A short grace period at the start of every round. */
 game_invulnerable_until=game_invulnerable_ticks;
 game_confirmed();
}
void game_reset_time(u64 now){game_last_tick=now;}
void game_advance(u64 now){
 u64 dt=now>=game_last_tick?now-game_last_tick:0;game_last_tick=now;
 if(game_enabled && !game_over_flag)game_time+=dt;
}
int game_running(void){return game_enabled && !game_over_flag;}
int game_over(void){return game_enabled && game_over_flag;}
int game_player_vulnerable(void){return game_running() && game_time>=game_invulnerable_until;}
int game_player_hit(u64 now){
 (void)now;
 if(!game_player_vulnerable())return 0;
 if(game_lives)game_lives--;
 if(!game_lives)game_over_flag=1;
 else game_invulnerable_until=game_time+game_invulnerable_ticks;
 return 1;
}
void game_add_score(u32 points){if(game_running() && game_score<=99999999u-points)game_score+=points;}
int game_player_visible(void){
 if(!game_enabled)return 1;
 if(game_over_flag)return 0;
 if(game_time>=game_invulnerable_until)return 1;
 /* 100 ms on / 100 ms off while invulnerable. */
 return ((game_time/((u64)BSP_CLINT_HZ/10u))&1u)==0;
}
static u32 game_if[5]; /* S (PIPE ON): snapshot at claim; game_if_valid declared above game_begin */
void game_claimed(void){game_if[0]=game_lives;game_if[1]=game_score;game_if[2]=game_over_flag;game_if[3]=game_running() && game_time<game_invulnerable_until;
 game_if[4]=(u32)(game_time/((u64)BSP_CLINT_HZ/1000u));game_if_valid=1;}
void game_confirmed(void){
 if(game_if_valid){game_display_lives=game_if[0];game_display_score=game_if[1];game_display_over=game_if[2];game_display_invulnerable=game_if[3];
  game_display_survival_ms=game_if[4];game_if_valid=0;return;}
 game_display_lives=game_lives;game_display_score=game_score;game_display_over=game_over_flag;
 game_display_invulnerable=game_running() && game_time<game_invulnerable_until;
 game_display_survival_ms=(u32)(game_time/((u64)BSP_CLINT_HZ/1000u));
}
void game_restored(void){
 game_if_valid=0;
 game_lives=game_score=game_over_flag=0;game_time=game_invulnerable_until=0;
 game_display_lives=game_display_score=game_display_over=game_display_invulnerable=game_display_survival_ms=0;
}
u32 game_displayed_lives(void){return game_display_lives;}
u32 game_displayed_score(void){return game_display_score;}
u32 game_displayed_over(void){return game_display_over;}
u32 game_displayed_invulnerable(void){return game_display_invulnerable;}
u32 game_displayed_survival_ms(void){return game_display_survival_ms;}
