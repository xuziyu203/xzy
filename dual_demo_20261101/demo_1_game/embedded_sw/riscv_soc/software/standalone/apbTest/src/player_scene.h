#ifndef PLAYER_SCENE_H
#define PLAYER_SCENE_H
#include "gpu.h"
#include "combat.h"
void player_scene_init(void);
gpu_result_t player_scene_select(u32 enable);
u32 player_scene_selected(void);
gpu_result_t player_scene_begin(void);
gpu_result_t player_scene_phase_begin(void);
gpu_result_t player_scene_next(u64 now);
void player_scene_claimed(void);   /* S: snapshot at claim (PIPE ON only) */
void player_scene_defer(u32 on);
void player_scene_drop(void);
gpu_result_t player_scene_apply(void);
void player_scene_confirmed(void);
gpu_result_t player_scene_disable(void);
void player_scene_restored(void);
u32 player_scene_displayed_visible(void);
u32 player_scene_displayed_x(void);
u32 player_scene_displayed_y(void);
u32 player_scene_pending_x(void);
u32 player_scene_pending_y(void);
int player_scene_box(combat_box_t *box);
void player_scene_center(int32_t *cx,int32_t *cy);
#endif
