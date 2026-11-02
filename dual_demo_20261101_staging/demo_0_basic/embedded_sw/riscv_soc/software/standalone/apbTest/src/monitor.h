#ifndef SRC_MONITOR_H_
#define SRC_MONITOR_H_

#include <stdint.h>
#include "renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The monitor deliberately uses only the existing RECT render item.  It is a
 * compact status panel rather than an OSD/font engine, so it remains usable
 * with the current command FIFO and with the CPU statistics backend. */
typedef struct {
    uint32_t mode;          /* 0 = FULL, 1 = QUAD */
    uint32_t object_count;
    renderer_backend_t backend;
    uint32_t fps;
    uint32_t frame_cycles;
    uint32_t error;
} monitor_info_t;

/* Number of RECT items emitted by monitor_render().  Callers can reserve this
 * budget when selecting a QUAD object count for the fixed hardware FIFO. */
#define MONITOR_BAR_COUNT  6u
#define MONITOR_RECT_COUNT 7u

/* Submit a monitor panel into an already-open renderer frame.  The panel is
 * origin/size based and emits six horizontal status blocks:
 * mode, object count, renderer, FPS, frame cycles, and error.  Zero-valued
 * width bars use a one-pixel dim block; non-zero values are
 * represented by block width and colour.  No framebuffer pointer or GPU
 * register is accessed here. */
renderer_result_t monitor_render(renderer_t *renderer,
                                 uint32_t origin_x, uint32_t origin_y,
                                 uint32_t width, uint32_t height,
                                 const monitor_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* SRC_MONITOR_H_ */
