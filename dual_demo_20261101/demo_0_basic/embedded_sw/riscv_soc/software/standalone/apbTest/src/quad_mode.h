#ifndef APB_TEST_QUAD_MODE_H
#define APB_TEST_QUAD_MODE_H

/* Software-only QUAD layout.  The FPGA command FIFO has depth 64.  QUAD
 * reserves four panel fills and the monitor renderer reserves seven status
 * rectangles, leaving 53 object rectangles.  RECT is used for these objects
 * because the existing 64x64 BLIT has no scale/clipping fields; FULL keeps
 * the original BLIT path. */

#include <stdint.h>
#include "renderer.h"

#define QUAD_VIEWPORT_COUNT       4u
#define QUAD_CONTENT_VIEWPORTS    3u
#define QUAD_MAX_OBJECTS          53u
#define QUAD_BACKGROUND_RECTS     4u
#define QUAD_MONITOR_RECTS       7u
#define QUAD_FIFO_DEPTH          64u
#define QUAD_OBJECT_RECT_SIZE    64u

typedef struct {
    uint32_t origin_x;
    uint32_t origin_y;
    uint32_t width;
    uint32_t height;
    uint32_t world_x;
    uint32_t world_y;
    uint32_t world_width;
    uint32_t world_height;
} quad_viewport_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint16_t color;
} quad_rect_t;

/* Initialise four half-resolution viewports in TL, TR, BL, BR order. */
void quad_layout_init(quad_viewport_t viewports[QUAD_VIEWPORT_COUNT],
                      uint32_t screen_width, uint32_t screen_height);

/* Clamp a requested scene count to the command budget available in QUAD. */
uint32_t quad_safe_object_count(uint32_t requested_count);

/* Return the complete command count when monitor_rect_count status blocks are
 * emitted by the monitor layer.  Zero means the budget would overflow. */
uint32_t quad_command_count(uint32_t object_count,
                            uint32_t monitor_rect_count);

/* Distribute objects round-robin over the three content quadrants.  The
 * monitor quadrant (index 3) deliberately receives no scene objects. */
int quad_object_viewport(uint32_t object_index,
                         uint32_t *viewport_index,
                         uint32_t *viewport_slot);

/* Validate a surface-local object without folding, clipping or scaling.
 * Screen origins are scanout metadata and never enter GPU coordinates. */
int quad_project_object(const quad_viewport_t *viewport,
                        int32_t world_x, int32_t world_y,
                        uint32_t world_width, uint32_t world_height,
                        uint16_t color, quad_rect_t *rect);

/* Renderer-facing helpers.  They emit only RECT items, so they are safe for
 * QUAD's independent panels without changing the GPU protocol. */
renderer_result_t quad_submit_backgrounds(
    renderer_t *renderer,
    const quad_viewport_t viewports[QUAD_VIEWPORT_COUNT],
    const uint16_t colors[QUAD_VIEWPORT_COUNT]);

renderer_result_t quad_submit_object(
    renderer_t *renderer,
    const quad_viewport_t viewports[QUAD_VIEWPORT_COUNT],
    uint32_t object_index,
    int32_t world_x, int32_t world_y,
    uint32_t world_width, uint32_t world_height,
    uint16_t color);

#endif
