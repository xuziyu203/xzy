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
 * FPS integer BCD is [19:8]; tenths [27:24], hundredths [31:28],
 * and bit 20 marks a measured FPS sample. Software clamps objects/FPS to
 * 99/999 for the basic demo; invalid samples leave bit 20 clear and render as --. */
#define GPU_PANEL_ROW_FPS_VALID (1u << 20)
#define GPU_PANEL_FLAG_BURST_ON (1u << 0)
#define GPU_PANEL_FLAG_FIFO_ON (1u << 1)
#define GPU_PANEL_FLAG_DBUF_ON (1u << 2)
#define GPU_PANEL_FLAG_OPTIONS_PENDING (1u << 3)
#define GPU_PANEL_FLAG_BLIT_TIME_VALID (1u << 4)
#define GPU_PANEL_FLAG_RECT_TIME_VALID (1u << 5)
#define GPU_PANEL_FLAG_ERROR_VALID (1u << 6)
#define GPU_PANEL_FLAG_FULL_HUD_VALID (1u << 7)
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


/* Showcase ABI: actual HW option readback, not requested UI state. */
#define GPU_REG_DEMO_OPTIONS (IO_APB_SLAVE_0_INPUT + 0x11Cu)
#define GPU_DEMO_OPTIONS_MAGIC 0xB5020000u
#define GPU_DEMO_BURST_ON 1u
#define GPU_DEMO_DBUF_ON 2u
#define GPU_DEMO_FIFO_ON 4u
#define GPU_PALETTE_SOURCE 0x80000000u
#define GPU_PALETTE_STRIDE 1920u
u32 gpu_get_demo_options(void);
gpu_result_t gpu_set_demo_options(u32 options);
gpu_result_t gpu_push_palette_rect(u32 x, u32 y, u16 color);

#endif
