#ifndef SRC_GPU_H_
#define SRC_GPU_H_

#include <stdint.h>
#include "soc.h"
#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPU_REG_TEST_OFFSET        0x00000000u
#define GPU_REG_CTRL_OFFSET        0x00000004u
#define GPU_REG_STATUS_OFFSET      0x00000008u
#define GPU_REG_CMD_WORD0_OFFSET   0x0000000Cu
#define GPU_REG_CMD_WORD1_OFFSET   0x00000010u
#define GPU_REG_CMD_WORD2_OFFSET   0x00000014u
#define GPU_REG_CMD_WORD3_OFFSET   0x00000018u
#define GPU_REG_CMD_PUSH_OFFSET    0x0000001Cu
#define GPU_REG_FIFO_STATUS_OFFSET 0x00000020u
#define GPU_REG_ERROR_INFO_OFFSET  0x00000024u
#define GPU_REG_BLOCKCOPY_CYCLES_OFFSET 0x00000028u
#define GPU_REG_BLOCKCOPY_TOTAL_CYCLES_OFFSET 0x0000003Cu
#define GPU_REG_RECT_LAST_CYCLES_OFFSET 0x0000002Cu
#define GPU_REG_RECT_TOTAL_CYCLES_OFFSET 0x00000030u
#define GPU_REG_FRAME_CYCLES_OFFSET 0x00000034u
#define GPU_REG_RECT_COUNT_OFFSET 0x00000038u
#define GPU_REG_TARGET_CONTEXT_OFFSET 0x00000060u
#define GPU_REG_DISPLAY_CONTROL_OFFSET 0x00000064u
#define GPU_REG_DISPLAY_GENERATION_OFFSET 0x00000068u
#define GPU_REG_DISPLAY_STATUS_OFFSET 0x0000006Cu
#define GPU_REG_ASYNC_CONTROL_OFFSET 0x0000008Cu
#define GPU_REG_ASYNC_Q0_SEQ_OFFSET 0x00000090u
#define GPU_REG_ASYNC_GPU_SEQ_OFFSET 0x00000094u
#define GPU_REG_ASYNC_RENDER_CYCLES_OFFSET 0x00000098u
#define GPU_REG_ASYNC_OWNER_WAIT_OFFSET 0x0000009Cu
#define GPU_REG_ASYNC_EVENT_STATUS_OFFSET 0x000000A0u
#define GPU_REG_PANEL_CPU_ROW_OFFSET 0x000000A4u
#define GPU_REG_PANEL_BLIT_ROW_OFFSET 0x000000A8u
#define GPU_REG_PANEL_RECT_ROW_OFFSET 0x000000ACu
#define GPU_REG_PANEL_BLIT_TIME_OFFSET 0x000000B0u
#define GPU_REG_PANEL_RECT_TIME_OFFSET 0x000000B4u
#define GPU_REG_PANEL_FLAGS_OFFSET 0x000000B8u
#define GPU_REG_PANEL_ERROR_OFFSET 0x000000BCu
#define GPU_REG_PANEL_COMMIT_OFFSET 0x000000C0u
#define GPU_REG_PANEL_STATUS_OFFSET 0x000000C4u

