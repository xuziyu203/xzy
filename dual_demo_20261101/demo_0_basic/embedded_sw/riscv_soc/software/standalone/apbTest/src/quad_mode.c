#include "quad_mode.h"

void quad_layout_init(quad_viewport_t viewports[QUAD_VIEWPORT_COUNT],
                      uint32_t screen_width, uint32_t screen_height)
{
    uint32_t half_width;
    uint32_t half_height;

    if (viewports == 0)
        return;

    half_width = screen_width / 2u;
    half_height = screen_height / 2u;

    viewports[0].origin_x = 0u;
    viewports[0].origin_y = 0u;
    viewports[1].origin_x = half_width;
    viewports[1].origin_y = 0u;
    viewports[2].origin_x = 0u;
    viewports[2].origin_y = half_height;
    viewports[3].origin_x = half_width;
    viewports[3].origin_y = half_height;

    for (uint32_t i = 0u; i < QUAD_VIEWPORT_COUNT; i++) {
        viewports[i].width = half_width;
        viewports[i].height = half_height;
        viewports[i].world_x = 0u;
        viewports[i].world_y = 0u;
        viewports[i].world_width = half_width;
        viewports[i].world_height = half_height;
    }
}

uint32_t quad_safe_object_count(uint32_t requested_count)
{
    return requested_count > QUAD_MAX_OBJECTS ? QUAD_MAX_OBJECTS : requested_count;
}

uint32_t quad_command_count(uint32_t object_count,
                            uint32_t monitor_rect_count)
{
    uint32_t objects = quad_safe_object_count(object_count);
    uint32_t total;

    if (monitor_rect_count > QUAD_MONITOR_RECTS)
        return 0u;
    total = QUAD_BACKGROUND_RECTS + objects + monitor_rect_count;
    if (total > QUAD_FIFO_DEPTH)
        return 0u;
    return total;
}

int quad_object_viewport(uint32_t object_index,
                         uint32_t *viewport_index,
                         uint32_t *viewport_slot)
{
    if ((viewport_index == 0) || (viewport_slot == 0) ||
        (object_index >= QUAD_MAX_OBJECTS))
        return 0;

    /* Round-robin means NUM 10 still lights all three content panels, while
     * NUM 21 fills each content panel with seven blocks exactly. */
    *viewport_index = object_index % QUAD_CONTENT_VIEWPORTS;
    *viewport_slot = object_index / QUAD_CONTENT_VIEWPORTS;
    return 1;
}

int quad_project_object(const quad_viewport_t *viewport,
                        int32_t world_x, int32_t world_y,
                        uint32_t world_width, uint32_t world_height,
                        uint16_t color, quad_rect_t *rect)
{
    if ((viewport == 0) || (rect == 0) ||
        (world_x < 0) || (world_y < 0) ||
        (world_width == 0u) || (world_height == 0u) ||
        (world_width > viewport->width) ||
        (world_height > viewport->height))
        return 0;
    if (((uint32_t)world_x > viewport->width - world_width) ||
        ((uint32_t)world_y > viewport->height - world_height))
        return 0;

    // x/y and dimensions are already surface-local, with no folding/scaling.
    rect->x = (uint32_t)world_x;
    rect->y = (uint32_t)world_y;
    rect->width = world_width;
    rect->height = world_height;
    rect->color = color;
    return 1;
}

renderer_result_t quad_submit_backgrounds(
    renderer_t *renderer,
    const quad_viewport_t viewports[QUAD_VIEWPORT_COUNT],
    const uint16_t colors[QUAD_VIEWPORT_COUNT])
{
    render_item_t item;
    renderer_result_t result;
    uint32_t i;

    if ((renderer == 0) || (viewports == 0) || (colors == 0))
        return RENDERER_ERR_PARAM;
    for (i = 0u; i < QUAD_VIEWPORT_COUNT; i++) {
        item.kind = RENDER_ITEM_RECT;
        item.target_surface_id = GPU_TARGET_Q0 + i;
        item.x = 0u;
        item.y = 0u;
        item.width = viewports[i].width;
        item.height = viewports[i].height;
        item.color = colors[i];
        item.src_offset = 0u;
        item.src_stride = 0u;
        result = renderer_submit_item(renderer, &item);
        if (result != RENDERER_OK)
            return result;
    }
    return RENDERER_OK;
}

renderer_result_t quad_submit_object(
    renderer_t *renderer,
    const quad_viewport_t viewports[QUAD_VIEWPORT_COUNT],
    uint32_t object_index,
    int32_t world_x, int32_t world_y,
    uint32_t world_width, uint32_t world_height,
    uint16_t color)
{
    quad_rect_t rect;
    render_item_t item;
    uint32_t viewport_index;
    uint32_t viewport_slot;

    (void)viewport_slot;
    if ((renderer == 0) || (viewports == 0) ||
        !quad_object_viewport(object_index, &viewport_index, &viewport_slot))
        return RENDERER_ERR_PARAM;
    if (!quad_project_object(&viewports[viewport_index], world_x, world_y,
                             world_width, world_height, color, &rect))
        return RENDERER_ERR_PARAM;
    item.kind = RENDER_ITEM_RECT;
    item.target_surface_id = GPU_TARGET_Q0 + viewport_index;
    item.x = rect.x;
    item.y = rect.y;
    item.width = rect.width;
    item.height = rect.height;
    item.color = rect.color;
    item.src_offset = 0u;
    item.src_stride = 0u;
    return renderer_submit_item(renderer, &item);
}
