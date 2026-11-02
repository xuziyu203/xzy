#ifndef EFFECT_SCENE_H
#define EFFECT_SCENE_H
#include "gpu.h"
#include "enemy_config.h"
/* FX2 bounded transient effects and an independent persistent shield. Time advances only through
 * effect_scene_advance(), so STOP/MOVE (reset_time) freezes and resumes the
 * animation without catch-up. Spawning never affects damage or scoring. */
enum { EFFECT_SMALL=0, EFFECT_LARGE=1, EFFECT_SPARK=2, EFFECT_SHIELD=3 };
/* A death may evict a lower-priority (older) effect; plain effects never evict. */
enum { EFFECT_PRIORITY_SPARK=0, EFFECT_PRIORITY_DEATH=1, EFFECT_PRIORITY_BOSS=2 };
enum { SHIELD_OFF=0, SHIELD_VISUAL=1, SHIELD_GUARD=2, SHIELD_MANUAL=3 };
typedef struct { u64 remaining;u32 sequence,session,absorbed,merged;u32 flags,counts; } effect_snapshot_t;
gpu_result_t effect_shield_select(u32 mode);
u32 effect_shield_mode(void);
gpu_result_t effect_shield_manual_enable(void); /* FIX: KEY2 / PLAYER SHIELD request, consumed in a legal game frame */
/* GHD: KEY2 / PLAYER SHIELD toggle. MANUAL -> OFF, otherwise -> MANUAL (no duration, no charges, unlimited absorb).
 * Only effect_scene_begin (new game / restart: shield_mode, OFF in the game app) and game over clear it; frame updates and the
 * light -> pressure phase switch keep it. Returns GPU_OK when the state changed. */
gpu_result_t effect_shield_toggle(void);
u32 effect_shield_on(void);          /* pending state: MANUAL and visible rules hold (protection uses the same mode) */
u32 effect_shield_toggles(void);     /* accepted toggles since effect_scene_begin */
u32 effect_shield_absorbed(void);    /* absorbed enemy bullets/missiles since effect_scene_begin */
int effect_shield_absorb(void);
void effect_scene_spark(u32 enemy,int32_t cx,int32_t cy,u64 now);
const effect_snapshot_t *effect_scene_snapshot(void);
void effect_scene_init(void);
gpu_result_t effect_scene_select(u32 enable);
u32 effect_scene_selected(void);
gpu_result_t effect_scene_begin(u64 now);
gpu_result_t effect_scene_phase_begin(u64 now);
void effect_scene_reset_time(u64 now);
/* Registers an animation centred at (cx,cy); returns 1 when placed. */
int effect_scene_spawn(u32 type,int32_t cx,int32_t cy,u32 priority,u64 now);
gpu_result_t effect_scene_advance(u64 now);
gpu_result_t effect_scene_collect(void);
int effect_scene_stage_service(void);
void effect_scene_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
void effect_scene_confirmed(void);
void effect_scene_cancel(void);
gpu_result_t effect_scene_disable(void);
void effect_scene_restored(void);
void effect_scene_clear(void);
u32 effect_scene_displayed_active(void);
u32 effect_scene_displayed_spawned(void);
u32 effect_scene_displayed_dropped(void);
u32 effect_scene_displayed_evicted(void);
#endif
