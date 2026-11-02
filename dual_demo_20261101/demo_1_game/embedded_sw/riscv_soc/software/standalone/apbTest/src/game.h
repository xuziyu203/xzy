#ifndef GAME_H
#define GAME_H
#include "gpu.h"
/* Endless-survival rules: lives, invulnerability, score, game over.
 * All state is pending game state; game_confirmed() publishes the snapshot
 * shown by STATUS/HUD after the matching frame is displayed. */
void game_init(void);
void game_select(u32 enable);
u32 game_selected(void);
void game_begin(u64 now);
void game_reset_time(u64 now);
void game_advance(u64 now);
int game_player_vulnerable(void);
/* Applies one hit; returns 1 when it consumed a life (0 while invulnerable/over). */
int game_player_hit(u64 now);
int game_over(void);
int game_running(void);
void game_add_score(u32 points);
int game_player_visible(void); /* blink while invulnerable, hidden after game over */
void game_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
void game_confirmed(void);
void game_restored(void);
u32 game_displayed_lives(void);
u32 game_displayed_score(void);
u32 game_displayed_over(void);
u32 game_displayed_invulnerable(void);
u32 game_displayed_survival_ms(void);
#endif
