#include "renderer.h"

static renderer_result_t renderer_from_gpu_result(gpu_result_t result)
{
    switch (result) {
    case GPU_OK:                return RENDERER_OK;
    case GPU_ERR_PARAM:         return RENDERER_ERR_PARAM;
    case GPU_ERR_BUSY:          return RENDERER_ERR_BUSY;
    case GPU_ERR_TIMEOUT:       return RENDERER_ERR_TIMEOUT;
    case GPU_ERR_FIFO_FULL:     return RENDERER_ERR_FIFO_FULL;
    case GPU_ERR_FIFO_NOT_EMPTY:return RENDERER_ERR_FIFO_NOT_EMPTY;
    case GPU_ERR_EMPTY_FRAME:   return RENDERER_ERR_EMPTY_FRAME;
    case GPU_ERR_PUSH_REJECT:   return RENDERER_ERR_PUSH_REJECT;
    case GPU_ERR_HW:            return RENDERER_ERR_HW;
    default:                    return RENDERER_ERR_HW;
    }
}

renderer_result_t renderer_fpga_begin_frame(renderer_t *renderer)
{
    gpu_result_t result;

    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    renderer->stats.last_gpu_perf_valid = 0u;
    result = gpu_begin_frame();
    if (result != GPU_OK)
        renderer_fpga_capture_status(renderer);
    return renderer_from_gpu_result(result);
}

renderer_result_t renderer_fpga_submit_item(renderer_t *renderer,
                                            const render_item_t *item)
{
    gpu_result_t result;

    (void)renderer;
    if (item == 0)
        return RENDERER_ERR_PARAM;
    result = gpu_set_target(item->target_surface_id);
    if (result != GPU_OK)
        return renderer_from_gpu_result(result);
    if (item->kind == RENDER_ITEM_RECT) {
        uint32_t center_x;
        uint32_t center_y;

        if ((item->x > 0xFFFFFFFFu - item->width / 2u) ||
            (item->y > 0xFFFFFFFFu - item->height / 2u))
            return RENDERER_ERR_PARAM;
        center_x = item->x + item->width / 2u;
        center_y = item->y + item->height / 2u;
#ifndef HOST_TEST
        if (item->src_offset == GPU_PALETTE_SOURCE)
            result = gpu_push_palette_rect(item->x, item->y, item->color);
        else
#endif
            result = gpu_push_rect(center_x, center_y,
                                   item->width, item->height, item->color);
    } else {
#ifdef HOST_TEST
        /* Legacy host contract supplies only the original FULL mock. The
         * target-local path is exercised by the Step 1 MMIO oracle. */
        result = gpu_push_blit(item->src_offset, item->x, item->y,
                               item->width, item->height, item->src_stride);
#else
        result = gpu_push_blit_target(item->target_surface_id,
                                     item->src_offset, item->x, item->y,
                                     item->width, item->height, item->src_stride);
#endif
    }
    return renderer_from_gpu_result(result);
}

renderer_result_t renderer_fpga_submit_frame(renderer_t *renderer)
{
    gpu_result_t result;
    u32 fifo_status;

    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    fifo_status = gpu_get_fifo_status();
    if ((fifo_status & GPU_FIFO_STATUS_LEVEL_MASK) !=
        renderer->frame_item_count) {
        renderer_fpga_capture_status(renderer);
        return RENDERER_ERR_HW;
    }
    result = gpu_submit_frame();
    if (result != GPU_OK)
        renderer_fpga_capture_status(renderer);
    return renderer_from_gpu_result(result);
}

renderer_result_t renderer_fpga_wait_done(renderer_t *renderer,
                                          uint32_t poll_limit)
{
    gpu_result_t result;
    gpu_perf_t perf;

    if (renderer == 0)
        return RENDERER_ERR_PARAM;

    result = gpu_wait_done(poll_limit);
    renderer_fpga_capture_status(renderer);
    if (result != GPU_OK)
        return renderer_from_gpu_result(result);

    result = gpu_get_perf(&perf);
    if (result == GPU_OK) {
        renderer->stats.last_gpu_perf = perf;
        renderer->stats.last_gpu_perf_valid = 1u;
    }
    renderer_fpga_capture_status(renderer);
    return renderer_from_gpu_result(result);
}

void renderer_fpga_capture_status(renderer_t *renderer)
{
    if (renderer == 0)
        return;
    renderer->stats.last_gpu_status = gpu_get_status();
    renderer->stats.last_gpu_fifo_status = gpu_get_fifo_status();
    renderer->stats.last_gpu_error_info = gpu_get_error_info();
}

renderer_result_t renderer_fpga_get_last_gpu_perf(const renderer_t *renderer,
                                                  gpu_perf_t *perf)
{
    if ((renderer == 0) || (perf == 0))
        return RENDERER_ERR_PARAM;
    if (!renderer->stats.last_gpu_perf_valid)
        return RENDERER_ERR_BUSY;
    *perf = renderer->stats.last_gpu_perf;
    return RENDERER_OK;
}
