#include "renderer.h"
#include <string.h>

/* Public dispatch and backend-neutral frame accounting. */

static void renderer_record_result(renderer_t *renderer,
                                   renderer_result_t result)
{
    renderer->stats.last_result = result;
    if (result != RENDERER_OK) {
        renderer->stats.errors++;
        if (renderer->backend == RENDERER_BACKEND_FPGA)
            renderer_fpga_capture_status(renderer);
    }
}

void renderer_init(renderer_t *renderer, renderer_backend_t backend)
{
    if (renderer == 0)
        return;

    memset(renderer, 0, sizeof(*renderer));
    renderer->backend = (backend == RENDERER_BACKEND_CPU) ?
                        RENDERER_BACKEND_CPU : RENDERER_BACKEND_FPGA;
    renderer->stats.last_result = RENDERER_OK;
}

renderer_result_t renderer_set_backend(renderer_t *renderer,
                                       renderer_backend_t backend)
{
    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    if ((backend != RENDERER_BACKEND_FPGA) &&
        (backend != RENDERER_BACKEND_CPU)) {
        renderer_record_result(renderer, RENDERER_ERR_PARAM);
        return RENDERER_ERR_PARAM;
    }
    /* Selecting the current backend is a true no-op, including counters. */
    if (renderer->backend == backend)
        return RENDERER_OK;
    if (renderer->frame_open || renderer->frame_submitted) {
        renderer_record_result(renderer, RENDERER_ERR_BUSY);
        return RENDERER_ERR_BUSY;
    }

    renderer->backend = backend;
    renderer_reset_stats(renderer);
    return RENDERER_OK;
}

renderer_backend_t renderer_get_backend(const renderer_t *renderer)
{
    if (renderer == 0)
        return RENDERER_BACKEND_FPGA;
    return renderer->backend;
}

renderer_result_t renderer_begin_frame(renderer_t *renderer)
{
    renderer_result_t result;

    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    if (renderer->frame_open || renderer->frame_submitted) {
        renderer_record_result(renderer, RENDERER_ERR_BUSY);
        return RENDERER_ERR_BUSY;
    }

    if (renderer->backend == RENDERER_BACKEND_CPU)
        result = renderer_cpu_begin_frame(renderer);
    else
        result = renderer_fpga_begin_frame(renderer);

    renderer_record_result(renderer, result);
    if (result == RENDERER_OK) {
        renderer->frame_open = 1u;
        renderer->frame_item_count = 0u;
        renderer->stats.current_frame_items = 0u;
        renderer->stats.frames_started++;
    }
    return result;
}

renderer_result_t renderer_submit_item(renderer_t *renderer,
                                        const render_item_t *item)
{
    renderer_result_t result;

    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    if (item == 0) {
        renderer_record_result(renderer, RENDERER_ERR_PARAM);
        return RENDERER_ERR_PARAM;
    }
    if (!renderer->frame_open || renderer->frame_submitted) {
        renderer_record_result(renderer, RENDERER_ERR_BUSY);
        return RENDERER_ERR_BUSY;
    }
    if ((item->kind != RENDER_ITEM_RECT) &&
        (item->kind != RENDER_ITEM_BLIT)) {
        renderer_record_result(renderer, RENDERER_ERR_PARAM);
        return RENDERER_ERR_PARAM;
    }

    if (renderer->backend == RENDERER_BACKEND_CPU)
        result = renderer_cpu_submit_item(renderer, item);
    else
        result = renderer_fpga_submit_item(renderer, item);

    renderer_record_result(renderer, result);
    if (result == RENDERER_OK) {
        renderer->frame_item_count++;
        renderer->stats.current_frame_items = renderer->frame_item_count;
        renderer->stats.items_submitted++;
        if (item->kind == RENDER_ITEM_RECT)
            renderer->stats.rect_items++;
        else
            renderer->stats.blit_items++;
    }
    return result;
}

