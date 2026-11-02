#ifndef DECOR_SCENE_H
#define DECOR_SCENE_H
#include "gpu.h"
/* BGT-A decorative turret shots (bg_turret_overnight_20261028).
 * Three turrets baked into the I4 background, two muzzles each (alternating), fixed straight paths in a 60 Hz tick
 * domain derived from CLINT time (independent of the render rate and of every game RNG). At most DECOR_MAX_ACTIVE
 * shots live; a spawn that finds no free slot is skipped and counted (SKIP_FULL). Decor never collides, damages,
 * is absorbed by the shield, scores, or counts as an ordinary bullet. Drawn by the PL before every game object. */
void decor_scene_init(void);
gpu_result_t decor_scene_select(u32 enable);   /* only while no session is active */
u32 decor_scene_selected(void);
u32 decor_scene_hw_supported(void);
void decor_scene_begin(u64 now);               /* session start / RESTART: no live shot, tick 0 = now */
void decor_scene_reset_time(u64 now);          /* STOP/MOVE/arm: keep the processed tick count, no catch-up */
void decor_scene_advance(u64 now);             /* process every whole 60 Hz tick up to now */
void decor_scene_collect(void);                /* write the slot words (8 or 16 by hardware) + enable for the next claim */
gpu_result_t decor_scene_rate(u32 two_x);      /* CMD: 0 = 1X (2.4 s, max 8), 1 = 2X (1.2 s, max 16); idle only */
u32 decor_scene_rate_2x(void);
void decor_scene_edge_mode(u32 on); /* GHD4: light-mode edge glow shots */
u32 decor_scene_edge(void);
void decor_scene_claimed(void);                /* S: snapshot at claim (PIPE ON only) */
void decor_scene_confirmed(void);              /* publish the counts of the frame that was just shown */
u32 decor_scene_displayed_visible(void);       /* GHD: shown turret shots with a drawn pixel on screen (decor_extent.h) */
u32 decor_word_visible(u32 word);              /* GHD: same rule for one slot word (host tests) */
void decor_scene_disable(void);                /* EXIT / restore: enable 0, slots 0 */
u32 decor_scene_required_assets(void);
void decor_scene_status(void (*text)(const char *),void (*dec)(u64));
/* Host-test accessors (pure state, no I/O). */
u32 decor_scene_live_mask(void);
/* GHDA: up to k (<= 4) glow shots in pressure (turret mode), sharing the 16 slots; live glow shots */
void decor_scene_pressure_glow(u32 k);
u32 decor_scene_glow_extra(void);
u32 decor_scene_live_edge(void);
int decor_scene_slot_word(u32 slot,u32 *word);
u32 decor_scene_ticks(void);
u32 decor_scene_spawned(void);
u32 decor_scene_skipped(void);
u32 decor_scene_retired(void);
#endif
