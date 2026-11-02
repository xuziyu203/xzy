#ifndef MISSILE_SCENE_H
#define MISSILE_SCENE_H
#include "gpu.h"
#include "combat.h"
/* Independent 8 slots, four blue MINI03 and four red BOSS. No ordinary group,
 * PB, transient FX or shield slot is ever borrowed. Published counters describe
 * the same confirmed frame, never pending or per-colour maxima. */
typedef struct { u32 word[2]; } missile_object_desc_t;
void missile_scene_init(void);
gpu_result_t missile_scene_select(u32 enable);
u32 missile_scene_selected(void);
gpu_result_t missile_scene_begin(u64 now);
void missile_scene_reset_time(u64 now);
void missile_scene_set_fire(u32 enable);
gpu_result_t missile_scene_advance_motion(u64 now);
gpu_result_t missile_scene_advance_fire(u64 now);
gpu_result_t missile_scene_collect(void);
int missile_scene_active_box(u32 slot,combat_box_t *box);
void missile_scene_kill(u32 slot);
int missile_scene_stage_service(void);
void missile_scene_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
void missile_scene_confirmed(void);
void missile_scene_cancel(void);
gpu_result_t missile_scene_disable(void);
void missile_scene_restored(void);
u32 missile_scene_displayed_active(void);
u32 missile_scene_displayed_visible(void);
u32 missile_scene_displayed_type(u32 type,u32 field);
u32 missile_scene_displayed_spawned(void);
u32 missile_scene_displayed_dropped(void);
/* S1: 0 while PATTERN RICH (set at BEGIN/RESTART); suppressed = blue spawn slots skipped since then. */
u32 missile_scene_blue_enabled(void);
u32 missile_scene_blue_suppressed(void);
#endif