#define GPU_REG_TEST        (IO_APB_SLAVE_0_INPUT + GPU_REG_TEST_OFFSET)
#define GPU_REG_CTRL        (IO_APB_SLAVE_0_INPUT + GPU_REG_CTRL_OFFSET)
#define GPU_REG_STATUS      (IO_APB_SLAVE_0_INPUT + GPU_REG_STATUS_OFFSET)
#define GPU_REG_CMD_WORD0   (IO_APB_SLAVE_0_INPUT + GPU_REG_CMD_WORD0_OFFSET)
#define GPU_REG_CMD_WORD1   (IO_APB_SLAVE_0_INPUT + GPU_REG_CMD_WORD1_OFFSET)
#define GPU_REG_CMD_WORD2   (IO_APB_SLAVE_0_INPUT + GPU_REG_CMD_WORD2_OFFSET)
#define GPU_REG_CMD_WORD3   (IO_APB_SLAVE_0_INPUT + GPU_REG_CMD_WORD3_OFFSET)
#define GPU_REG_CMD_PUSH    (IO_APB_SLAVE_0_INPUT + GPU_REG_CMD_PUSH_OFFSET)
#define GPU_REG_FIFO_STATUS (IO_APB_SLAVE_0_INPUT + GPU_REG_FIFO_STATUS_OFFSET)
#define GPU_REG_ERROR_INFO  (IO_APB_SLAVE_0_INPUT + GPU_REG_ERROR_INFO_OFFSET)
#define GPU_REG_BLOCKCOPY_CYCLES (IO_APB_SLAVE_0_INPUT + GPU_REG_BLOCKCOPY_CYCLES_OFFSET)
#define GPU_REG_BLOCKCOPY_TOTAL_CYCLES (IO_APB_SLAVE_0_INPUT + GPU_REG_BLOCKCOPY_TOTAL_CYCLES_OFFSET)
#define GPU_REG_RECT_LAST_CYCLES (IO_APB_SLAVE_0_INPUT + GPU_REG_RECT_LAST_CYCLES_OFFSET)
#define GPU_REG_RECT_TOTAL_CYCLES (IO_APB_SLAVE_0_INPUT + GPU_REG_RECT_TOTAL_CYCLES_OFFSET)
#define GPU_REG_FRAME_CYCLES (IO_APB_SLAVE_0_INPUT + GPU_REG_FRAME_CYCLES_OFFSET)
#define GPU_REG_RECT_COUNT (IO_APB_SLAVE_0_INPUT + GPU_REG_RECT_COUNT_OFFSET)
#define GPU_REG_TARGET_CONTEXT (IO_APB_SLAVE_0_INPUT + GPU_REG_TARGET_CONTEXT_OFFSET)
#define GPU_REG_DISPLAY_CONTROL (IO_APB_SLAVE_0_INPUT + GPU_REG_DISPLAY_CONTROL_OFFSET)
#define GPU_REG_DISPLAY_STATUS (IO_APB_SLAVE_0_INPUT + GPU_REG_DISPLAY_STATUS_OFFSET)
#define GPU_REG_DISPLAY_GENERATION (IO_APB_SLAVE_0_INPUT + GPU_REG_DISPLAY_GENERATION_OFFSET)
#define GPU_REG_ASYNC_CONTROL (IO_APB_SLAVE_0_INPUT + GPU_REG_ASYNC_CONTROL_OFFSET)
#define GPU_REG_ASYNC_Q0_SEQ (IO_APB_SLAVE_0_INPUT + GPU_REG_ASYNC_Q0_SEQ_OFFSET)
#define GPU_REG_ASYNC_GPU_SEQ (IO_APB_SLAVE_0_INPUT + GPU_REG_ASYNC_GPU_SEQ_OFFSET)
#define GPU_REG_ASYNC_RENDER_CYCLES (IO_APB_SLAVE_0_INPUT + GPU_REG_ASYNC_RENDER_CYCLES_OFFSET)
#define GPU_REG_ASYNC_OWNER_WAIT (IO_APB_SLAVE_0_INPUT + GPU_REG_ASYNC_OWNER_WAIT_OFFSET)
#define GPU_REG_ASYNC_EVENT_STATUS (IO_APB_SLAVE_0_INPUT + GPU_REG_ASYNC_EVENT_STATUS_OFFSET)
#define GPU_REG_PANEL_CPU_ROW (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_CPU_ROW_OFFSET)
#define GPU_REG_PANEL_BLIT_ROW (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_BLIT_ROW_OFFSET)
#define GPU_REG_PANEL_RECT_ROW (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_RECT_ROW_OFFSET)
#define GPU_REG_PANEL_BLIT_TIME (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_BLIT_TIME_OFFSET)
#define GPU_REG_PANEL_RECT_TIME (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_RECT_TIME_OFFSET)
#define GPU_REG_PANEL_FLAGS (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_FLAGS_OFFSET)
#define GPU_REG_PANEL_ERROR (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_ERROR_OFFSET)
#define GPU_REG_PANEL_COMMIT (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_COMMIT_OFFSET)
#define GPU_REG_PANEL_STATUS (IO_APB_SLAVE_0_INPUT + GPU_REG_PANEL_STATUS_OFFSET)
#define GPU_ASYNC_ABI_MAGIC 0xA6010000u

