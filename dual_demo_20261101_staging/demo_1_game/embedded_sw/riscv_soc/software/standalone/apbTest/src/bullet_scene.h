#ifndef BULLET_SCENE_H
#define BULLET_SCENE_H
#include "gpu.h"
#include "enemy_config.h"
#include "combat.h"
typedef struct { u32 word[2]; } bullet_object_desc_t;
void bullet_scene_init(void);
gpu_result_t bullet_scene_select(u32 enable);
u32 bullet_scene_selected(void);
gpu_result_t bullet_scene_pattern(u32 dual);
u32 bullet_scene_dual(void);
u32 bullet_scene_pattern_mode(void);
u32 bullet_scene_displayed_groups(void);
int bullet_scene_member_box(u32 slot,u32 member,combat_box_t *box);
void bullet_scene_kill_member(u32 slot,u32 member);
/* Target counts groups; legacy STRESS is restricted to single mode.
 * 0 = natural fire under the global budget; n = keep n live groups (stress). */
void bullet_scene_set_stress(u32 target);
u32 bullet_scene_stress(void);
/* 0 stops new enemy fire (game over); existing bullets keep moving. */
void bullet_scene_set_fire(u32 enable);
gpu_result_t bullet_scene_begin(u64 now);
void bullet_scene_reset_time(u64 now);
gpu_result_t bullet_scene_next(u64 now);
gpu_result_t bullet_scene_advance(u64 now);
/* Split form used by the game: move every bullet, run combat, then fire. */
gpu_result_t bullet_scene_advance_motion(u64 now);
gpu_result_t bullet_scene_advance_fire(u64 now);
gpu_result_t bullet_scene_collect(void);
int bullet_spawn_count_from(u32 emitter,u32 *live);
int bullet_scene_active_box(u32 slot,combat_box_t *box);
void bullet_scene_kill(u32 slot);
void bullet_scene_clear(void);
int bullet_scene_stage_service(void);
void bullet_scene_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
u32 bullet_scene_lcg(void);
void bullet_scene_lcg_restore(u32 v);
void bullet_scene_confirmed(void);
void bullet_scene_cancel(void);
gpu_result_t bullet_scene_disable(void);
void bullet_scene_restored(void);
u32 bullet_scene_displayed_active(void);
u32 bullet_scene_displayed_visible(void);
u32 bullet_scene_displayed_spawned(void);
u32 bullet_scene_displayed_dropped(void);
u32 bullet_scene_displayed_skipped(void);
u32 bullet_scene_displayed_fan(void);
/* Stage-1 D: confirmed descriptor count/sequence, stage calls of that frame,
 * and the fire LCG state when the current session began. */
u32 bullet_scene_confirmed_count(void);
u32 bullet_scene_confirmed_sequence(void);
u32 bullet_scene_confirmed_stage_calls(void);
u32 bullet_scene_lcg_at_begin(void);
/* H: slots at or above this index are inactive (member_box returns 0). */
u32 bullet_scene_scan_limit(void);
/* S1 RICH (pattern 3). Options: LITE (GAME ON only: FANX period 460 ms) and STAGGER (respawn first shot on a fixed
 * per-plane phase slot), both default off. which: 0 LITE, 1 STAGGER, 2 LITE active in this session.
 * Stats k: 0 events, 1 deferred(budget), 2 deferred(pool), 3 deferred(reserve), 4 late dropped, 5 cease-fire,
 * 6 max wait frames, 7 peak groups (confirmed snapshot, since BEGIN/RESTART). */
void bullet_scene_rich_options(u32 lite,u32 stagger);
u32 bullet_scene_rich_option(u32 which);
u32 bullet_scene_rich_stat(u32 k);
/* BGT-B pool cap / quota (idle only) and diagnostics */
void bullet_scene_rich_pool(u32 cap,u32 quota);
/* GHD3S density tier 0 L / 1 M / 2 H (see bullet_scene.c); selectable in the menu, applies to RICH spawns from the next shot. */
void bullet_scene_density_select(u32 tier);
/* GHDA: plane angles, probe ladder, pressure glow (see bullet_scene.c) */
u32 bullet_scene_angles_on(void);
u32 bullet_scene_plane_angle(u32 e); /* 0 / 1 / 2 = 30 / 45 / 60 deg */
void bullet_scene_set_angles(u32 on);
u32 bullet_scene_ladder_active(void);
u32 bullet_scene_ladder_level(void);
const char *bullet_scene_ladder_tag(void);
u32 bullet_scene_pressure_glow(void);
u32 bullet_scene_density_pool(void); /* GHD4: pressure pool cap of the selected tier */
u32 bullet_scene_density_tier(void);
u32 bullet_dir16(int vx,int vy,u32 prev); /* GHD5R: 16-sector motion direction (0 = +y, 4 = +x) */
void bullet_scene_legacy_period(u32 pct); /* GHD5: LIGHT (pattern 0/1/2) fire period %, 25..100 */
u32 bullet_scene_legacy_period_pct(void);
u32 bullet_scene_rich_pool_cap(void);
u32 bullet_scene_rich_quota(void);
u32 bullet_scene_rich_floor(u32 r);
u32 bullet_scene_rich_live_role(u32 r);
u32 bullet_scene_displayed_role_visible(u32 role);
u32 bullet_scene_displayed_style_visible(u32 style);
/* Prefilter: 0 only if no member of slot can touch box b along either swept path. */
int bullet_scene_group_may_touch(u32 slot,const combat_box_t *b);
#endif
