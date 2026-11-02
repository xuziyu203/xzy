#include "gpu.h"
#include "io.h"

u32 gpu_get_status(void)
{
    return read_u32(GPU_REG_STATUS);
}

u32 gpu_get_fifo_status(void)
{
    return read_u32(GPU_REG_FIFO_STATUS);
}

u32 gpu_get_error_info(void)
{
    return read_u32(GPU_REG_ERROR_INFO);
}

u32 gpu_get_display_status(void)
{
    return read_u32(GPU_REG_DISPLAY_STATUS);
}

u32 gpu_get_display_generation(void)
{
    return read_u32(GPU_REG_DISPLAY_GENERATION);
}

u32 gpu_get_async_control(void) { return read_u32(GPU_REG_ASYNC_CONTROL); }
u32 gpu_get_async_q0_seq(void) { return read_u32(GPU_REG_ASYNC_Q0_SEQ); }
u32 gpu_get_async_gpu_seq(void) { return read_u32(GPU_REG_ASYNC_GPU_SEQ); }
u32 gpu_get_async_render_cycles(void) { return read_u32(GPU_REG_ASYNC_RENDER_CYCLES); }
u32 gpu_get_async_owner_wait(void) { return read_u32(GPU_REG_ASYNC_OWNER_WAIT); }
u32 gpu_get_async_event_status(void) { return read_u32(GPU_REG_ASYNC_EVENT_STATUS); }
u32 gpu_get_panel_status(void) { return read_u32(GPU_REG_PANEL_STATUS); }

gpu_result_t gpu_panel_publish(const gpu_panel_snapshot_t *snapshot)
{
    if (snapshot == 0)
        return GPU_ERR_PARAM;
    /* A busy mailbox owns the published snapshot until the pixel-domain
     * acknowledge returns.  Drop this window instead of overwriting it. */
    if ((gpu_get_panel_status() & GPU_PANEL_STATUS_BUSY) != 0u)
        return GPU_ERR_BUSY;
    write_u32(snapshot->cpu_row, GPU_REG_PANEL_CPU_ROW);
    write_u32(snapshot->blit_row, GPU_REG_PANEL_BLIT_ROW);
    write_u32(snapshot->rect_row, GPU_REG_PANEL_RECT_ROW);
    write_u32(snapshot->blit_time, GPU_REG_PANEL_BLIT_TIME);
    write_u32(snapshot->rect_time, GPU_REG_PANEL_RECT_TIME);
    write_u32(snapshot->flags, GPU_REG_PANEL_FLAGS);
    write_u32(snapshot->error, GPU_REG_PANEL_ERROR);
    write_u32(1u, GPU_REG_PANEL_COMMIT);
    return GPU_OK;
}

gpu_result_t gpu_set_async_enable(int enable)
{
    if ((enable != 0) && (enable != 1))
        return GPU_ERR_PARAM;
    write_u32(GPU_ASYNC_ABI_MAGIC | (enable ? 1u : 0u),
              GPU_REG_ASYNC_CONTROL);
    if ((enable != 0) && ((gpu_get_async_control() & 1u) == 0u))
        return GPU_ERR_BUSY;
    return GPU_OK;
}

gpu_result_t gpu_get_perf(gpu_perf_t *perf)
{
    gpu_perf_t sample;
    u32 status;

    if (perf == 0) {
        return GPU_ERR_PARAM;
    }

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) == 0u &&
        ((status & GPU_STATUS_BUSY) != 0u ||
         (status & GPU_STATUS_DONE) == 0u)) {
        return GPU_ERR_BUSY;
    }

    sample.blockcopy_cycles = read_u32(GPU_REG_BLOCKCOPY_CYCLES);
    sample.blockcopy_total_cycles = read_u32(GPU_REG_BLOCKCOPY_TOTAL_CYCLES);
    sample.rect_last_cycles = read_u32(GPU_REG_RECT_LAST_CYCLES);
    sample.rect_total_cycles = read_u32(GPU_REG_RECT_TOTAL_CYCLES);
    sample.frame_cycles = read_u32(GPU_REG_FRAME_CYCLES);
    sample.rect_count = read_u32(GPU_REG_RECT_COUNT);

    // Recheck readiness; the caller must serialize this read with submit.
    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        *perf = sample;
        return GPU_ERR_HW;
    }
    if ((status & (GPU_STATUS_BUSY | GPU_STATUS_DONE)) != GPU_STATUS_DONE) {
        return GPU_ERR_BUSY;
    }

    *perf = sample;
    return GPU_OK;
}

