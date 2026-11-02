#ifndef SRC_RENDERER_H_
#define SRC_RENDERER_H_

#include <stdint.h>
#include "gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Backend selection is runtime state.  The scene and viewport layers do not
 * depend on which implementation is selected. */
typedef enum {
    RENDERER_BACKEND_FPGA = 0,
    RENDERER_BACKEND_CPU = 1
} renderer_backend_t;

typedef enum {
    RENDER_ITEM_RECT = 1,
    RENDER_ITEM_BLIT = 2
} render_item_kind_t;

/* Render items use top-left destination coordinates for both operations.
 * FPGA RECT commands are center based internally and are converted by the
 * FPGA backend.  BLIT source offsets are relative to the current Front. */
typedef struct {
    render_item_kind_t kind;
    uint32_t target_surface_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint16_t color;
    uint32_t src_offset;
    uint32_t src_stride;
} render_item_t;

/* Keep the numeric values aligned with gpu_result_t where possible so callers
 * can preserve the existing debug variables through the conversion helper. */
typedef enum {
    RENDERER_OK = 0,
    RENDERER_ERR_PARAM = 1,
    RENDERER_ERR_BUSY = 2,
    RENDERER_ERR_TIMEOUT = 3,
    RENDERER_ERR_FIFO_FULL = 4,
    RENDERER_ERR_FIFO_NOT_EMPTY = 5,
    RENDERER_ERR_EMPTY_FRAME = 6,
    RENDERER_ERR_PUSH_REJECT = 7,
    RENDERER_ERR_HW = 8,
    RENDERER_ERR_UNSUPPORTED = 9
} renderer_result_t;

typedef struct {
    uint32_t frames_started;
    uint32_t frames_submitted;
    uint32_t frames_completed;
    uint32_t items_submitted;
    uint32_t rect_items;
    uint32_t blit_items;
    uint32_t errors;
    uint32_t current_frame_items;
    uint32_t last_frame_items;
    renderer_result_t last_result;
    /* These fields are populated only by the FPGA backend.  CPU mode never
     * reads the GPU MMIO block and leaves them at zero. */
    uint32_t last_gpu_status;
    uint32_t last_gpu_fifo_status;
    uint32_t last_gpu_error_info;
    uint8_t last_gpu_perf_valid;
    gpu_perf_t last_gpu_perf;
} renderer_stats_t;

typedef struct {
    renderer_backend_t backend;
    uint8_t frame_open;
    uint8_t frame_submitted;
    uint16_t reserved;
    uint32_t frame_item_count;
    renderer_stats_t stats;
} renderer_t;

void renderer_init(renderer_t *renderer, renderer_backend_t backend);
renderer_result_t renderer_set_backend(renderer_t *renderer,
                                       renderer_backend_t backend);
renderer_backend_t renderer_get_backend(const renderer_t *renderer);

renderer_result_t renderer_begin_frame(renderer_t *renderer);
renderer_result_t renderer_submit_item(renderer_t *renderer,
                                        const render_item_t *item);
renderer_result_t renderer_submit_frame(renderer_t *renderer);
renderer_result_t renderer_wait_done(renderer_t *renderer,
                                     uint32_t poll_limit);
/* Convenience wrapper retaining the complete-frame style used by the demo. */
renderer_result_t renderer_end_frame(renderer_t *renderer,
                                     uint32_t poll_limit);

/* Number of items accepted into the currently open frame.  After completion,
 * the value is reset to zero; stats.last_frame_items retains the final count. */
uint32_t renderer_frame_item_count(const renderer_t *renderer);
const renderer_stats_t *renderer_get_stats(const renderer_t *renderer);
void renderer_reset_stats(renderer_t *renderer);

/* FPGA-only performance snapshot.  CPU mode returns RENDERER_ERR_UNSUPPORTED
 * without touching GPU registers. */
renderer_result_t renderer_get_last_gpu_perf(const renderer_t *renderer,
                                             gpu_perf_t *perf);
gpu_result_t renderer_result_to_gpu_result(renderer_result_t result);

/* Backend hooks implemented in renderer_fpga.c and renderer_cpu.c. */
renderer_result_t renderer_fpga_begin_frame(renderer_t *renderer);
renderer_result_t renderer_fpga_submit_item(renderer_t *renderer,
                                            const render_item_t *item);
renderer_result_t renderer_fpga_submit_frame(renderer_t *renderer);
renderer_result_t renderer_fpga_wait_done(renderer_t *renderer,
                                          uint32_t poll_limit);
renderer_result_t renderer_fpga_get_last_gpu_perf(const renderer_t *renderer,
                                                  gpu_perf_t *perf);
void renderer_fpga_capture_status(renderer_t *renderer);
renderer_result_t renderer_cpu_begin_frame(renderer_t *renderer);
renderer_result_t renderer_cpu_submit_item(renderer_t *renderer,
                                           const render_item_t *item);
renderer_result_t renderer_cpu_submit_frame(renderer_t *renderer);
renderer_result_t renderer_cpu_wait_done(renderer_t *renderer,
                                         uint32_t poll_limit);
renderer_result_t renderer_cpu_get_last_gpu_perf(const renderer_t *renderer,
                                                 gpu_perf_t *perf);
void renderer_cpu_set_target(uint32_t target_id);
void renderer_cpu_set_service(void (*service)(void));
/* True only while the synchronous CPU writer owns the wait callback.  This
 * software boundary is distinct from the hardware WAIT_OWNER status bit. */
int renderer_cpu_sync_waiting(void);
#define RENDERER_CPU_ROW_BORROW_PARAM   (1u << 0)
#define RENDERER_CPU_ROW_BORROW_LEASED  (1u << 1)
#define RENDERER_CPU_ROW_BORROW_ASYNC   (1u << 2)
#define RENDERER_CPU_ROW_BORROW_ITEMS   (1u << 3)
#define RENDERER_CPU_ROW_BORROW_HOST    (1u << 4)
uint32_t renderer_cpu_row_borrow_block(void);
int renderer_cpu_async_start(renderer_t *renderer,
                             const render_item_t *items, uint32_t count);
int renderer_cpu_async_service(renderer_t *renderer);
int renderer_cpu_async_busy(void);
int renderer_cpu_row_borrow(uint16_t **row);
void renderer_cpu_row_release(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_RENDERER_H_ */