/* Q3 OSD row word: object BCD is [7:0] (tens [7:4], ones [3:0]),
 * FPS BCD is [19:8] (hundreds [19:16], tens [15:12], ones [11:8]),
 * and bit 20 marks a measured FPS sample. Software clamps objects/FPS to
 * 99/999; invalid samples leave bit 20 clear and render as --. */
#define GPU_PANEL_ROW_FPS_VALID (1u << 20)
#define GPU_PANEL_FLAG_ASYNC_ON (1u << 0)
#define GPU_PANEL_FLAG_ASYNC_PENDING (1u << 1)
#define GPU_PANEL_FLAG_COLLIDE_CPU (1u << 2)
#define GPU_PANEL_FLAG_COLLIDE_GPU (1u << 3)
#define GPU_PANEL_FLAG_BLIT_TIME_VALID (1u << 4)
#define GPU_PANEL_FLAG_RECT_TIME_VALID (1u << 5)
#define GPU_PANEL_FLAG_ERROR_VALID (1u << 6)
#define GPU_PANEL_FLAG_FULL_HUD_VALID (1u << 7)
#define GPU_PANEL_FLAG_GAME_HUD (1u << 10)
#define GPU_PANEL_FLAG_GAME_RULES (1u << 11) /* blit_row=lives BCD, rect_row=score 6xBCD */
#define GPU_PANEL_FLAG_GAME_OVER (1u << 12)
#define GPU_REG_GAME_HUD_ABI (IO_APB_SLAVE_0_INPUT + 0x000001ECu)
/* HD3.2: OBJECTS hundreds=BLIT[7:4], tens/units=CPU[7:0]; lives=BLIT[3:0]; FPS unchanged. */
#define GPU_GAME_HUD_ABI 0x48440402u
/* GHD HUD2 (rtl/game_hud2_overlay.v, before the q3 overlay): the same 7-word snapshot, flags [13] HUD2 + [7] valid, [10] clear
 * so the q3 game HUD stays off. cpu_row FPS/ENEMY BCD, blit_row BULLETS/HP/level/shield/over/rules, rect_row SCORE, blit_time
 * BUILD tag (5 x 6-bit codes of GPU_HUD2_CHARS). BCD nibble 0xF = blank. Layout model: scripts/hud_design.py. */
