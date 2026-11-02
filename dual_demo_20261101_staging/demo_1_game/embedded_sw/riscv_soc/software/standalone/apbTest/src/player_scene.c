#include "player_scene.h"
#include "enemy_scene.h"
#include "asset_upload.h"
#include "player_input.h"
#include "game.h"
#include "bsp.h"
/* Independent state: never consumes enemy or bullet slots. Only the matching
 * completed scene publishes position and visibility. Q16 stays CPU-side. */
typedef struct { u32 x,y,enabled,visible; } player_state_t;
static u32 player_selected;
static player_state_t player_pending,player_displayed;
static int64_t player_xq,player_yq;
static u32 player_previous_x,player_previous_y,player_moved,player_opaque;
static const uint8_t player_hitbox[4]=PLAYER_HITBOX_VALUES;
void player_scene_init(void){
 player_selected=0;player_pending=(player_state_t){PLAYER_X,PLAYER_Y,0,0};
 player_displayed=player_pending;
 player_xq=(int64_t)PLAYER_X*65536;player_yq=(int64_t)PLAYER_Y*65536;
 player_input_init();
}
u32 player_scene_selected(void){return player_selected;}
gpu_result_t player_scene_select(u32 enable){
 u32 config,frame;gpu_result_t r;
 if(enable>1u)return GPU_ERR_PARAM;
 if(enable){
  if(enemy_scene_selected()==ENEMY_SCENE_SINGLE)return GPU_ERR_PARAM;
  r=gpu_player_get(&config,&frame);if(r!=GPU_OK)return r;
 }
 player_selected=enable;if(!enable)player_input_revoke(clint_getTime(BSP_CLINT));return GPU_OK;
}
gpu_result_t player_scene_begin(void){
 gpu_result_t r;int opaque=asset_upload_player_mask();
 if(player_selected && opaque<0)return GPU_ERR_PARAM;
 r=gpu_player_configure(player_selected,PLAYER_X,PLAYER_Y);if(r!=GPU_OK)return r;
 player_pending=(player_state_t){PLAYER_X,PLAYER_Y,player_selected,player_selected && opaque>0};
 player_opaque=opaque>0;player_moved=0;player_previous_x=PLAYER_X;player_previous_y=PLAYER_Y;
 player_xq=(int64_t)PLAYER_X*65536;player_yq=(int64_t)PLAYER_Y*65536;
 player_displayed.visible=0;return GPU_OK;
}
gpu_result_t player_scene_phase_begin(void){
 u32 visible=player_displayed.visible;gpu_result_t r=player_scene_begin();
 player_displayed.visible=visible;return r;
}
static u32 player_defer,player_word_valid,player_word_en,player_word_x,player_word_y;
void player_scene_defer(u32 on){player_defer=on!=0;}
void player_scene_drop(void){player_word_valid=0;}
gpu_result_t player_scene_apply(void){if(!player_word_valid)return GPU_OK;player_word_valid=0;return gpu_player_configure_sealed(player_word_en,player_word_x,player_word_y);}
gpu_result_t player_scene_next(u64 now){
 int64_t dx,dy,x,y;int clip_x=0,clip_y=0;gpu_result_t r;
 player_input_service(now,1);player_input_take(&dx,&dy);
 x=player_xq+dx;y=player_yq+dy;
 if(x<0){x=0;clip_x=1;}else if(x>(int64_t)1824*65536){x=(int64_t)1824*65536;clip_x=1;}
 if(y<0){y=0;clip_y=1;}else if(y>(int64_t)984*65536){y=(int64_t)984*65536;clip_y=1;}
 /* Game over hides the ship; invulnerability blinks it. Position still updates. */
 /* S: in the overlap the 0x284 write (accepted only while PL is idle) is held and done by player_scene_apply(). */
 if(player_defer){player_word_en=player_selected && game_player_visible();player_word_x=(u32)(x/65536);player_word_y=(u32)(y/65536);player_word_valid=1;}
 else{r=gpu_player_configure(player_selected && game_player_visible(),(u32)(x/65536),(u32)(y/65536));if(r!=GPU_OK)return r;}
 player_previous_x=player_pending.x;player_previous_y=player_pending.y;player_moved=1;
 player_xq=x;player_yq=y;player_pending.x=(u32)(x/65536);player_pending.y=(u32)(y/65536);
 player_pending.visible=player_selected && game_player_visible() && player_opaque;
 player_input_clip(clip_x,clip_y);
 return GPU_OK;
}
static player_state_t player_if;static u32 player_if_valid;
void player_scene_claimed(void){player_if=player_pending;player_if_valid=1;}
void player_scene_confirmed(void){if(player_if_valid){player_displayed=player_if;player_if_valid=0;return;}player_displayed=player_pending;}
u32 player_scene_displayed_visible(void){return player_displayed.visible;}
u32 player_scene_displayed_x(void){return player_displayed.x;}
u32 player_scene_displayed_y(void){return player_displayed.y;}
u32 player_scene_pending_x(void){return player_pending.x;}
u32 player_scene_pending_y(void){return player_pending.y;}
gpu_result_t player_scene_disable(void){return gpu_player_configure(0,PLAYER_X,PLAYER_Y);}
void player_scene_restored(void){player_displayed.visible=0;player_displayed.enabled=0;}
int player_scene_box(combat_box_t *b){
 if(!b || !player_selected)return 0;
 b->x0=(int32_t)player_pending.x+player_hitbox[0];b->x1=(int32_t)player_pending.x+player_hitbox[2];
 b->y0=(int32_t)player_pending.y+player_hitbox[1];b->y1=(int32_t)player_pending.y+player_hitbox[3];
 b->previous_x0=(int32_t)player_previous_x+player_hitbox[0];b->previous_y0=(int32_t)player_previous_y+player_hitbox[1];
 b->continuous=player_moved;return 1;
}
void player_scene_center(int32_t *cx,int32_t *cy){*cx=(int32_t)player_pending.x+PLAYER_WIDTH/2;*cy=(int32_t)player_pending.y+PLAYER_HEIGHT/2;}
