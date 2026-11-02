#ifndef PLAYER_BULLET_SCENE_H
#define PLAYER_BULLET_SCENE_H
#include "gpu.h"
#include "enemy_config.h"
#include "combat.h"
#define PLAYER_BULLET_REQUIRED_ASSETS 0x00003000u
typedef struct { u32 word[2]; } player_bullet_object_desc_t;
void player_bullet_scene_init(void);
gpu_result_t player_bullet_scene_select(u32 enable);
u32 player_bullet_scene_selected(void);
gpu_result_t player_bullet_scene_begin(u64 now);
void player_bullet_scene_reset_time(u64 now);
gpu_result_t player_bullet_scene_next(u64 now);
gpu_result_t player_bullet_scene_advance(u64 now);
gpu_result_t player_bullet_scene_collect(void);
int player_bullet_scene_active_box(u32 slot,combat_box_t *box);
void player_bullet_scene_kill(u32 slot);
void player_bullet_scene_set_fire(u32 enable); /* 0 while the player is down */
int player_bullet_scene_stage_service(void);
void player_bullet_scene_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
void player_bullet_scene_confirmed(void);
void player_bullet_scene_cancel(void);
gpu_result_t player_bullet_scene_disable(void);
void player_bullet_scene_restored(void);
u32 player_bullet_scene_displayed_active(void);
u32 player_bullet_scene_displayed_visible(void);
u32 player_bullet_scene_displayed_spawned(void);
u32 player_bullet_scene_displayed_dropped(void);
u32 player_bullet_scene_displayed_skipped(void);
int player_bullet_scene_displayed(u32 slot,player_bullet_object_desc_t *object);
#endif
