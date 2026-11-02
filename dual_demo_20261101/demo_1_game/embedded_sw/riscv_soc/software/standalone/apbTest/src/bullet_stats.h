#ifndef BULLET_STATS_H
#define BULLET_STATS_H
#include <stdint.h>
/* GHD: projectile counts of the displayed frame. Inputs are the confirmed (shown-frame) snapshots of each scene, each counting
 * distinct projectiles with at least one drawn pixel on screen: a partly visible projectile counts once, a retired, hidden or
 * wholly off-screen one counts zero. Enemy bullet groups are expanded to their member mask (pairs/triples/singles), never
 * groups x N. Aircraft, the player ship, the shield and explosion/spark effects are not projectiles.
 *   air     enemy aircraft bullets (visible members of the bullet pool)
 *   missile enemy missiles (fired by aircraft)
 *   turret  decorative turret shots (decor slots; no collision, drawn)
 *   player  player bullets
 *   enemy   air + missile + turret;  total = enemy + player (the HUD BULLETS value)
 *   groups  shown enemy bullet pool groups (slots, not projectiles; for capacity tracking only) */
typedef struct { uint32_t air, missile, turret, player, enemy, total, groups; } bullet_stats_t;
/* Window statistics since bullet_stats_window_reset: shown frames, per-frame sums (64 bit), min/max of total. */
typedef struct { uint32_t frames, total_min, total_max, air_max, turret_max; uint64_t total_sum, air_sum, missile_sum, turret_sum, player_sum; } bullet_stats_window_t;
void bullet_stats_reset(void);                      /* session start: zero the shown frame and the window */
void bullet_stats_window_reset(void);
/* Called once per shown frame after every scene *_confirmed(). */
void bullet_stats_confirmed(uint32_t air, uint32_t missile, uint32_t turret, uint32_t player, uint32_t groups);
const bullet_stats_t *bullet_stats_displayed(void);
const bullet_stats_window_t *bullet_stats_window(void);
#endif
