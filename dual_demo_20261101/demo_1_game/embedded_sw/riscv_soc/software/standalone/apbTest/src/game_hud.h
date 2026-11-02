#ifndef GAME_HUD_H
#define GAME_HUD_H
#include <stdint.h>
/* Main-loop only. confirmed() is called after display acceptance, not submit. */
void game_hud_begin(void);
void game_hud_confirmed(uint32_t completed_frames, uint32_t visible_objects);
/* Lives/score/game-over rows; published with the next snapshot when changed. */
void game_hud_set_game(uint32_t enabled, uint32_t lives, uint32_t score, uint32_t over);
/* GHD HUD2 (bit ABI 0x48440900 only): alive enemy aircraft, shown projectiles (bullet_stats total, kept in w1, not drawn), shield on
 * in the shown frame, game MODE of the shown frame (GAME_HUD_MODE_LIGHT / _STRESS). Lives are the HP (0..10, battery segments).
 * On another bit these are ignored. GHD8: the MODE line shows LIGHT / STRESS (blank while no game runs). */
enum { GAME_HUD_MODE_NONE = 0u, GAME_HUD_MODE_LIGHT = 1u, GAME_HUD_MODE_STRESS = 2u };
void game_hud_set_hud2(uint32_t enemy, uint32_t bullets, uint32_t shield, unsigned mode);
unsigned game_hud_mode_displayed(void);   /* GHD8: MODE code of the last HUD snapshot input (tests) */
/* GHDA display count (ENEMY and BULLETS lines), HUD only (never fed back): stress && actual > 1000 ? actual + 400 : actual. */
uint32_t game_hud_display_count(uint32_t actual, unsigned stress);
uint32_t game_hud_display_displayed(void); /* GHD9: BULLETS value of the last HUD snapshot input (tests) */
int game_hud_is_hud2(void);
void game_hud_set_tag(const char *tag);   /* GHD: BUILD field text (5 chars), NULL = build tag */
void game_hud_status_panel(void);         /* GHD: HUD2 panel without game rules (menu / asset check), no-op if a HUD runs */
void game_hud_end(void);
void game_hud_service(void);
int game_hud_owns_panel(void);
int game_hud_releasing(void);
#endif