#define GPU_GAME_HUD2_ABI 0x48440900u /* GHDA: ENEMY + BULLETS show panel w4 (rect_time) [31:16] = display BCD, [1:0] = MODE */
#define GPU_PANEL_FLAG_HUD2 (1u << 13)
#define GPU_HUD2_CHARS " 0123456789ABCDEFGHIJKLMNOPRSTUVWXY/:.-x"
#define GPU_REG_ENEMY_KEY_CONFIG (IO_APB_SLAVE_0_INPUT + 0x1F0u)
#define GPU_REG_ENEMY_KEY_ABI (IO_APB_SLAVE_0_INPUT + 0x1F4u)
#define GPU_REG_ENEMY_KEY_FRAME (IO_APB_SLAVE_0_INPUT + 0x1F8u)
#define GPU_ENEMY_KEY_ABI 0x434B0101u
#define GPU_ENEMY_KEY_ENABLE 0x00010000u
#define GPU_ENEMY_KEY_CONFIG_MASK 0x0001FFFFu
#define GPU_ENEMY_KEY_FRAME_VALID 0x80000000u
#define GPU_REG_BACKGROUND_ABI (IO_APB_SLAVE_0_INPUT + 0x300u)
#define GPU_REG_BACKGROUND_CONFIG (IO_APB_SLAVE_0_INPUT + 0x304u)
#define GPU_REG_BACKGROUND_FRAME (IO_APB_SLAVE_0_INPUT + 0x308u)
#define GPU_REG_BACKGROUND_ERROR (IO_APB_SLAVE_0_INPUT + 0x30Cu)
#define GPU_BACKGROUND_ABI 0x42470100u
#define GPU_BACKGROUND_IMAGE 0u
#define GPU_BACKGROUND_GRADIENT 1u
#define GPU_BACKGROUND_I4 2u /* BGT-A: BG_I4 + BG_PALETTE, needs GPU_REG_BT_ABI */
#define GPU_REG_BT_ABI (IO_APB_SLAVE_0_INPUT + 0x380u)
#define GPU_BT_ABI 0x42540101u
#define GPU_REG_BG_SOURCE (IO_APB_SLAVE_0_INPUT + 0x324u) /* CMD: 0 game I4, 1 start-menu I4 */
#define GPU_REG_DECOR_CAP (IO_APB_SLAVE_0_INPUT + 0x3D0u) /* CMD: decor slots (old bit 0) */
#define GPU_BACKGROUND_FRAME_VALID 0x80000000u
#define GPU_PANEL_FLAG_FULL_SOURCE_SHIFT 8u
#define GPU_PANEL_FLAG_FULL_SOURCE_MASK (3u << GPU_PANEL_FLAG_FULL_SOURCE_SHIFT)
#define GPU_PANEL_STATUS_BUSY (1u << 0)
#define GPU_ASYNC_ABI_MAGIC 0xA6010000u

#define GPU_DISPLAY_MODE_FULL 0u
#define GPU_DISPLAY_MODE_QUAD 1u
#define GPU_FULL_SOURCE_BLIT 0u
#define GPU_FULL_SOURCE_RECT 1u
#define GPU_FULL_SOURCE_CPU 2u
#define GPU_TARGET_FULL_BLIT 0u
#define GPU_TARGET_FULL_RECT 1u
#define GPU_TARGET_FULL_CPU 2u
#define GPU_TARGET_Q0 3u
#define GPU_TARGET_Q1 4u
#define GPU_TARGET_Q2 5u
#define GPU_TARGET_Q3 6u

#define GPU_CTRL_FRAME_START  (1u << 0)
#define GPU_CTRL_START        GPU_CTRL_FRAME_START
#define GPU_STATUS_BUSY       (1u << 0)
#define GPU_STATUS_DONE       (1u << 1)
#define GPU_STATUS_ERROR      (1u << 2)

#define GPU_CMD_PUSH                  (1u << 0)
#define GPU_FIFO_STATUS_LEVEL_MASK    0x000000FFu
#define GPU_FIFO_STATUS_EMPTY         (1u << 8)
#define GPU_FIFO_STATUS_FULL          (1u << 9)
#define GPU_FIFO_STATUS_PUSH_REJECT   (1u << 10)
#define GPU_FIFO_STATUS_START_EMPTY   (1u << 11)

#define GPU_CMD_PROTOCOL_VERSION      2u

#define GPU_CMD_OP_INVALID            0x0u
#define GPU_CMD_OP_RECT               0x1u
#define GPU_CMD_OP_BLIT               0x2u

#define GPU_CMD_FLAG_COLOR_KEY_ENABLE (1u << 0)
#define GPU_CMD_FLAG_ALPHA_ENABLE     (1u << 1)

#define GPU_CMD_FIELD_12BIT_MASK      0x00000FFFu
#define GPU_CMD_WORD0_X_SHIFT         8u
#define GPU_CMD_WORD0_Y_SHIFT         20u
#define GPU_CMD_WORD1_HEIGHT_SHIFT    12u
#define GPU_CMD_WORD3_COLOR_SHIFT     16u

