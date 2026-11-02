#include "monitor.h"

/* RGB565 colours.  The panel intentionally uses a small fixed palette so
 * that the status meaning is visible even before a text/font path exists. */
#define MONITOR_PANEL_COLOR   0x1082u
#define MONITOR_DIM_COLOR     0x39C7u
#define MONITOR_FULL_COLOR    0x07E0u
#define MONITOR_QUAD_COLOR    0x001Fu
#define MONITOR_OBJECT_COLOR  0xFFE0u
#define MONITOR_FPGA_COLOR    0xFD20u
#define MONITOR_CPU_COLOR     0xF81Fu
#define MONITOR_FPS_COLOR     0x07FFu
#define MONITOR_CYCLES_COLOR  0x8410u
#define MONITOR_OK_COLOR      0x07E0u
#define MONITOR_ERROR_COLOR   0xF800u

static uint32_t monitor_clamp_width(uint32_t available, uint32_t value,
                                    uint32_t denominator)
{
    uint64_t scaled;

    if (available == 0u || value == 0u || denominator == 0u)
        return 0u;
    scaled = (uint64_t)available * (uint64_t)value;
    if (scaled >= (uint64_t)available * (uint64_t)denominator)
        return available;
    return (uint32_t)(scaled / (uint64_t)denominator);
}

static renderer_result_t monitor_submit_rect(renderer_t *renderer,
                                              uint32_t x, uint32_t y,
                                              uint32_t width, uint32_t height,
                                              uint16_t color)
{
    render_item_t item;

    /* A zero-valued bar still consumes one FIFO item.  This is essential
     * on the first frame, when FPS and cycle samples do not exist yet. */
    if (width == 0u) {
        width = 1u;
        color = MONITOR_DIM_COLOR;
    }
    item.kind = RENDER_ITEM_RECT;
    item.target_surface_id = GPU_TARGET_Q3;
    /* Q3 is a 960x540 local surface.  monitor_render receives the
     * full-screen origin so the same layout code can describe the HDMI
     * quadrant; translate it before submitting the render item. */
    item.x = (x >= (GPU_SCREEN_WIDTH / 2u)) ?
             (x - (GPU_SCREEN_WIDTH / 2u)) : x;
    item.y = (y >= (GPU_SCREEN_HEIGHT / 2u)) ?
             (y - (GPU_SCREEN_HEIGHT / 2u)) : y;
    item.width = width;
    item.height = height;
    item.color = color;
    item.src_offset = 0u;
    item.src_stride = 0u;
    return renderer_submit_item(renderer, &item);
}

renderer_result_t monitor_render(renderer_t *renderer,
                                 uint32_t origin_x, uint32_t origin_y,
                                 uint32_t width, uint32_t height,
                                 const monitor_info_t *info)
{
    uint32_t inner_x;
    uint32_t inner_y;
    uint32_t inner_width;
    uint32_t row_height;
    uint32_t bar_height;
    uint32_t gap;
    uint32_t y;
    uint32_t bar_width;
    renderer_result_t result;

    if ((renderer == 0) || (info == 0) || (width < 32u) || (height < 48u) ||
        (width > GPU_SCREEN_WIDTH) || (height > GPU_SCREEN_HEIGHT) ||
        (origin_x > GPU_SCREEN_WIDTH - width) ||
        (origin_y > GPU_SCREEN_HEIGHT - height))
        return RENDERER_ERR_PARAM;

    /* The panel itself is one item.  Keep a modest margin so the bars are
     * recognizable when the monitor occupies a QUAD viewport. */
    result = monitor_submit_rect(renderer, origin_x, origin_y,
                                 width, height, MONITOR_PANEL_COLOR);
    if (result != RENDERER_OK)
        return result;

    inner_x = origin_x + 12u;
    inner_y = origin_y + 12u;
    inner_width = width - 24u;
    row_height = (height - 24u) / MONITOR_BAR_COUNT;
    if (row_height < 4u)
        row_height = 4u;
    gap = row_height / 3u;
    if (gap == 0u)
        gap = 1u;
    bar_height = row_height - gap;
    if (bar_height == 0u)
        bar_height = 1u;
    bar_width = inner_width;

    /* The panel provides a common dim track; each foreground bar encodes one
     * value.  This keeps the panel plus six bars bounded by
     * MONITOR_RECT_COUNT. */
    y = inner_y;
    result = monitor_submit_rect(renderer, inner_x, y, bar_width, bar_height,
                                 (info->mode == 0u) ? MONITOR_FULL_COLOR :
                                                       MONITOR_QUAD_COLOR);
    if (result != RENDERER_OK)
        return result;

    y += row_height;
    result = monitor_submit_rect(renderer, inner_x, y,
                                 monitor_clamp_width(bar_width,
                                                     info->object_count, 30u),
                                 bar_height, MONITOR_OBJECT_COLOR);
    if (result != RENDERER_OK)
        return result;

    y += row_height;
    result = monitor_submit_rect(renderer, inner_x, y,
                                 bar_width,
                                 bar_height,
                                 (info->backend == RENDERER_BACKEND_CPU) ?
                                     MONITOR_CPU_COLOR : MONITOR_FPGA_COLOR);
    if (result != RENDERER_OK)
        return result;

    y += row_height;
    result = monitor_submit_rect(renderer, inner_x, y,
                                 monitor_clamp_width(bar_width, info->fps, 60u),
                                 bar_height, MONITOR_FPS_COLOR);
    if (result != RENDERER_OK)
        return result;

    y += row_height;
    result = monitor_submit_rect(renderer, inner_x, y,
                                 monitor_clamp_width(bar_width,
                                                     info->frame_cycles,
                                                     2000000u),
                                 bar_height, MONITOR_CYCLES_COLOR);
    if (result != RENDERER_OK)
        return result;

    /* The error row doubles as a heartbeat: green means the last completed
     * frame reported no error, red means an error counter is non-zero. */
    return monitor_submit_rect(renderer, inner_x, inner_y + 5u * row_height,
                               bar_width, bar_height,
                               (info->error != 0u) ? MONITOR_ERROR_COLOR :
                                                     MONITOR_OK_COLOR);
}