renderer_result_t renderer_submit_frame(renderer_t *renderer)
{
    renderer_result_t result;

    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    if (!renderer->frame_open || renderer->frame_submitted) {
        renderer_record_result(renderer, RENDERER_ERR_BUSY);
        return RENDERER_ERR_BUSY;
    }
    if (renderer->frame_item_count == 0u) {
        renderer_record_result(renderer, RENDERER_ERR_EMPTY_FRAME);
        return RENDERER_ERR_EMPTY_FRAME;
    }

    if (renderer->backend == RENDERER_BACKEND_CPU)
        result = renderer_cpu_submit_frame(renderer);
    else
        result = renderer_fpga_submit_frame(renderer);

    renderer_record_result(renderer, result);
    if (result == RENDERER_OK) {
        renderer->frame_submitted = 1u;
        renderer->stats.frames_submitted++;
    }
    return result;
}

renderer_result_t renderer_wait_done(renderer_t *renderer,
                                     uint32_t poll_limit)
{
    renderer_result_t result;

    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    if (!renderer->frame_submitted) {
        renderer_record_result(renderer, RENDERER_ERR_BUSY);
        return RENDERER_ERR_BUSY;
    }

    if (renderer->backend == RENDERER_BACKEND_CPU)
        result = renderer_cpu_wait_done(renderer, poll_limit);
    else
        result = renderer_fpga_wait_done(renderer, poll_limit);

    renderer_record_result(renderer, result);
    if (result == RENDERER_OK) {
        renderer->stats.frames_completed++;
        renderer->stats.last_frame_items = renderer->frame_item_count;
        renderer->stats.current_frame_items = 0u;
        renderer->frame_item_count = 0u;
        renderer->frame_open = 0u;
        renderer->frame_submitted = 0u;
    }
    return result;
}

renderer_result_t renderer_end_frame(renderer_t *renderer,
                                     uint32_t poll_limit)
{
    renderer_result_t result = renderer_submit_frame(renderer);

    if (result != RENDERER_OK)
        return result;
    return renderer_wait_done(renderer, poll_limit);
}

uint32_t renderer_frame_item_count(const renderer_t *renderer)
{
    if (renderer == 0)
        return 0u;
    return renderer->frame_item_count;
}

const renderer_stats_t *renderer_get_stats(const renderer_t *renderer)
{
    if (renderer == 0)
        return 0;
    return &renderer->stats;
}

void renderer_reset_stats(renderer_t *renderer)
{
    if (renderer == 0)
        return;

    /* Discard old backend snapshots, but do not lose the active frame's item
     * count if counters are reset while recording or waiting for a frame. */
    memset(&renderer->stats, 0, sizeof(renderer->stats));
    renderer->stats.last_result = RENDERER_OK;
    renderer->stats.current_frame_items = renderer->frame_item_count;
}

renderer_result_t renderer_get_last_gpu_perf(const renderer_t *renderer,
                                             gpu_perf_t *perf)
{
    if ((renderer == 0) || (perf == 0))
        return RENDERER_ERR_PARAM;

    if (renderer->backend == RENDERER_BACKEND_CPU)
        return renderer_cpu_get_last_gpu_perf(renderer, perf);
    return renderer_fpga_get_last_gpu_perf(renderer, perf);
}

gpu_result_t renderer_result_to_gpu_result(renderer_result_t result)
{
    switch (result) {
    case RENDERER_OK:                 return GPU_OK;
    case RENDERER_ERR_PARAM:          return GPU_ERR_PARAM;
    case RENDERER_ERR_BUSY:           return GPU_ERR_BUSY;
    case RENDERER_ERR_TIMEOUT:        return GPU_ERR_TIMEOUT;
    case RENDERER_ERR_FIFO_FULL:      return GPU_ERR_FIFO_FULL;
    case RENDERER_ERR_FIFO_NOT_EMPTY: return GPU_ERR_FIFO_NOT_EMPTY;
    case RENDERER_ERR_EMPTY_FRAME:    return GPU_ERR_EMPTY_FRAME;
    case RENDERER_ERR_PUSH_REJECT:    return GPU_ERR_PUSH_REJECT;
    case RENDERER_ERR_HW:             return GPU_ERR_HW;
    case RENDERER_ERR_UNSUPPORTED:    return GPU_ERR_PARAM;
    default:                          return GPU_ERR_HW;
    }
}