#define GPU_ERROR_INFO_CODE_MASK      0x000000FFu
#define GPU_ERROR_INFO_OPCODE_MASK    0x00000F00u
#define GPU_ERROR_INFO_FLAGS_MASK     0x0000F000u
#define GPU_ERROR_INFO_OPCODE_SHIFT   8u
#define GPU_ERROR_INFO_FLAGS_SHIFT    12u

#define GPU_HW_ERR_NONE               0x00u
#define GPU_HW_ERR_INVALID_OPCODE     0x01u
#define GPU_HW_ERR_UNSUPPORTED_OPCODE 0x02u
#define GPU_HW_ERR_ILLEGAL_FLAGS      0x03u
#define GPU_HW_ERR_MALFORMED_RECT     0x04u
#define GPU_HW_ERR_MALFORMED_BLIT     0x05u
#define GPU_HW_ERR_FIFO_UNDERFLOW     0x06u
#define GPU_HW_ERR_COUNT_MISMATCH     0x07u
#define GPU_HW_ERR_BLOCK_COPY         0x08u
#define GPU_HW_ERR_EXECUTION_UNIT     0x09u

#define GPU_SCREEN_WIDTH   1920u
#define GPU_SCREEN_HEIGHT  1080u
#define GPU_FRAME_STRIDE_BYTES (GPU_SCREEN_WIDTH * 2u)
#define GPU_FRAME_SIZE_BYTES   (GPU_FRAME_STRIDE_BYTES * GPU_SCREEN_HEIGHT)

#define GPU_DONE_POLL_LIMIT  100000000u

typedef enum {
    GPU_OK = 0,
    GPU_ERR_PARAM,
    GPU_ERR_BUSY,
    GPU_ERR_TIMEOUT,
    GPU_ERR_FIFO_FULL,
    GPU_ERR_FIFO_NOT_EMPTY,
    GPU_ERR_EMPTY_FRAME,
    GPU_ERR_PUSH_REJECT,
    GPU_ERR_HW,
    GPU_ERR_UNSUPPORTED
} gpu_result_t;

typedef struct {
    /* Last BlockCopy task in this frame; zero for RECT-only frames. */
    uint32_t blockcopy_cycles;
    /* Sum of all BlockCopy active windows in this frame. */
    uint32_t blockcopy_total_cycles;
    uint32_t rect_last_cycles;
    uint32_t rect_total_cycles;
    uint32_t frame_cycles;
    uint32_t rect_count;
} gpu_perf_t;

/* All numeric fields are prepared by software before publication.  Row words
 * use packed BCD so the pixel domain only performs glyph lookup.  BLIT/RECT
 * times are complete-frame cycle counts (0xFFFFFFFF means saturated) and
 * ERROR is a raw 32-bit code; both are rendered in hex. */
typedef struct {
    uint32_t cpu_row;
    uint32_t blit_row;
    uint32_t rect_row;
    uint32_t blit_time;
    uint32_t rect_time;
    uint32_t flags;
    uint32_t error;
} gpu_panel_snapshot_t;

/* Serialized caller: read after wait_done and before the next submit.
 * GPU_OK: completed frame. GPU_ERR_HW: frozen partial results are returned.
 * GPU_ERR_BUSY/GPU_ERR_PARAM: *perf is unchanged. 0xFFFFFFFF is saturated.
 */
gpu_result_t gpu_get_perf(gpu_perf_t *perf);

/* Enemy compositor only; never enables generic BLIT flags. Serialized caller
 * must exclude claim and other resource sessions. Whole enable/key word,
 * reserved bits rejected. Busy writes are not accepted, even if value matches.
 * Config persists across BEGIN/STOP/EXIT; hardware reset clears it.
 * frame contains VALID|config from the last accepted claim, not a pending value.
 */
gpu_result_t gpu_enemy_key_get(u32 *config, u32 *frame);
gpu_result_t gpu_enemy_key_configure(u32 config);
/* Independent BG 1.0; no writes on unsupported hardware. FRAME is claim-latched,
 * not display confirmation. Callers exclude all active resource sessions. */