gpu_result_t gpu_begin_frame(void)
{
    u32 fifo_status;

    u32 status;

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }

    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_EMPTY) == 0u) {
        return GPU_ERR_FIFO_NOT_EMPTY;
    }

    return GPU_OK;
}

gpu_result_t gpu_set_target(u32 target_id)
{
    if (target_id > GPU_TARGET_Q3)
        return GPU_ERR_PARAM;
    write_u32(target_id, GPU_REG_TARGET_CONTEXT);
    return GPU_OK;
}

gpu_result_t gpu_set_display(u32 mode, u32 full_source)
{
    if ((mode > GPU_DISPLAY_MODE_QUAD) ||
        (full_source > GPU_FULL_SOURCE_CPU))
        return GPU_ERR_PARAM;
    write_u32((full_source << 2) | mode, GPU_REG_DISPLAY_CONTROL);
    return GPU_OK;
}

static gpu_result_t gpu_push_rect_flags(
    u32 flags,
    u32 center_x,
    u32 center_y,
    u32 width,
    u32 height,
    u16 color
)
{
    u32 cmd_word0;
    u32 cmd_word1;
    u32 cmd_word2;
    u32 cmd_word3;
    u32 status;
    u32 fifo_status;

    if ((center_x >= GPU_SCREEN_WIDTH) ||
        (center_y >= GPU_SCREEN_HEIGHT) ||
        (width == 0u) ||
        (width > GPU_SCREEN_WIDTH) ||
        (height == 0u) ||
        (height > GPU_SCREEN_HEIGHT)) {
        return GPU_ERR_PARAM;
    }

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }

    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_FULL) != 0u) {
        return GPU_ERR_FIFO_FULL;
    }

    cmd_word0 = GPU_CMD_OP_RECT | (flags << 4) |
                ((center_x & GPU_CMD_FIELD_12BIT_MASK) <<
                    GPU_CMD_WORD0_X_SHIFT) |
                ((center_y & GPU_CMD_FIELD_12BIT_MASK) <<
                    GPU_CMD_WORD0_Y_SHIFT);
    cmd_word1 = (width & GPU_CMD_FIELD_12BIT_MASK) |
                ((height & GPU_CMD_FIELD_12BIT_MASK) <<
                    GPU_CMD_WORD1_HEIGHT_SHIFT);
    cmd_word2 = 0u;
    cmd_word3 = ((u32)color & 0x0000FFFFu) <<
                GPU_CMD_WORD3_COLOR_SHIFT;

    write_u32(cmd_word0, GPU_REG_CMD_WORD0);
    write_u32(cmd_word1, GPU_REG_CMD_WORD1);
    write_u32(cmd_word2, GPU_REG_CMD_WORD2);
    write_u32(cmd_word3, GPU_REG_CMD_WORD3);
    write_u32(GPU_CMD_PUSH, GPU_REG_CMD_PUSH);

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_PUSH_REJECT) != 0u) {
        return GPU_ERR_PUSH_REJECT;
    }

    return GPU_OK;
}

gpu_result_t gpu_push_rect(u32 x, u32 y, u32 w, u32 h, u16 color)
{
    return gpu_push_rect_flags(0u, x, y, w, h, color);
}
gpu_result_t gpu_push_palette_rect(u32 x, u32 y, u16 color)
{
    if (x > 9u * 64u || (x & 63u) || (y != 0u && y != 64u))
        return GPU_ERR_PARAM;
    return gpu_push_rect_flags(1u, x + 32u, y + 32u, 64u, 64u, color);
}
u32 gpu_get_demo_options(void) { return read_u32(GPU_REG_DEMO_OPTIONS); }
gpu_result_t gpu_set_demo_options(u32 options)
{
    u32 status = gpu_get_demo_options();
    if ((status & 0xFFFF0000u) != GPU_DEMO_OPTIONS_MAGIC) return GPU_ERR_HW;
    if (status & 0x100u) return GPU_ERR_BUSY;
    write_u32(GPU_DEMO_OPTIONS_MAGIC | (options & 3u), GPU_REG_DEMO_OPTIONS);
    return (gpu_get_demo_options() & 3u) == (options & 3u) ? GPU_OK : GPU_ERR_BUSY;
}

gpu_result_t gpu_push_blit(
    u32 src_offset,
    u32 dst_x,
    u32 dst_y,
    u32 width,
    u32 height,
    u32 src_stride
)
{
    /* Compatibility entry: validate FULL geometry without changing the
     * target context selected by existing callers. */
    return gpu_push_blit_target(GPU_TARGET_FULL_BLIT, src_offset, dst_x,
                               dst_y, width, height, src_stride);
}

