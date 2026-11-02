#ifndef ENEMY_SCENE_H
#define ENEMY_SCENE_H
#include "gpu.h"
#include "enemy_config.h"
#include "combat.h"
typedef struct { u32 word[4]; } enemy_object_desc_t;
/* 0 = legacy single sprite; 1..WAVE_COUNT select a multi-enemy wave table. */
enum { ENEMY_SCENE_SINGLE=0, ENEMY_SCENE_BOSS4=1, ENEMY_SCENE_WAVE8=2, ENEMY_SCENE_WAVE16=3, ENEMY_SCENE_WAVE35=4, ENEMY_SCENE_WAVE20=5, ENEMY_SCENE_WAVE24=6, ENEMY_SCENE_WAVE32=7, ENEMY_SCENE_WAVE40=8, ENEMY_SCENE_WAVE0=9,
       ENEMY_SCENE_LAST=WAVE_COUNT };
void enemy_scene_init(void);
gpu_result_t enemy_scene_select(u32 scene);
u32 enemy_scene_selected(void);
const char *enemy_scene_name(void);
u32 enemy_scene_required_assets(void);
u32 enemy_scene_displayed_objects(void);
gpu_result_t enemy_scene_begin(void);
gpu_result_t enemy_scene_phase_begin(void);
gpu_result_t enemy_scene_submit(const enemy_object_desc_t *objects,u32 count);
int enemy_scene_stage_service(void); /* -1 failure, 0 pending, 1 sealed; bounded writes */
void enemy_scene_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
void enemy_scene_confirmed(void);
void enemy_scene_cancel(void);
void enemy_scene_restored(void);
void enemy_scene_reset_time(u64 now);
gpu_result_t enemy_scene_next(u64 now);
/* advance/collect let combat remove deaths before sealing the frame.
 * next remains a convenience wrapper for callers without combat. */
gpu_result_t enemy_scene_advance(u64 now);
gpu_result_t enemy_scene_collect(void);
u32 enemy_scene_instance_count(void);
u32 enemy_scene_instance_material(u32 index);
/* S1 RICH role (RICH_ROLE_*), from the wave row or the material; 0 when out of range. */
u32 enemy_scene_instance_role(u32 index);
u32 enemy_scene_lane_guard(void);
int enemy_scene_instance_box(u32 index,combat_box_t *box);
int enemy_scene_instance_center(u32 index,int32_t *cx,int32_t *cy);
int enemy_scene_hit(u32 index,u32 damage,u64 now); /* 1 only on alive->dead */
int enemy_scene_emitter_restarted(u32 index);
/* 0 after game over: dead instances stay dead (no new spawns); begin re-enables. */
void enemy_scene_set_respawn(u32 enable);
u32 enemy_scene_alive_count(void); /* confirmed */
u32 enemy_scene_boss_hp(void); /* confirmed: first large instance */
int enemy_scene_pending_emitter(u32 index,enemy_object_desc_t *object);
int enemy_scene_displayed(u32 index,enemy_object_desc_t *object);
#endif