gpu_result_t gpu_background_get(u32 *config,u32 *frame,u32 *error);
gpu_result_t gpu_background_configure(u32 mode);
gpu_result_t gpu_background_source(u32 menu); /* CMD: I4 source select, idle only */

u32 gpu_get_status(void);
u32 gpu_get_fifo_status(void);
u32 gpu_get_error_info(void);
u32 gpu_get_display_status(void);
u32 gpu_get_display_generation(void);
u32 gpu_get_async_control(void);
u32 gpu_get_async_q0_seq(void);
u32 gpu_get_async_gpu_seq(void);
u32 gpu_get_async_render_cycles(void);
u32 gpu_get_async_owner_wait(void);
u32 gpu_get_async_event_status(void);
gpu_result_t gpu_set_async_enable(int enable);
u32 gpu_get_panel_status(void);
gpu_result_t gpu_panel_publish(const gpu_panel_snapshot_t *snapshot);

gpu_result_t gpu_begin_frame(void);
gpu_result_t gpu_set_target(u32 target_id);
gpu_result_t gpu_set_display(u32 mode, u32 full_source);

gpu_result_t gpu_push_rect(
    u32 center_x,
    u32 center_y,
    u32 width,
    u32 height,
    u16 color
);

/* Command v2 BLIT: src_offset is relative to current Front; dst_x/y are
 * top-left coordinates in current Back. RGB565, fixed 3840-byte stride,
 * 16-byte source / 8-pixel destination and width alignment; no clipping.
 * Flags, alpha and color/key are reserved zero. Source and destination must
 * fit their respective frames. Front and Back are separate buffers.
 */
gpu_result_t gpu_push_blit(
    u32 src_offset,
    u32 dst_x,
    u32 dst_y,
    u32 width,
    u32 height,
    u32 src_stride
);

/* Independent multi-enemy table API. 32-bit counts/indices, never HUD digits. */
#define GPU_ENEMY_SCENE_ABI 0x4D450102u /* ME1.2: entries staged in non-decreasing y */
#define GPU_ENEMY_SCENE_BASE 0x200u
gpu_result_t gpu_enemy_scene_capacity(u32 *capacity);
gpu_result_t gpu_enemy_scene_mode(u32 multi);
gpu_result_t gpu_enemy_scene_open(u32 count,u32 sequence);
gpu_result_t gpu_enemy_scene_write(const u32 words[4]);
gpu_result_t gpu_enemy_scene_seal(u32 count,u32 sequence);
void gpu_enemy_scene_cancel(void);
u32 gpu_enemy_scene_frame_sequence(void);
/* Independent BU2.0 table. Bits0..2 ready/staging/error; bit3 word phase,
 * bit4 confirmed-frame valid. Feature bits0..4 signed clip/two words/paired
 * claim/8-bit slot/y-sorted staging. BU2.1 word0[17:16] is live mask;
 * BU4.0: word0 live[18:16], immutable triple-layout[19]; layout1 only style1;
 * layout0 accepts mask0..3 at x+0/20; layout1 accepts mask0..7 at (0,0)/(9,5)/(17,0).
 * Four styles; word1 style[17:16], sort-slot[25:18]. Members draw8x12 from packed16x32 storage at x and x+20; mask00 draws nothing. RTL errors 1 malformed or unsorted,
 * 2 state, 3 incomplete, 4 duplicate. */