gpu_result_t gpu_push_blit_target(
    u32 target_id, u32 src_offset, u32 dst_x, u32 dst_y,
    u32 width, u32 height, u32 src_stride
)
{
    u32 status;
    u32 fifo_status;
    u32 src_x;
    u32 src_y;
    u32 cmd_word0;
    u32 cmd_word1;
    u32 surface_width = target_id >= GPU_TARGET_Q0 ? 960u : GPU_SCREEN_WIDTH;
    u32 surface_height = target_id >= GPU_TARGET_Q0 ? 540u : GPU_SCREEN_HEIGHT;
    u32 atlas = (src_offset & GPU_PALETTE_SOURCE) != 0u;
    u32 source_width = atlas ? 960u : surface_width;
    u32 source_height = atlas ? 128u : surface_height;
    u32 stride = source_width * 2u;
    src_offset &= ~GPU_PALETTE_SOURCE;

    // Check ranges before subtraction/addition, including hostile u32 inputs.
    if ((target_id > GPU_TARGET_Q3) ||
        (width == 0u) || (width > surface_width) ||
        (height == 0u) || (height > surface_height) ||
        ((width & 7u) != 0u) || ((dst_x & 7u) != 0u) ||
        ((src_offset & 15u) != 0u) ||
        (src_stride != stride) ||
        (src_offset >= stride * source_height) ||
        (dst_x > surface_width - width) ||
        (dst_y > surface_height - height)) {
        return GPU_ERR_PARAM;
    }

    src_x = (src_offset % stride) / 2u;
    src_y = src_offset / stride;
    if ((width > source_width) || (height > source_height) ||
        (src_x > source_width - width) ||
        (src_y > source_height - height)) {
        return GPU_ERR_PARAM;
    }

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }
    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }
    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_FULL) != 0u) {
        return GPU_ERR_FIFO_FULL;
    }

    // Preserve the existing v2 field layout; RECT keeps center coordinates.
    cmd_word0 = GPU_CMD_OP_BLIT | (atlas ? 0x10u : 0u) |
                (dst_x << GPU_CMD_WORD0_X_SHIFT) |
                (dst_y << GPU_CMD_WORD0_Y_SHIFT);
    cmd_word1 = width | (height << GPU_CMD_WORD1_HEIGHT_SHIFT);
    write_u32(cmd_word0, GPU_REG_CMD_WORD0);
    write_u32(cmd_word1, GPU_REG_CMD_WORD1);
    write_u32(src_offset, GPU_REG_CMD_WORD2);
    write_u32(src_stride, GPU_REG_CMD_WORD3);
    write_u32(GPU_CMD_PUSH, GPU_REG_CMD_PUSH);

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_PUSH_REJECT) != 0u) {
        return GPU_ERR_PUSH_REJECT;
    }
    return GPU_OK;
}

gpu_result_t gpu_submit_frame(void)
{
    u32 fifo_status;
    u32 status;

    status = gpu_get_status();
    if ((status & GPU_STATUS_ERROR) != 0u) {
        return GPU_ERR_HW;
    }

    if ((status & GPU_STATUS_BUSY) != 0u) {
        return GPU_ERR_BUSY;
    }

    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_EMPTY) != 0u) {
        return GPU_ERR_EMPTY_FRAME;
    }

    write_u32(GPU_CTRL_FRAME_START, GPU_REG_CTRL);

    return GPU_OK;
}

gpu_result_t gpu_wait_done(u32 poll_limit)
{
    u32 poll_count;
    u32 status;
    u32 error_info;

    for (poll_count = 0u;
         poll_count < poll_limit;
         poll_count++) {
        status = gpu_get_status();

        if ((status & GPU_STATUS_ERROR) != 0u) {
            error_info = gpu_get_error_info();
            (void)error_info;
            return GPU_ERR_HW;
        }

        if ((status & GPU_STATUS_DONE) != 0u) {
            return GPU_OK;
        }
    }

    return GPU_ERR_TIMEOUT;
}

gpu_result_t gpu_draw_rect(
    u32 center_x,
    u32 center_y,
    u32 width,
    u32 height,
    u16 color
)
{
    gpu_result_t result;

    result = gpu_begin_frame();
    if (result != GPU_OK) {
        return result;
    }

    result = gpu_push_rect(center_x, center_y, width, height, color);
    if (result != GPU_OK) {
        return result;
    }

    result = gpu_submit_frame();
    if (result != GPU_OK) {
        return result;
    }

    return gpu_wait_done(GPU_DONE_POLL_LIMIT);
}