#define GPU_BULLET_SCENE_ABI 0x42550700u /* GHD5R: + triple direction word0[23:20] */
#define GPU_PLAYER_ABI 0x504c0100u
gpu_result_t gpu_player_get(u32 *config,u32 *frame);
gpu_result_t gpu_player_configure(u32 enable,u32 x,u32 y);
gpu_result_t gpu_player_configure_sealed(u32 enable,u32 x,u32 y); /* GHD3S: PIPE publish after early staging */
#define GPU_BULLET_SCENE_FEATURES 0xFFu
gpu_result_t gpu_bullet_scene_capacity(u32 *capacity);
gpu_result_t gpu_bullet_scene_mode(u32 enable);
gpu_result_t gpu_bullet_scene_open(u32 count,u32 sequence);
gpu_result_t gpu_bullet_scene_write(const u32 words[2]);
/* O3: validate words without MMIO; then batch_open (one status read), put
 * pairs (two writes each), batch_close (error and written-count reads). */
int gpu_bullet_scene_words_ok(const u32 words[2]);
gpu_result_t gpu_bullet_scene_batch_open(void);
void gpu_bullet_scene_put(const u32 words[2]);
gpu_result_t gpu_bullet_scene_batch_close(u32 written);
gpu_result_t gpu_bullet_scene_seal(u32 count,u32 sequence);
void gpu_bullet_scene_cancel(void);
/* Independent PB2.0: fixed blue, sixteen signed clipped two-word records, y-sorted. */
#define GPU_PLAYER_BULLET_SCENE_ABI 0x50420200u
#define GPU_PLAYER_BULLET_SCENE_FEATURES 0x3fu
gpu_result_t gpu_player_bullet_scene_capacity(u32 *capacity);
gpu_result_t gpu_player_bullet_scene_mode(u32 enable);
gpu_result_t gpu_player_bullet_scene_open(u32 count,u32 sequence);
gpu_result_t gpu_player_bullet_scene_write(const u32 words[2]);
gpu_result_t gpu_player_bullet_scene_seal(u32 count,u32 sequence);
void gpu_player_bullet_scene_cancel(void);
/* FX1.0: up to sixteen alpha-blended effect instances, y-sorted.
 * word0 = type[15] | frame[14:12] | x[11:0] (x multiple of 8), word1 = y.
 * Entries must lie fully on screen; the RTL rejects anything else. */
#define GPU_EFFECT_SCENE_ABI 0x46580101u
#define GPU_EFFECT_SCENE_FEATURES 0x0fu
gpu_result_t gpu_effect_scene_capacity(u32 *capacity);
gpu_result_t gpu_effect_scene_mode(u32 enable);
gpu_result_t gpu_effect_scene_open(u32 count,u32 sequence);
gpu_result_t gpu_effect_scene_write(const u32 words[2]);
gpu_result_t gpu_effect_scene_seal(u32 count,u32 sequence);
void gpu_effect_scene_cancel(void);
gpu_result_t gpu_submit_frame(void);
/* Target-local geometry validation. Caller sets the same target context via
 * gpu_set_target() before enqueue. FULL=1920x1080/3840, Q0..Q3=960x540/1920.
 * Source byte offset is in that target's Front, destination is in its Back.
 * No clipping, alpha, key or cross-surface source is supported. */
gpu_result_t gpu_push_blit_target(
    u32 target_id, u32 src_offset, u32 dst_x, u32 dst_y,
    u32 width, u32 height, u32 src_stride);
gpu_result_t gpu_wait_done(u32 poll_limit);

gpu_result_t gpu_draw_rect(
    u32 center_x,
    u32 center_y,
    u32 width,
    u32 height,
    u16 color
);

#ifdef __cplusplus
}
#endif

/* MS1 independent clipped 8x32 / 12x32; sorted atomic fifth segment. */
#define GPU_MISSILE_SCENE_ABI 0x4D530100u
#define GPU_MISSILE_SCENE_FEATURES 0x0000007Fu
gpu_result_t gpu_missile_scene_capacity(u32 *cap);
void gpu_missile_scene_cancel(void);
gpu_result_t gpu_missile_scene_mode(u32 enable);
gpu_result_t gpu_missile_scene_open(u32 count,u32 sequence);
gpu_result_t gpu_missile_scene_write(const u32 words[2]);
gpu_result_t gpu_missile_scene_seal(u32 count,u32 sequence);
#endif /* SRC_GPU_H_ */
