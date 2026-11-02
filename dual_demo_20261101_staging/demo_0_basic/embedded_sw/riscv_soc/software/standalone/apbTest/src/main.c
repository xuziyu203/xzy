#include "gpu.h"
#include "renderer.h"
#include "monitor.h"
#include "quad_mode.h"
#include "cpu_writer.h"
#include "bg_uart.h"
#include "bg_ddr_adapter.h"
#include "uart_reply.h"
#include "bsp.h"
#include "demo_command_text.h"
#include "step1_motion.h"
#include <stdint.h>
#include <string.h>

#define DEMO_RECT_SIZE 64u
#define DEMO_MAX_OBJECT_COUNT 64u
#define DEMO_BLIT_COUNT DEMO_MAX_OBJECT_COUNT
#define DEMO_FRAME_COMMAND_COUNT (1u + DEMO_BLIT_COUNT)
#define DEMO_UART_LINE_SIZE 96u
#define DEMO_UART_POLL_BUDGET 64u
#define DEMO_VIEWPORT_MAX_COUNT 4u
#define DEMO_QUAD_WIDTH (GPU_SCREEN_WIDTH / 2u)
#define DEMO_QUAD_HEIGHT (GPU_SCREEN_HEIGHT / 2u)
#define STEP1_OBJECT_SIZE 64u
#define STEP1_MIN_X 0
#define STEP1_MAX_X (960 - STEP1_OBJECT_SIZE)
#define STEP1_MIN_Y 0
#define STEP1_MAX_Y (540 - STEP1_OBJECT_SIZE)
#define STEP1_ASYNC_SWITCH_LIMIT 1024u
#define DEMO_SWITCH_DRAIN_LIMIT (STEP1_ASYNC_SWITCH_LIMIT * 64u)

/* FULL supports 64 objects: 65 commands normally, 85 while seeding the
 * 20-color atlas. The 128-entry command queue holds the complete frame.
 * QUAD remains limited to 20 objects per viewport. Pixel FIFOs are unchanged. */

/* BLIT source offsets require x*2 to be 16-byte aligned.  x is therefore
 * always kept on an eight-pixel boundary.  A +/-4 velocity is represented
 * as an eight-pixel step on alternating frames, preserving its average
 * four-pixel-per-frame speed while keeping every BLIT legal. */
#define DEMO_X_SLOW 4
#define DEMO_X_FAST 8

typedef enum {
    DEMO_VIEWPORT_MODE_FULL = 0,
    DEMO_VIEWPORT_MODE_QUAD = 1
} demo_viewport_mode_t;

/* FULL uses 1920x1080; each QUAD scene uses its own 960x540 local space. */
typedef struct {
    u32 origin_x;
    u32 origin_y;
    u32 width;
    u32 height;
    u32 scale_num;
    u32 scale_den;
    u32 world_x;
    u32 world_y;
    u32 world_width;
    u32 world_height;
} demo_viewport_t;

typedef struct {
    demo_viewport_mode_t mode;
    u32 count;
    u32 active_index;
    demo_viewport_t viewports[DEMO_VIEWPORT_MAX_COUNT];
} demo_viewport_layout_t;

typedef struct {
    u32 dst_x;
    u32 dst_y;
    u32 dst_width;
    u32 dst_height;
} demo_viewport_rect_t;

/* x/y are world-space positions.  In FPGA mode they also track the current
 * Front; the CPU Q0 mode renders pixels while the FPGA scene advances independently.  The ten-column,
 * three-row layout is initially non-overlapping and spans the display so
 * boundary reflections and pair collisions are both observable. */
static blit_object_t demo_objects[DEMO_MAX_OBJECT_COUNT];
static int32_t demo_x_phase[DEMO_MAX_OBJECT_COUNT];
static u32 demo_active_count = DEMO_MAX_OBJECT_COUNT;
static u32 demo_speed_level = 1u;
static u32 demo_full_source = GPU_FULL_SOURCE_BLIT;

static u32 demo_full_target_id(void)
{
    return demo_full_source == GPU_FULL_SOURCE_RECT ? GPU_TARGET_FULL_RECT :
           demo_full_source == GPU_FULL_SOURCE_CPU ? GPU_TARGET_FULL_CPU :
           GPU_TARGET_FULL_BLIT;
}
static volatile u32 demo_level = 0u;
static u32 demo_fps = 0u;
static u64 demo_last_frame_time;
static u32 demo_reseed_pending;
static u32 full_blit_seed_pending = 1u;
static char demo_uart_line[DEMO_UART_LINE_SIZE];
static u32 demo_uart_line_length;
static u32 demo_uart_line_overflow;
static demo_viewport_layout_t demo_viewport_layout;
static renderer_t demo_renderer;
static renderer_t cpu_q0_renderer;
/* Step 1 keeps bounded scenes and independent CPU/GPU scheduling.
 * NUM and COLLIDE requests are sampled only when planning a new frame;
 * each path publishes its configuration only after its own present. */
static u32 step1_active;
static u32 step1_failed;
static u32 step1_seed_frames;
static u32 step1_motion_frames;
static blit_object_t step1_shown[STEP1_MAX_OBJECTS];
static blit_object_t step1_planned[STEP1_MAX_OBJECTS];
static u32 step1_scene_count = 1u;
static u32 step1_gpu_count;
static u32 step1_pending_count = 1u;
static u32 step1_frame_commands;

/* Configuration is shared; acknowledgement and in-flight snapshots are not.
 * An ON/NUM change reseeds only a newly planned collision-enabled scene.
 * This also resolves overlaps left behind by COLLIDE OFF without modifying
 * the old Front scene used by BLIT erases and source addressing. */
typedef struct {
    u32 shown_epoch;
    u32 frame_epoch;
    u32 shown_enabled;
    u32 frame_enabled;
} step1_collision_state_t;

static u32 step1_collision_requested = 1u;
static u32 step1_layout_epoch;
static step1_collision_state_t step1_serial_collision;
static step1_collision_state_t step1_cpu_collision;
static step1_collision_state_t step1_gpu_collision;
static u32 step1_async_mode;
static u32 step1_async_pending;
static u32 step1_async_switch_wait;
static int step1_async_switch_error;
static u32 step1_async_gpu_state;
static u32 step1_async_gpu_seq_base;
static u32 step1_async_cpu_seq_base;
static u32 step1_async_gpu_inflight_count;
static u32 step1_async_cpu_inflight_count;
static u32 step1_async_cpu_count;
static u32 step1_async_cpu_state;
static blit_object_t step1_async_cpu_shown[STEP1_MAX_OBJECTS];
static blit_object_t step1_async_cpu_planned[STEP1_MAX_OBJECTS];
static blit_object_t step1_async_gpu_shown[STEP1_MAX_OBJECTS];
static blit_object_t step1_async_gpu_planned[STEP1_MAX_OBJECTS];
static u32 step1_async_cpu_frames;
static u32 step1_async_gpu_frames;

typedef enum {
    DEMO_SWITCH_IDLE = 0,
    DEMO_SWITCH_DRAIN,
    DEMO_SWITCH_DISABLE_ASYNC,
    DEMO_SWITCH_PREPARE,
    DEMO_SWITCH_RUN,
    DEMO_SWITCH_ERROR
} demo_switch_state_t;

static demo_switch_state_t demo_switch_state;
static demo_viewport_mode_t demo_confirmed_mode;
static u32 demo_confirmed_source;
static demo_viewport_mode_t demo_pending_mode;
static u32 demo_pending_source;
static u32 demo_pending_valid;
static demo_viewport_mode_t demo_switch_mode;
static u32 demo_switch_source;
static u32 demo_switch_wait;
static u32 demo_switch_rendered;
static int demo_switch_error;
static u32 step1_async_freeze_new;
static u32 step1_async_off_pending;
static u32 demo_full_seq_base;
static u64 demo_full_window_start;
static u32 demo_full_window_armed;
static u32 demo_full_snapshot_valid;
static u32 demo_async_resume_requested;
static u32 demo_options_requested = 3u;
static u32 demo_options_pending;

static u64 demo_panel_window_start;
static u32 demo_panel_q0_base;
static u32 demo_panel_gpu_base;
static u32 demo_panel_perf_generation;
static u32 demo_panel_perf_base;
static u32 demo_panel_window_armed;
static quad_viewport_t demo_quad_viewports[QUAD_VIEWPORT_COUNT];
static void demo_init_objects(void);
static void demo_uart_poll(void);
static int step1_async_service(void);
static void demo_panel_reset_window(void);
static void demo_panel_service(void);
static void demo_request_display(demo_viewport_mode_t mode, u32 source);
static int demo_switch_service(void);
static int demo_full_cpu_cycle(void);
static void demo_panel_invalidate_full(void);
static int demo_bg_prepare_full_cpu(void);
static int demo_prepare_full_cpu(void);
static u32 demo_panel_wait_active;
static u64 demo_panel_wait_start;
static int demo_switch_fail(u32 stage, int error, int raw);

static int demo_bg_restore(void);
static int demo_bg_restore_status(void);
static int demo_bg_restore_pending(void);
static u32 demo_bg_saved_mode;
static u32 demo_bg_saved_source;
static u32 demo_bg_saved_valid;
/* EXIT recovery owns this target until an actual recovery frame commits. */
static int demo_bg_recovery_state;
static u32 demo_bg_recovery_target_mode;
static u32 demo_bg_recovery_target_source;
static u32 demo_bg_recovery_requesting;
static u32 demo_bg_recovery_force_frame;
static u32 demo_bg_recovery_switch_active;
static u32 demo_bg_recovery_frame_done;

volatile gpu_result_t gpu_test_begin_result;
volatile gpu_result_t gpu_test_background_result;
volatile gpu_result_t gpu_test_push0_result;
volatile gpu_result_t gpu_test_blit_result;
volatile gpu_result_t gpu_test_submit_result;
volatile gpu_result_t gpu_test_wait_result;
volatile gpu_result_t gpu_test_perf_result;
volatile gpu_result_t gpu_test_error_result;
volatile gpu_perf_t gpu_test_perf;
volatile u32 gpu_test_status;
volatile u32 gpu_test_fifo_status;
volatile u32 gpu_test_error_info;
volatile u32 gpu_test_frame_count;
volatile u32 gpu_test_bootstrap_done;
volatile u32 gpu_test_frame_command_count;
volatile u32 gpu_test_object_index;
volatile u32 gpu_test_old_x[DEMO_MAX_OBJECT_COUNT];
volatile u32 gpu_test_old_y[DEMO_MAX_OBJECT_COUNT];
volatile u32 gpu_test_new_x[DEMO_MAX_OBJECT_COUNT];
volatile u32 gpu_test_new_y[DEMO_MAX_OBJECT_COUNT];
volatile u32 gpu_test_source_offset[DEMO_MAX_OBJECT_COUNT];
/* Renderer statistics are exported for the same debugger/ILA workflow as
 * the existing GPU test variables.  CPU mode updates these counters without
 * touching the GPU registers. */
volatile u32 renderer_test_backend;
volatile u32 renderer_test_frames_started;
volatile u32 renderer_test_frames_submitted;
volatile u32 renderer_test_frames_completed;
volatile u32 renderer_test_items_submitted;
volatile u32 renderer_test_rect_items;
volatile u32 renderer_test_blit_items;
volatile u32 renderer_test_errors;
volatile u32 renderer_test_last_frame_items;
volatile u32 renderer_test_gpu_perf_valid;

#define DEMO_TELEM_TX_CAP 192u

typedef struct {
    u64 window_start;
    u64 window_frames;
    u64 frame_cycles;
    u64 cpu_cycles;
    u64 gpu_wait_cycles;
    u64 owner_wait_cycles;
    u64 row_wait_cycles;
    u64 present_wait_cycles;
    u64 gpu_hw_cycles;
    char tx[DEMO_TELEM_TX_CAP];
    u32 tx_len;
    u32 tx_pos;
} demo_telemetry_t;

static demo_telemetry_t demo_telemetry;

static void demo_telem_append_char(char c)
{
    if (demo_telemetry.tx_len < (DEMO_TELEM_TX_CAP - 1u))
        demo_telemetry.tx[demo_telemetry.tx_len++] = c;
}

static void demo_telem_append_text(const char *text)
{
    while (*text != '\0')
        demo_telem_append_char(*text++);
}

static void demo_telem_append_u64(u64 value)
{
    char digits[24];
    u32 count = 0u;
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u)
        demo_telem_append_char(digits[--count]);
}

static void demo_telemetry_init(void)
{
    demo_telemetry.window_start = clint_getTime(BSP_CLINT);
    demo_telemetry.window_frames = 0u;
    demo_telemetry.frame_cycles = 0u;
    demo_telemetry.cpu_cycles = 0u;
    demo_telemetry.gpu_wait_cycles = 0u;
    demo_telemetry.owner_wait_cycles = 0u;
    demo_telemetry.row_wait_cycles = 0u;
    demo_telemetry.present_wait_cycles = 0u;
    demo_telemetry.gpu_hw_cycles = 0u;
    demo_telemetry.tx_len = 0u;
    demo_telemetry.tx_pos = 0u;
}

static void demo_telemetry_stop_serial(void)
{
    demo_telemetry.window_start = clint_getTime(BSP_CLINT);
    demo_telemetry.window_frames = 0u;
    demo_telemetry.frame_cycles = 0u;
    demo_telemetry.cpu_cycles = 0u;
    demo_telemetry.gpu_wait_cycles = 0u;
    demo_telemetry.owner_wait_cycles = 0u;
    demo_telemetry.row_wait_cycles = 0u;
    demo_telemetry.present_wait_cycles = 0u;
    demo_telemetry.gpu_hw_cycles = 0u;
    demo_telemetry.tx_len = 0u;
    demo_telemetry.tx_pos = 0u;
}
static void demo_telemetry_account_frame(u64 frame_cycles, u64 cpu_cycles,
                                          u64 gpu_wait_cycles,
                                          u64 gpu_hw_cycles)
{
    demo_telemetry.window_frames++;
    demo_telemetry.frame_cycles += frame_cycles;
    demo_telemetry.cpu_cycles += cpu_cycles;
    demo_telemetry.gpu_wait_cycles += gpu_wait_cycles;
    demo_telemetry.owner_wait_cycles += cpu_writer_owner_wait_cycles();
    demo_telemetry.row_wait_cycles += cpu_writer_row_wait_cycles();
    demo_telemetry.present_wait_cycles += cpu_writer_present_wait_cycles();
    demo_telemetry.gpu_hw_cycles += gpu_hw_cycles;
}

static void demo_telemetry_prepare_snapshot(u64 now)
{
    u64 elapsed;
    if (demo_telemetry.tx_pos < demo_telemetry.tx_len)
        return;
    elapsed = (now >= demo_telemetry.window_start) ?
              (now - demo_telemetry.window_start) : 0u;
    if ((elapsed < (u64)BSP_CLINT_HZ) ||
        (demo_telemetry.window_frames == 0u))
        return;

    demo_telemetry.tx_len = 0u;
    demo_telemetry.tx_pos = 0u;
    demo_telem_append_text("SERIAL_BASELINE WIN_CYCLES=");
    demo_telem_append_u64(elapsed);
    demo_telem_append_text(" FRAMES=");
    demo_telem_append_u64(demo_telemetry.window_frames);
    demo_telem_append_text(" FRAME=");
    demo_telem_append_u64(demo_telemetry.frame_cycles /
                          demo_telemetry.window_frames);
    demo_telem_append_text(" CPU=");
    demo_telem_append_u64(demo_telemetry.cpu_cycles /
                          demo_telemetry.window_frames);
    demo_telem_append_text(" GPUWAIT=");
    demo_telem_append_u64(demo_telemetry.gpu_wait_cycles /
                          demo_telemetry.window_frames);
    demo_telem_append_text(" OWNER=");
    demo_telem_append_u64(demo_telemetry.owner_wait_cycles /
                          demo_telemetry.window_frames);
    demo_telem_append_text(" ROW=");
    demo_telem_append_u64(demo_telemetry.row_wait_cycles /
                          demo_telemetry.window_frames);
    demo_telem_append_text(" PRESENT=");
    demo_telem_append_u64(demo_telemetry.present_wait_cycles /
                          demo_telemetry.window_frames);
    demo_telem_append_text(" GPUHW=");
    demo_telem_append_u64(demo_telemetry.gpu_hw_cycles /
                          demo_telemetry.window_frames);
    demo_telem_append_text("\r\n");
    demo_telemetry.window_start = now;
    demo_telemetry.window_frames = 0u;
    demo_telemetry.frame_cycles = 0u;
    demo_telemetry.cpu_cycles = 0u;
    demo_telemetry.gpu_wait_cycles = 0u;
    demo_telemetry.owner_wait_cycles = 0u;
    demo_telemetry.row_wait_cycles = 0u;
    demo_telemetry.present_wait_cycles = 0u;
    demo_telemetry.gpu_hw_cycles = 0u;
}

static void demo_telemetry_service(void)
{
    /* Control/binary sessions own UART output until their lifecycle completes. */
    if (uart_reply_pending() || bg_uart_preparing() ||
        bg_uart_binary_active() || bg_uart_recovery_active())
        return;
#ifdef HOST_TEST
    /* The host contract has no UART MMIO driver; telemetry accounting still
     * compiles and is covered by the same scene model. */
    return;
#else
    u64 now = clint_getTime(BSP_CLINT);
    demo_telemetry_prepare_snapshot(now);
    if ((demo_telemetry.tx_pos < demo_telemetry.tx_len) &&
        (uart_writeAvailability(BSP_UART_TERMINAL) != 0u)) {
        write_u32((u32)(unsigned char)demo_telemetry.tx[demo_telemetry.tx_pos++],
                  BSP_UART_TERMINAL + UART_DATA);
        if (demo_telemetry.tx_pos == demo_telemetry.tx_len) {
            demo_telemetry.tx_len = 0u;
            demo_telemetry.tx_pos = 0u;
        }
    }
#endif
}

static u32 demo_panel_options_flags(void)
{
    u32 status = gpu_get_demo_options();
    return ((status & GPU_DEMO_BURST_ON) ? 1u : 0u) |
           ((status & GPU_DEMO_FIFO_ON) ? 2u : 0u) |
           ((status & GPU_DEMO_DBUF_ON) ? 4u : 0u) |
           (demo_options_pending ? 8u : 0u);
}

static u32 demo_panel_pack_row(u32 objects, u32 fps, u32 valid)
{
    u32 row = 0u;
    if (objects > 99u) objects = 99u;
    if (fps > 99999u) fps = 99999u;
    row |= (fps % 10u) << 28;
    row |= ((fps / 10u) % 10u) << 24;
    fps /= 100u;
    row |= (objects % 10u);
    row |= ((objects / 10u) % 10u) << 4;
    row |= (fps % 10u) << 8;
    row |= ((fps / 10u) % 10u) << 12;
    row |= ((fps / 100u) % 10u) << 16;
    if (valid != 0u) row |= GPU_PANEL_ROW_FPS_VALID;
    return row;
}

static u32 demo_panel_fps(u32 delta, u64 elapsed)
{
    u64 measured;
    if ((delta == 0u) || (elapsed == 0u)) return 0u;
    measured = ((u64)delta * (u64)BSP_CLINT_HZ * 100u + elapsed / 2u) / elapsed;
    return (measured > 99999u) ? 99999u : (u32)measured;
}

#define DEMO_PANEL_CYCLES_PER_US 100u
static u32 demo_panel_time_bcd(u32 cycles)
{
    u32 microseconds;
    u32 packed = 0u;
    u32 digit;
    if (cycles == 0xFFFFFFFFu) return 0xFFFFFFFFu;
    microseconds = cycles / DEMO_PANEL_CYCLES_PER_US;
    if ((cycles % DEMO_PANEL_CYCLES_PER_US) >=
        (DEMO_PANEL_CYCLES_PER_US / 2u)) ++microseconds;
    for (digit = 0u; digit < 8u; ++digit) {
        packed |= (microseconds % 10u) << (digit * 4u);
        microseconds /= 10u;
    }
    return packed;
}

static u32 demo_panel_cpu_objects(void)
{
    return step1_async_mode ? step1_async_cpu_count :
           (demo_confirmed_mode == DEMO_VIEWPORT_MODE_FULL ? demo_active_count : step1_scene_count);
}

static u32 demo_panel_gpu_objects(void)
{
    return step1_async_mode ? step1_gpu_count :
           (demo_confirmed_mode == DEMO_VIEWPORT_MODE_FULL ? demo_active_count : step1_gpu_count);
}

static u32 demo_panel_error_code(void)
{
    u32 error = gpu_get_error_info();
    u32 events = gpu_get_async_event_status();
    int cpu_error = cpu_writer_last_error();
    if (demo_switch_error != 0) return 0x80000000u | ((u32)demo_switch_error & 0xFFFFu);
    if (step1_async_switch_error != 0) return 0x80000000u | ((u32)step1_async_switch_error & 0xFFFFu);
    if (error != 0u) return error;
    if (events != 0u) return 0x40000000u | (events & 0x00FFFFFFu);
    if (cpu_error != 0) return 0x20000000u | ((u32)cpu_error & 0x00FFFFFFu);
    return step1_failed ? 0xFFFFFFFFu : 0u;
}

static void demo_panel_reset_window(void)
{
    demo_panel_window_start = clint_getTime(BSP_CLINT);
    demo_panel_q0_base = gpu_get_async_q0_seq();
    demo_panel_gpu_base = gpu_get_async_gpu_seq();
    demo_panel_perf_base = demo_panel_perf_generation;
    demo_full_window_start = demo_panel_window_start;
    demo_full_seq_base = gpu_get_display_generation();
    demo_panel_window_armed = 1u;
    demo_full_window_armed = 1u;
}

static void demo_panel_invalidate_full(void)
{
    gpu_panel_snapshot_t snapshot;
    snapshot.cpu_row = demo_panel_pack_row(demo_active_count, 0u, 0u);
    snapshot.blit_row = demo_panel_pack_row(0u, 0u, 0u);
    snapshot.rect_row = demo_panel_pack_row(0u, 0u, 0u);
    snapshot.blit_time = 0u;
    snapshot.rect_time = 0u;
    snapshot.flags = demo_panel_options_flags() | GPU_PANEL_FLAG_FULL_HUD_VALID |
                     ((demo_switch_source & 3u) << GPU_PANEL_FLAG_FULL_SOURCE_SHIFT);
    snapshot.error = 0u;
    (void)gpu_panel_publish(&snapshot);
    demo_full_snapshot_valid = 0u;
}

static void demo_panel_publish_full_label(void)
{
    gpu_panel_snapshot_t snapshot;
    u32 source = demo_confirmed_source & 3u;
    snapshot.cpu_row = demo_panel_pack_row(demo_active_count, 0u, 0u);
    snapshot.blit_row = demo_panel_pack_row(demo_active_count, 0u, 0u);
    snapshot.rect_row = demo_panel_pack_row(demo_active_count, 0u, 0u);
    snapshot.blit_time = 0u;
    snapshot.rect_time = 0u;
    snapshot.flags = demo_panel_options_flags() | GPU_PANEL_FLAG_FULL_HUD_VALID |
                     (source << GPU_PANEL_FLAG_FULL_SOURCE_SHIFT);
    snapshot.error = demo_panel_error_code();
    if (snapshot.error != 0u)
        snapshot.flags |= GPU_PANEL_FLAG_ERROR_VALID;
    if (gpu_panel_publish(&snapshot) == GPU_OK)
        demo_full_snapshot_valid = 1u;
}

static void demo_panel_service(void)
{
    gpu_panel_snapshot_t snapshot;
    u64 now;
    u64 elapsed;
    u32 q0_seq;
    u32 gpu_seq;
    u32 q0_delta;
    u32 gpu_delta;
    u32 full_seq;
    u32 full_delta;
    u32 cpu_valid;
    u32 gpu_valid;
    u32 perf_valid;
    u32 flags;
    u32 error;
    u32 source;
    u32 active_mode;

    if ((demo_switch_state != DEMO_SWITCH_IDLE) &&
        (demo_switch_state != DEMO_SWITCH_ERROR))
        return;
    active_mode = demo_confirmed_mode;
    if (active_mode == DEMO_VIEWPORT_MODE_FULL) {
        if (demo_full_snapshot_valid == 0u)
            demo_panel_publish_full_label();
        if (demo_full_window_armed == 0u) {
            demo_panel_reset_window();
            return;
        }
        now = clint_getTime(BSP_CLINT);
        if (now < demo_full_window_start)
            demo_panel_reset_window();
        elapsed = now - demo_full_window_start;
        if (elapsed < (u64)BSP_CLINT_HZ / 4u)
            return;
        full_seq = gpu_get_display_generation();
        full_delta = full_seq - demo_full_seq_base;
        source = demo_confirmed_source & 3u;
        snapshot.cpu_row = demo_panel_pack_row(demo_active_count,
            source == GPU_FULL_SOURCE_CPU ? demo_panel_fps(full_delta, elapsed) : 0u,
            source == GPU_FULL_SOURCE_CPU ? 1u : 0u);
        snapshot.blit_row = demo_panel_pack_row(demo_active_count,
            source == GPU_FULL_SOURCE_BLIT ? demo_panel_fps(full_delta, elapsed) : 0u,
            source == GPU_FULL_SOURCE_BLIT ? 1u : 0u);
        snapshot.rect_row = demo_panel_pack_row(demo_active_count,
            source == GPU_FULL_SOURCE_RECT ? demo_panel_fps(full_delta, elapsed) : 0u,
            source == GPU_FULL_SOURCE_RECT ? 1u : 0u);
        snapshot.blit_time = demo_panel_time_bcd(gpu_test_perf.blockcopy_total_cycles);
        snapshot.rect_time = demo_panel_time_bcd(gpu_test_perf.rect_total_cycles);
        flags = demo_panel_options_flags() | GPU_PANEL_FLAG_FULL_HUD_VALID |
                (source << GPU_PANEL_FLAG_FULL_SOURCE_SHIFT);
        perf_valid = (demo_panel_perf_generation != demo_panel_perf_base) ? 1u : 0u;
        if (perf_valid != 0u)
            flags |= GPU_PANEL_FLAG_BLIT_TIME_VALID | GPU_PANEL_FLAG_RECT_TIME_VALID;
        error = demo_panel_error_code();
        if (error != 0u)
            flags |= GPU_PANEL_FLAG_ERROR_VALID;
        snapshot.flags = flags;
        snapshot.error = error;
        (void)gpu_panel_publish(&snapshot);
        demo_panel_reset_window();
        return;
    }

    if (demo_panel_window_armed == 0u) {
        demo_panel_reset_window();
        return;
    }
    now = clint_getTime(BSP_CLINT);
    if (now < demo_panel_window_start) {
        demo_panel_reset_window();
        return;
    }
    elapsed = now - demo_panel_window_start;
    if (elapsed < (u64)BSP_CLINT_HZ / 4u)
        return;

    q0_seq = gpu_get_async_q0_seq();
    gpu_seq = gpu_get_async_gpu_seq();
    q0_delta = q0_seq - demo_panel_q0_base;
    gpu_delta = gpu_seq - demo_panel_gpu_base;
    cpu_valid = 1u;
    gpu_valid = 1u;
    perf_valid = (demo_panel_perf_generation != demo_panel_perf_base) ? 1u : 0u;
    snapshot.cpu_row = demo_panel_pack_row(demo_panel_cpu_objects(),
                                           demo_panel_fps(q0_delta, elapsed), cpu_valid);
    snapshot.blit_row = demo_panel_pack_row(demo_panel_gpu_objects(),
                                            demo_panel_fps(gpu_delta, elapsed), gpu_valid);
    snapshot.rect_row = demo_panel_pack_row(demo_panel_gpu_objects(),
                                            demo_panel_fps(gpu_delta, elapsed), gpu_valid);
    snapshot.blit_time = demo_panel_time_bcd(gpu_test_perf.blockcopy_total_cycles);
    snapshot.rect_time = demo_panel_time_bcd(gpu_test_perf.rect_total_cycles);
    flags = demo_panel_options_flags();
    if (perf_valid != 0u)
        flags |= GPU_PANEL_FLAG_BLIT_TIME_VALID | GPU_PANEL_FLAG_RECT_TIME_VALID;
    error = demo_panel_error_code();
    if (error != 0u) flags |= GPU_PANEL_FLAG_ERROR_VALID;
    snapshot.flags = flags;
    snapshot.error = error;
    (void)gpu_panel_publish(&snapshot);
    demo_panel_reset_window();
}/* Keep the existing debugger-visible GPU variables while routing all render
 * work through the backend-neutral renderer interface. */
static gpu_result_t demo_gpu_result(renderer_result_t result)
{
    return renderer_result_to_gpu_result(result);
}

static void demo_viewport_set(demo_viewport_t *viewport,
                              u32 origin_x, u32 origin_y,
                              u32 width, u32 height,
                              u32 scale_num, u32 scale_den,
                              u32 world_x, u32 world_y,
                              u32 world_width, u32 world_height)
{
    viewport->origin_x = origin_x;
    viewport->origin_y = origin_y;
    viewport->width = width;
    viewport->height = height;
    viewport->scale_num = scale_num;
    viewport->scale_den = scale_den;
    viewport->world_x = world_x;
    viewport->world_y = world_y;
    viewport->world_width = world_width;
    viewport->world_height = world_height;
}

static void demo_viewport_configure(demo_viewport_mode_t mode)
{
    demo_viewport_mode_t previous = demo_viewport_layout.mode;
    demo_viewport_layout.mode = mode;
    demo_viewport_layout.active_index = 0u;

    if (mode == DEMO_VIEWPORT_MODE_QUAD) {
        quad_layout_init(demo_quad_viewports,
                         GPU_SCREEN_WIDTH, GPU_SCREEN_HEIGHT);
        demo_viewport_layout.count = DEMO_VIEWPORT_MAX_COUNT;
        demo_viewport_set(&demo_viewport_layout.viewports[0],
                          0u, 0u, DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT,
                          1u, 1u, 0u, 0u, DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT);
        demo_viewport_set(&demo_viewport_layout.viewports[1],
                          DEMO_QUAD_WIDTH, 0u,
                          DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT,
                          1u, 1u, 0u, 0u,
                          DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT);
        demo_viewport_set(&demo_viewport_layout.viewports[2],
                          0u, DEMO_QUAD_HEIGHT,
                          DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT,
                          1u, 1u, 0u, 0u,
                          DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT);
        demo_viewport_set(&demo_viewport_layout.viewports[3],
                          DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT,
                          DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT,
                          1u, 1u, 0u, 0u,
                          DEMO_QUAD_WIDTH, DEMO_QUAD_HEIGHT);
    } else {
        demo_viewport_layout.count = 1u;
        demo_viewport_set(&demo_viewport_layout.viewports[0],
                          0u, 0u, GPU_SCREEN_WIDTH, GPU_SCREEN_HEIGHT,
                          1u, 1u, 0u, 0u,
                          GPU_SCREEN_WIDTH, GPU_SCREEN_HEIGHT);
    }
    if (previous != mode) {
        // A mode change changes the physical coordinate space. Start a
        // deterministic legal scene; the next frame seeds its own target.
        demo_init_objects();
        full_blit_seed_pending = 1u;
    }
}

static const demo_viewport_t *demo_viewport_active(void)
{
    return &demo_viewport_layout.viewports[demo_viewport_layout.active_index];
}

/* Transform a world-space rectangle into the selected viewport.  The current
 * BLIT command has no scale or clipping support, so the caller must reject a
 * result whose scaled size is not the native 64x64 source size. */
static int demo_viewport_transform(const demo_viewport_t *viewport,
                                   int32_t world_x, int32_t world_y,
                                   u32 width, u32 height,
                                   demo_viewport_rect_t *rect)
{
    int32_t local_x = world_x - (int32_t)viewport->world_x;
    int32_t local_y = world_y - (int32_t)viewport->world_y;
    int32_t scaled_x;
    int32_t scaled_y;
    u32 scaled_width;
    u32 scaled_height;

    if ((local_x + (int32_t)width <= 0) ||
        (local_y + (int32_t)height <= 0) ||
        (local_x >= (int32_t)viewport->world_width) ||
        (local_y >= (int32_t)viewport->world_height))
        return 0;

    scaled_x = (local_x * (int32_t)viewport->scale_num) /
               (int32_t)viewport->scale_den;
    scaled_y = (local_y * (int32_t)viewport->scale_num) /
               (int32_t)viewport->scale_den;
    scaled_width = (width * viewport->scale_num) / viewport->scale_den;
    scaled_height = (height * viewport->scale_num) / viewport->scale_den;

    if ((scaled_x < 0) || (scaled_y < 0) ||
        ((u32)scaled_x + scaled_width > viewport->width) ||
        ((u32)scaled_y + scaled_height > viewport->height))
        return 0;

    rect->dst_x = viewport->origin_x + (u32)scaled_x;
    rect->dst_y = viewport->origin_y + (u32)scaled_y;
    rect->dst_width = scaled_width;
    rect->dst_height = scaled_height;
    return 1;
}

/* FULL is an identity transform; QUAD submits local RECTs in quad_mode. */
static int demo_viewport_transform_blit(const blit_object_t *object,
                                        demo_viewport_rect_t *rect)
{
    /* The current BLIT source is fixed at 64x64.  Width/height remain part
     * of the scene object contract so future protocols can expose 32/128
     * sprites without changing simulation code. */
    if ((object == 0) || (object->enable == 0u) ||
        (object->width != DEMO_RECT_SIZE) ||
        (object->height != DEMO_RECT_SIZE))
        return 0;
    if (!demo_viewport_transform(demo_viewport_active(),
                                 object->x, object->y,
                                 object->width, object->height, rect))
        return 0;

    if ((rect->dst_width != DEMO_RECT_SIZE) ||
        (rect->dst_height != DEMO_RECT_SIZE) ||
        ((rect->dst_x & 7u) != 0u))
        return 0;
    return 1;
}

static const uint16_t demo_colors[20] = {
    0xF800u, 0x07E0u, 0x041Fu, 0xFFE0u, 0x07FFu,
    0xF81Fu, 0xFD20u, 0xA81Fu, 0x87E0u, 0xFFFFu,
    0xFB8Eu, 0x5FE0u, 0x867Fu, 0xFFD0u, 0x3DFBu,
    0xFC1Fu, 0xBDE0u, 0x6D5Fu, 0xFA60u, 0xC618u
};
static u32 demo_palette_offset(u32 index)
{
    index %= 20u;
    return GPU_PALETTE_SOURCE | ((index / 10u) * 64u * 1920u +
                                 (index % 10u) * 128u);
}
static renderer_result_t demo_seed_palette(u32 target)
{
    render_item_t item = {0};
    u32 i;
    item.kind = RENDER_ITEM_RECT;
    item.target_surface_id = target;
    item.width = item.height = 64u;
    item.src_offset = GPU_PALETTE_SOURCE;
    for (i = 0u; i < 20u; ++i) {
        renderer_result_t result;
        item.x = (i % 10u) * 64u; item.y = (i / 10u) * 64u;
        item.color = demo_colors[i];
        result = renderer_submit_item(&demo_renderer, &item);
        if (result != RENDERER_OK) return result;
    }
    return RENDERER_OK;
}
static void demo_init_objects(void)
{
    static const int32_t x_positions[] = {
        32, 224, 416, 608, 800, 992, 1184, 1376, 1568, 1760
    };
    static const int32_t y_positions[] = { 32, 184, 336, 488, 640, 792, 944 };
    static const int32_t x_velocities[] = {
        DEMO_X_FAST, -DEMO_X_FAST, DEMO_X_SLOW, -DEMO_X_SLOW,
        DEMO_X_FAST, -DEMO_X_FAST, DEMO_X_SLOW, -DEMO_X_SLOW,
        DEMO_X_FAST, -DEMO_X_FAST
    };
    static const int32_t y_velocities[] = {
        3, -5, 5, -3, 3, -5, 5, -3, 3, -5
    };
    u32 i;

    for (i = 0u; i < DEMO_MAX_OBJECT_COUNT; i++) {
        u32 column = i % 10u;
        u32 row = i / 10u;
        blit_object_t *object = &demo_objects[i];

        object->x = x_positions[column];
        object->y = y_positions[row];
        if (demo_viewport_layout.mode == DEMO_VIEWPORT_MODE_QUAD) {
            u32 slot = i / QUAD_CONTENT_VIEWPORTS;
            /* 53 objects are distributed round-robin over Q0-Q2.  Each
             * local viewport uses a legal 6x3, 64-pixel grid; Q3 remains
             * reserved for the seven monitor rectangles. */
            object->x = 32 + (int32_t)(slot % 6u) * 152;
            object->y = 64 + (int32_t)(slot / 6u) * 152;
        }
        object->vx = x_velocities[column];
        object->vy = y_velocities[column];
        object->width = DEMO_RECT_SIZE;
        object->height = DEMO_RECT_SIZE;
        object->color = demo_colors[i % 20u];
        object->enable = 1u;
        /* The phase accumulator is reset with the deterministic scene seed;
         * SPD scales it later without ever emitting a misaligned x. */
        demo_x_phase[i] = 0;
    }
    full_blit_seed_pending = 1u;
}


static void demo_stop(const char *stage, gpu_result_t result)
{
    const renderer_stats_t *stats = renderer_get_stats(&demo_renderer);

    gpu_test_error_result = result;
    if ((stats != 0) &&
        (renderer_get_backend(&demo_renderer) == RENDERER_BACKEND_FPGA)) {
        gpu_test_status = stats->last_gpu_status;
        gpu_test_fifo_status = stats->last_gpu_fifo_status;
        gpu_test_error_info = stats->last_gpu_error_info;
    } else {
        gpu_test_status = 0u;
        gpu_test_fifo_status = 0u;
        gpu_test_error_info = 0u;
    }
    uart_writeStr(BSP_UART_TERMINAL, " raw_status=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)gpu_get_status());
    uart_writeStr(BSP_UART_TERMINAL, " display_status=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)gpu_get_display_status());
    uart_writeStr(BSP_UART_TERMINAL, " generation=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)gpu_get_display_generation());
    uart_writeStr(BSP_UART_TERMINAL, "\r\nRenderer stopped: ");
    uart_writeStr(BSP_UART_TERMINAL, stage);
    uart_writeStr(BSP_UART_TERMINAL, " result=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)result);
    uart_writeStr(BSP_UART_TERMINAL, " completed_frames=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)gpu_test_frame_count);
    uart_writeStr(BSP_UART_TERMINAL, " status=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)gpu_test_status);
    uart_writeStr(BSP_UART_TERMINAL, " fifo=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)gpu_test_fifo_status);
    uart_writeStr(BSP_UART_TERMINAL, " error_info=0x");
    uart_writeHex(BSP_UART_TERMINAL, (int)gpu_test_error_info);
    uart_writeStr(BSP_UART_TERMINAL, "\r\nRenderer remains serviceable; issue STATUS or reset.\r\n");
}

static void demo_publish_renderer_stats(void)
{
    const renderer_stats_t *stats = renderer_get_stats(&demo_renderer);

    if (stats == 0)
        return;
    renderer_test_backend = (u32)renderer_get_backend(&demo_renderer);
    renderer_test_frames_started = stats->frames_started;
    renderer_test_frames_submitted = stats->frames_submitted;
    renderer_test_frames_completed = stats->frames_completed;
    renderer_test_items_submitted = stats->items_submitted;
    renderer_test_rect_items = stats->rect_items;
    renderer_test_blit_items = stats->blit_items;
    renderer_test_errors = stats->errors;
    renderer_test_last_frame_items = stats->last_frame_items;
    renderer_test_gpu_perf_valid = stats->last_gpu_perf_valid;
}

static int demo_select_renderer(renderer_backend_t backend)
{
    renderer_backend_t previous = renderer_get_backend(&demo_renderer);
    renderer_result_t result = renderer_set_backend(&demo_renderer, backend);

    if (result != RENDERER_OK)
        return 0;

    /* The CPU Q0 path is a QUAD presentation.  Returning to FPGA selects the
     * normal BLIT surface so the shared writer is released cleanly. */
    if ((backend == RENDERER_BACKEND_FPGA) &&
        (demo_full_source == GPU_FULL_SOURCE_CPU)) {
        demo_full_source = GPU_FULL_SOURCE_BLIT;
        (void)gpu_set_display((demo_viewport_layout.mode ==
                               DEMO_VIEWPORT_MODE_QUAD) ?
                                  GPU_DISPLAY_MODE_QUAD :
                                  GPU_DISPLAY_MODE_FULL,
                              demo_full_source);
    }

    /* FPGA resumes from its last Front, while CPU mode deliberately does not
     * write pixels.  Re-seed the next FPGA frame from the simulated scene. */
    if ((previous != backend) && (backend == RENDERER_BACKEND_FPGA))
        demo_reseed_pending = 1u;
    if ((previous != backend) && (backend == RENDERER_BACKEND_FPGA) &&
        (demo_full_source == GPU_FULL_SOURCE_BLIT))
        full_blit_seed_pending = 1u;
    demo_publish_renderer_stats();
    return 1;
}

static u32 demo_quad_object_count(void);

static int demo_uart_protocol_active(void)
{
    return bg_uart_preparing() ||
           bg_uart_binary_active() || bg_uart_recovery_active();
}

static void demo_uart_write_uint(u32 value)
{
    char digits[10];
    u32 count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    while (count != 0u)
        uart_reply_write(BSP_UART_TERMINAL, digits[--count]);
}

static void demo_uart_options_status(void)
{
    u32 status = gpu_get_demo_options();
    uart_reply_writeStr(BSP_UART_TERMINAL, status & 1u ? " BURST=ON" : " BURST=OFF");
    uart_reply_writeStr(BSP_UART_TERMINAL, status & 2u ? " DBUF=ON" : " DBUF=OFF");
    uart_reply_writeStr(BSP_UART_TERMINAL, status & 4u ? " FIFO=ON" : " FIFO=OFF");
    uart_reply_writeStr(BSP_UART_TERMINAL, " OPTIONS_PENDING=");
    demo_uart_write_uint(demo_options_pending);
}
static void demo_uart_status_format(void)
{
    const renderer_stats_t *stats = renderer_get_stats(&demo_renderer);
    renderer_backend_t backend = renderer_get_backend(&demo_renderer);

    if (step1_active) {
        u32 cpu_objects;
        u32 gpu_objects;
        if (demo_confirmed_mode == DEMO_VIEWPORT_MODE_FULL) {
            uart_reply_writeStr(BSP_UART_TERMINAL, "STATUS MODE=FULL SOURCE=");
            uart_reply_writeStr(BSP_UART_TERMINAL, demo_confirmed_source == GPU_FULL_SOURCE_BLIT ? "BLIT" :
                          demo_confirmed_source == GPU_FULL_SOURCE_RECT ? "RECT" : "CPU");
            uart_reply_writeStr(BSP_UART_TERMINAL, " OBJECTS=");
            demo_uart_write_uint(demo_active_count);
            uart_reply_writeStr(BSP_UART_TERMINAL, " DISPLAY_GEN=");
            demo_uart_write_uint(gpu_get_display_generation());
            uart_reply_writeStr(BSP_UART_TERMINAL, " TRANSITION=");
            demo_uart_write_uint((u32)demo_switch_state);
            uart_reply_writeStr(BSP_UART_TERMINAL, " PENDING=");
            demo_uart_write_uint(demo_pending_valid);
            demo_uart_options_status();
            uart_reply_writeStr(BSP_UART_TERMINAL, " ERROR=");
            uart_reply_writeHex(BSP_UART_TERMINAL, (int)demo_panel_error_code());
            uart_reply_writeStr(BSP_UART_TERMINAL, "\r\n");
            return;
        }
        cpu_objects = step1_async_mode ? step1_async_cpu_count :
                      step1_scene_count;
        gpu_objects = step1_async_mode ? step1_gpu_count :
                      step1_scene_count;

        uart_reply_writeStr(BSP_UART_TERMINAL, "STATUS CPU_BLIT_RECT ");
        uart_reply_writeStr(BSP_UART_TERMINAL, step1_async_mode ? "ASYNC" : "SERIAL");
        uart_reply_writeStr(BSP_UART_TERMINAL, " QUAD CPU_OBJECTS=");
        demo_uart_write_uint(cpu_objects);
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_OBJECTS=");
        demo_uart_write_uint(gpu_objects);
        uart_reply_writeStr(BSP_UART_TERMINAL, " CPU_INFLIGHT=");
        demo_uart_write_uint(step1_async_cpu_inflight_count);
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_INFLIGHT=");
        demo_uart_write_uint(step1_async_gpu_inflight_count);
        uart_reply_writeStr(BSP_UART_TERMINAL, " RECT_OBJECTS=");
        demo_uart_write_uint(gpu_objects);
        uart_reply_writeStr(BSP_UART_TERMINAL, " COLLIDE=");
        uart_reply_writeStr(BSP_UART_TERMINAL,
                      step1_collision_requested ? "ON" : "OFF");
        uart_reply_writeStr(BSP_UART_TERMINAL, " CPU_COLLIDE=");
        uart_reply_writeStr(BSP_UART_TERMINAL,
                      (step1_async_mode ? step1_cpu_collision.shown_enabled :
                       step1_serial_collision.shown_enabled) ? "ON" : "OFF");
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_COLLIDE=");
        uart_reply_writeStr(BSP_UART_TERMINAL,
                      (step1_async_mode ? step1_gpu_collision.shown_enabled :
                       step1_serial_collision.shown_enabled) ? "ON" : "OFF");
        uart_reply_writeStr(BSP_UART_TERMINAL, " Q2_X=");
        demo_uart_write_uint(gpu_objects != 0u ?
                             (u32)(step1_async_mode ?
                                   step1_async_gpu_shown[0].x :
                                   step1_shown[0].x) : 0u);
        uart_reply_writeStr(BSP_UART_TERMINAL, " CPU_FRAMES=");
        demo_uart_write_uint(step1_async_cpu_frames);
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_FRAMES=");
        demo_uart_write_uint(step1_async_gpu_frames);
        uart_reply_writeStr(BSP_UART_TERMINAL, " SEED_FRAMES=");
        demo_uart_write_uint(step1_seed_frames);
        uart_reply_writeStr(BSP_UART_TERMINAL, " MOTION_FRAMES=");
        demo_uart_write_uint(step1_motion_frames);
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_COMMANDS=");
        demo_uart_write_uint(step1_frame_commands);
        uart_reply_writeStr(BSP_UART_TERMINAL, " BLOCKCOPY_TOTAL_CYCLES=");
        uart_reply_writeHex(BSP_UART_TERMINAL, (int)gpu_test_perf.blockcopy_total_cycles);
        uart_reply_writeStr(BSP_UART_TERMINAL, " CPU_STATUS=");
        uart_reply_writeHex(BSP_UART_TERMINAL, (int)cpu_writer_status());
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_STATUS=");
        uart_reply_writeHex(BSP_UART_TERMINAL, (int)gpu_get_status());
        uart_reply_writeStr(BSP_UART_TERMINAL, " Q0_SEQ=");
        demo_uart_write_uint(gpu_get_async_q0_seq());
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_SEQ=");
        demo_uart_write_uint(gpu_get_async_gpu_seq());
        uart_reply_writeStr(BSP_UART_TERMINAL, " ASYNC_PENDING=");
        demo_uart_write_uint(step1_async_pending);
        uart_reply_writeStr(BSP_UART_TERMINAL, " ASYNC_WAIT=");
        demo_uart_write_uint(step1_async_switch_wait);
        uart_reply_writeStr(BSP_UART_TERMINAL, " ASYNC_ERROR=");
        uart_reply_writeHex(BSP_UART_TERMINAL, step1_async_switch_error);
        uart_reply_writeStr(BSP_UART_TERMINAL, " GPU_WAIT=");
        demo_uart_write_uint(gpu_get_async_owner_wait());
        uart_reply_writeStr(BSP_UART_TERMINAL, " EVENTS=");
        uart_reply_writeHex(BSP_UART_TERMINAL, (int)gpu_get_async_event_status());
        uart_reply_writeStr(BSP_UART_TERMINAL, " ERROR_INFO=");
        uart_reply_writeHex(BSP_UART_TERMINAL, (int)gpu_get_error_info());
        uart_reply_writeStr(BSP_UART_TERMINAL, " FAILED=");
        demo_uart_write_uint(step1_failed);
        demo_uart_options_status();
        uart_reply_writeStr(BSP_UART_TERMINAL, "\r\n");
        return;
    }
    uart_reply_writeStr(BSP_UART_TERMINAL, "STATUS MODE=");
    uart_reply_writeStr(BSP_UART_TERMINAL,
                  demo_viewport_layout.mode == DEMO_VIEWPORT_MODE_QUAD ?
                      "QUAD" : "FULL");
    uart_reply_writeStr(BSP_UART_TERMINAL, " NUM=");
    demo_uart_write_uint(demo_active_count);
    if (demo_viewport_layout.mode == DEMO_VIEWPORT_MODE_QUAD) {
        uart_reply_writeStr(BSP_UART_TERMINAL, " QUAD_OBJECTS=");
        demo_uart_write_uint(demo_quad_object_count());
    }
    uart_reply_writeStr(BSP_UART_TERMINAL, " SPD=");
    demo_uart_write_uint(demo_speed_level);
    uart_reply_writeStr(BSP_UART_TERMINAL, " LEVEL=");
    demo_uart_write_uint(demo_level);
    uart_reply_writeStr(BSP_UART_TERMINAL, " RENDER=");
    uart_reply_writeStr(BSP_UART_TERMINAL,
                  backend == RENDERER_BACKEND_CPU ? "CPU" : "FPGA");
    uart_reply_writeStr(BSP_UART_TERMINAL, " FULL_SOURCE=");
    uart_reply_writeStr(BSP_UART_TERMINAL,
                  demo_full_source == GPU_FULL_SOURCE_BLIT ? "BLIT" :
                  demo_full_source == GPU_FULL_SOURCE_RECT ? "RECT" : "CPU");
    uart_reply_writeStr(BSP_UART_TERMINAL, " FRAME_CYCLES=");
    uart_reply_writeHex(BSP_UART_TERMINAL, (int)gpu_test_perf.frame_cycles);
    uart_reply_writeStr(BSP_UART_TERMINAL, " BLOCKCOPY_TOTAL_CYCLES=");
    uart_reply_writeHex(BSP_UART_TERMINAL,
                  (int)gpu_test_perf.blockcopy_total_cycles);
    uart_reply_writeStr(BSP_UART_TERMINAL, " ERROR=");
    uart_reply_writeHex(BSP_UART_TERMINAL,
                  stats != 0 ? (int)stats->errors : 0);
    uart_reply_writeStr(BSP_UART_TERMINAL, "\r\n");
}

static void demo_uart_status(void)
{
    if (demo_uart_protocol_active()) { demo_uart_status_format(); return; }
    if (!uart_reply_begin(UART_REPLY_QUERY_MAX)) return;
    demo_uart_status_format();
    uart_reply_commit();
}

static int demo_uart_line_ready(const char *line)
{
    line = demo_skip_spaces(line);
    if (demo_uart_protocol_active()) return !uart_reply_pending();
    if (demo_text_equal(line, "STATUS"))
        return uart_reply_query_ready();
    /* A protocol BEGIN / synchronous response must follow the complete text
     * already queued. */
    return !uart_reply_pending();
}

#include "scope_demo.h"

static void demo_uart_apply_line(const char *line)
{
    const char *argument;
    u32 value;
    /* A command may interrupt the one-byte telemetry sender. End its partial
     * line before any protocol response; do not resume that stale suffix. */
    if (demo_telemetry.tx_pos != 0u &&
        demo_telemetry.tx_pos < demo_telemetry.tx_len) {
        if (!demo_uart_protocol_active() &&
            demo_text_equal(demo_skip_spaces(line), "STATUS"))
            uart_reply_event("\r\n");
        else
            uart_writeStr(BSP_UART_TERMINAL, "\r\n");
    }
    demo_telemetry.tx_pos = demo_telemetry.tx_len = 0u;

    if (scope_uart(line)) return;
    if (scope_requested &&
        !demo_text_equal(demo_skip_spaces(line), "STATUS") &&
        !demo_command_arg(line, "BURST", &argument) &&
        !demo_command_arg(line, "DBUF", &argument)) {
        uart_writeStr(BSP_UART_TERMINAL, "ERR SCOPE_LOCKED: SCOPE STOP before changing workload\r\n");
        return;
    }
    if (bg_uart_handle_line(line))
        return;

    if (step1_active) {
        if (demo_text_equal(demo_skip_spaces(line), "STATUS")) {
            demo_uart_status();
        } else if (demo_command_arg(line, "MODE", &argument)) {
            if (demo_text_equal(argument, "FULL")) {
                demo_request_display(DEMO_VIEWPORT_MODE_FULL, demo_full_source);
                uart_writeStr(BSP_UART_TERMINAL, "OK MODE FULL PENDING\r\n");
            } else if (demo_text_equal(argument, "QUAD")) {
                demo_request_display(DEMO_VIEWPORT_MODE_QUAD, demo_full_source);
                uart_writeStr(BSP_UART_TERMINAL, "OK MODE QUAD PENDING\r\n");
            } else {
                uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_MODE\r\n");
            }
        } else if (demo_command_arg(line, "FULL", &argument)) {
            u32 source;
            if (demo_text_equal(argument, "BLIT")) source = GPU_FULL_SOURCE_BLIT;
            else if (demo_text_equal(argument, "RECT")) source = GPU_FULL_SOURCE_RECT;
            else if (demo_text_equal(argument, "CPU")) source = GPU_FULL_SOURCE_CPU;
            else {
                uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_FULL_SOURCE\r\n");
                return;
            }
            demo_request_display(DEMO_VIEWPORT_MODE_FULL, source);
            uart_writeStr(BSP_UART_TERMINAL, "OK FULL ");
            uart_writeStr(BSP_UART_TERMINAL, source == GPU_FULL_SOURCE_BLIT ?
                          "BLIT PENDING\r\n" : source == GPU_FULL_SOURCE_RECT ?
                          "RECT PENDING\r\n" : "CPU PENDING\r\n");
        } else if (demo_command_arg(line, "BURST", &argument) ||
                   demo_command_arg(line, "DBUF", &argument)) {
            u32 bit = demo_command_arg(line, "BURST", &argument) ? 1u : 2u;
            if (demo_text_equal(argument, "ON")) value = 1u;
            else if (demo_text_equal(argument, "OFF")) value = 0u;
            else { uart_writeStr(BSP_UART_TERMINAL, "ERR OPTION (ON|OFF)\r\n"); return; }
            if (scope_requested) scope_disarm();
            demo_options_requested = (demo_options_requested & ~bit) | (value ? bit : 0u);
            demo_options_pending = 1u;
            step1_async_freeze_new = 1u;
            demo_request_display(demo_pending_valid ? demo_pending_mode : demo_confirmed_mode,
                                 demo_pending_valid ? demo_pending_source : demo_confirmed_source);
            uart_writeStr(BSP_UART_TERMINAL, "OK OPTION PENDING; STATUS shows applied state\r\n");
        } else if (demo_text_equal(demo_skip_spaces(line), "ASYNC ON")) {
            if (demo_confirmed_mode != DEMO_VIEWPORT_MODE_QUAD ||
                step1_seed_frames < 2u || step1_async_mode != 0u ||
                step1_async_pending != 0u || step1_async_off_pending != 0u) {
                uart_writeStr(BSP_UART_TERMINAL, "ERR ASYNC_QUAD_ONLY_OR_NOT_IDLE\r\n");
            } else {
                step1_async_pending = 1u;
                step1_async_switch_wait = 0u;
                step1_async_switch_error = 0;
                uart_writeStr(BSP_UART_TERMINAL, "OK ASYNC ON; pending\r\n");
            }
        } else if (demo_text_equal(demo_skip_spaces(line), "ASYNC OFF")) {
            if (demo_confirmed_mode != DEMO_VIEWPORT_MODE_QUAD) {
                uart_writeStr(BSP_UART_TERMINAL, "ERR ASYNC_QUAD_ONLY\r\n");
            } else if (step1_async_mode == 0u) {
                demo_async_resume_requested = 0u;
                uart_writeStr(BSP_UART_TERMINAL, "OK ASYNC OFF\r\n");
            } else {
                demo_async_resume_requested = 0u;
                step1_async_off_pending = 1u;
                step1_async_freeze_new = 1u;
                uart_writeStr(BSP_UART_TERMINAL, "OK ASYNC OFF; pending\r\n");
            }
        } else if (demo_command_arg(line, "COLLIDE", &argument)) {
            if (demo_text_equal(argument, "ON")) value = 1u;
            else if (demo_text_equal(argument, "OFF")) value = 0u;
            else {
                uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_COLLIDE (ON|OFF)\r\n");
                return;
            }
            if (value != step1_collision_requested) {
                step1_collision_requested = value;
                step1_layout_epoch++;
            }
            uart_writeStr(BSP_UART_TERMINAL,
                          value ? "OK COLLIDE ON PENDING\r\n" :
                                  "OK COLLIDE OFF PENDING\r\n");
        } else if (demo_command_arg(line, "NUM", &argument) &&
                   demo_parse_uint(argument, &value) &&
                   ((value == 1u) || (value == 2u) || (value == 4u) ||
                    (value == 8u) || (value == 16u) || (value == 20u) ||
                     (((value == 32u) || (value == 64u)) &&
                      demo_confirmed_mode == DEMO_VIEWPORT_MODE_FULL &&
                      demo_switch_state == DEMO_SWITCH_IDLE && !demo_pending_valid))) {
            if (step1_pending_count != value) {
                step1_pending_count = value;
                step1_layout_epoch++;
            }
            uart_writeStr(BSP_UART_TERMINAL, "OK NUM ");
            demo_uart_write_uint(value);
            uart_writeStr(BSP_UART_TERMINAL, " PENDING\r\n");
        } else if (demo_command_arg(line, "NUM", &argument)) {
            uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_NUM (QUAD:1,2,4,8,16,20; FULL adds 32,64 after switch completes)\r\n");
        } else {
            uart_writeStr(BSP_UART_TERMINAL,
                          "ERR COMMAND: STATUS MODE FULL|QUAD FULL BLIT|RECT|CPU ASYNC ON|OFF NUM 1|2|4|8|16|20 (FULL also 32|64) COLLIDE ON|OFF BURST ON|OFF DBUF ON|OFF\r\n");
        }
        return;
    }
    if (demo_command_arg(line, "MODE", &argument)) {
        if (demo_text_equal(argument, "FULL")) {
            (void)gpu_set_display(GPU_DISPLAY_MODE_FULL, demo_full_source);
            demo_viewport_configure(DEMO_VIEWPORT_MODE_FULL);
            if (demo_full_source == GPU_FULL_SOURCE_BLIT)
                full_blit_seed_pending = 1u;
            demo_reseed_pending = 1u;
            uart_writeStr(BSP_UART_TERMINAL, "OK MODE FULL\r\n");
        } else if (demo_text_equal(argument, "QUAD")) {
            (void)gpu_set_display(GPU_DISPLAY_MODE_QUAD, demo_full_source);
            demo_viewport_configure(DEMO_VIEWPORT_MODE_QUAD);
            demo_reseed_pending = 1u;
            uart_writeStr(BSP_UART_TERMINAL, "OK MODE QUAD\r\n");
        } else {
            uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_MODE\r\n");
        }
        return;
    }

    if (demo_command_arg(line, "FULL", &argument)) {
        u32 source;
        if (demo_text_equal(argument, "BLIT"))
            source = GPU_FULL_SOURCE_BLIT;
        else if (demo_text_equal(argument, "RECT"))
            source = GPU_FULL_SOURCE_RECT;
        else if (demo_text_equal(argument, "CPU"))
            source = GPU_FULL_SOURCE_CPU;
        else {
            uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_FULL_SOURCE\r\n");
            return;
        }
        if (source == GPU_FULL_SOURCE_CPU) {
            demo_full_source = source;
            (void)gpu_set_display(GPU_DISPLAY_MODE_QUAD, source);
            demo_viewport_configure(DEMO_VIEWPORT_MODE_QUAD);
            demo_reseed_pending = 0u;
            uart_writeStr(BSP_UART_TERMINAL, "OK CPU_Q0 QUAD\r\n");
            return;
        }
        if ((source != GPU_FULL_SOURCE_CPU) &&
            (renderer_get_backend(&demo_renderer) == RENDERER_BACKEND_CPU) &&
            !demo_select_renderer(RENDERER_BACKEND_FPGA)) {
            uart_writeStr(BSP_UART_TERMINAL, "ERR RENDER BUSY\r\n");
            return;
        }
        demo_full_source = source;
        (void)gpu_set_display(GPU_DISPLAY_MODE_FULL, source);
        demo_viewport_configure(DEMO_VIEWPORT_MODE_FULL);
        if (source == GPU_FULL_SOURCE_BLIT)
            full_blit_seed_pending = 1u;
        demo_reseed_pending = 1u;
        uart_writeStr(BSP_UART_TERMINAL, "OK FULL ");
        uart_writeStr(BSP_UART_TERMINAL, source == GPU_FULL_SOURCE_BLIT ?
                      "BLIT\r\n" : source == GPU_FULL_SOURCE_RECT ?
                      "RECT\r\n" : "CPU\r\n");
        return;
    }

    if (demo_command_arg(line, "RENDER", &argument)) {
        if (demo_text_equal(argument, "FPGA")) {
            if (demo_select_renderer(RENDERER_BACKEND_FPGA))
                uart_writeStr(BSP_UART_TERMINAL, "OK RENDER FPGA\r\n");
            else
                uart_writeStr(BSP_UART_TERMINAL, "ERR RENDER BUSY\r\n");
        } else if (demo_text_equal(argument, "CPU")) {
            if (demo_select_renderer(RENDERER_BACKEND_CPU))
                uart_writeStr(BSP_UART_TERMINAL, "OK RENDER CPU\r\n");
            else
                uart_writeStr(BSP_UART_TERMINAL, "ERR RENDER BUSY\r\n");
        } else {
            uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_RENDERER\r\n");
        }
        return;
    }

    if (demo_command_arg(line, "NUM", &argument)) {
        if (!demo_parse_uint(argument, &value)) {
            uart_writeStr(BSP_UART_TERMINAL, "ERR SYNTAX\r\n");
        } else if ((value == 10u) || (value == 20u) ||
                   (value == 30u) || (value == 50u) || (value == 53u)) {
            demo_active_count = value;
            /* Recreate source pixels after a count change.  This is needed
             * because Front-relative BLIT cannot recover objects cleared
             * from Back while they were disabled. */
            demo_init_objects();
            demo_reseed_pending = 1u;
            uart_writeStr(BSP_UART_TERMINAL, "OK NUM ");
            demo_uart_write_uint(value);
            uart_writeStr(BSP_UART_TERMINAL, "\r\n");
        } else {
            uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_NUM\r\n");
        }
        return;
    }

    if (demo_command_arg(line, "SPD", &argument)) {
        if (!demo_parse_uint(argument, &value) ||
            (value < 1u) || (value > 4u)) {
            uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_SPD\r\n");
        } else {
            demo_speed_level = value;
            uart_writeStr(BSP_UART_TERMINAL, value == 1u ? "OK SPD 1\r\n" :
                          value == 2u ? "OK SPD 2\r\n" :
                          value == 3u ? "OK SPD 3\r\n" : "OK SPD 4\r\n");
        }
        return;
    }

    if (demo_command_arg(line, "LEVEL", &argument)) {
        if (!demo_parse_uint(argument, &value) || (value > 3u)) {
            uart_writeStr(BSP_UART_TERMINAL, "ERR BAD_LEVEL\r\n");
        } else {
            demo_level = value;
            /* LEVEL selects the requested scene pressure.  LEVEL3 is the
             * FIFO64 stress setting: four backgrounds plus seven monitor
             * blocks leave room for 53 QUAD objects. */
            demo_active_count = (value == 0u) ? 10u :
                                (value == 1u) ? 20u :
                                (value == 2u) ? 30u : 53u;
            demo_init_objects();
            demo_reseed_pending = 1u;
            uart_writeStr(BSP_UART_TERMINAL, value == 0u ? "OK LEVEL 0\r\n" :
                          value == 1u ? "OK LEVEL 1\r\n" :
                          value == 2u ? "OK LEVEL 2\r\n" : "OK LEVEL 3\r\n");
        }
        return;
    }

    if (demo_text_equal(demo_skip_spaces(line), "STATUS")) {
        demo_uart_status();
        return;
    }

    uart_writeStr(BSP_UART_TERMINAL, "ERR UNKNOWN_COMMAND\r\n");
}

static unsigned demo_uart_line_pending;
/* Keep one read-only query pending while the response queue drains.
 * Later commands retain their original order. */
static char demo_uart_deferred_status[DEMO_UART_LINE_SIZE];
static unsigned demo_uart_deferred_status_pending;
static int demo_uart_is_status(const char *line)
{
    line = demo_skip_spaces(line);
    return demo_text_equal(line,"STATUS");
}
static void demo_uart_dispatch_pending(void)
{
    if (demo_uart_deferred_status_pending) {
        if (demo_uart_line_ready(demo_uart_deferred_status)) {
            demo_uart_apply_line(demo_uart_deferred_status);
            demo_uart_deferred_status_pending = 0u;
        } else return;
    }
    if (!demo_uart_line_pending) return;
    if (demo_uart_line_overflow) {
        if (demo_uart_protocol_active()) {
            /* Keep protocol diagnostics out of the suspended text queue. */
            uart_writeStr(BSP_UART_TERMINAL, "ERR OVERFLOW\r\n");
        } else {
            if (!uart_reply_query_ready()) return;
            uart_reply_event("ERR OVERFLOW\r\n");
        }
    } else {
        if (!demo_uart_line_ready(demo_uart_line)) {
            if (!demo_uart_protocol_active() && !demo_uart_deferred_status_pending &&
                demo_uart_is_status(demo_uart_line)) {
                memcpy(demo_uart_deferred_status,demo_uart_line,demo_uart_line_length+1u);
                demo_uart_deferred_status_pending = 1u;
                demo_uart_line_pending = demo_uart_line_length = demo_uart_line_overflow = 0u;
            }
            return;
        }
        demo_uart_apply_line(demo_uart_line);
    }
    demo_uart_line_pending = demo_uart_line_length = demo_uart_line_overflow = 0u;
}

static void demo_uart_poll(void)
{
    u32 budget = DEMO_UART_POLL_BUDGET;
    int resource_guard = renderer_cpu_sync_waiting();

    bg_uart_set_resource_guard(resource_guard);
    if (resource_guard)
        bg_uart_service_protocol_only();
    else
        bg_uart_service();
    if (!demo_uart_protocol_active()) uart_reply_service();
    if (demo_uart_deferred_status_pending) demo_uart_dispatch_pending();

    if (demo_uart_line_pending) {
        demo_uart_dispatch_pending();
        budget = 0u;
    }
    while ((budget != 0u)) {
        uint8_t character;
        if (!bg_uart_read_byte(&character))
            break;
        budget--;

        /* This remains the only physical UART reader.  Once CHECK has
         * accepted its command, every byte, including CR/LF/CAN and command
         * looking payload, belongs to the BG state machine. */
        if (bg_uart_binary_active() ||
            bg_uart_recovery_active() ||
            (bg_uart_replay_active() &&
             ((character == 0x04u) || (character == 0x18u))) ||
            (bg_uart_preparing() && (character == 0x18u))) {
            (void)bg_uart_consume_byte(character);
            continue;
        }

        if ((character == '\r') || (character == '\n')) {
            if (demo_uart_line_overflow || demo_uart_line_length) {
                demo_uart_line[demo_uart_line_length] = '\0';
                demo_uart_line_pending = 1u;
                demo_uart_dispatch_pending();
                if (demo_uart_line_pending) budget = 0u;
            }
        } else if (demo_uart_line_overflow == 0u) {
            if (demo_uart_line_length < (DEMO_UART_LINE_SIZE - 1u)) {
                demo_uart_line[demo_uart_line_length++] = (char)character;
            } else {
                demo_uart_line_overflow = 1u;
            }
        }
    }
    bg_uart_set_resource_guard(0);
}

/* The main scene scheduler must yield while BG owns preparation.  This is
 * intentionally separate from binary_active(): PREPARING has no packet yet,
 * but it still must not launch another CPU frame. */
static int demo_scene_work_blocked(void)
{
    return bg_uart_preparing() || bg_uart_binary_active() ||
           bg_uart_recovery_active();
}

static u32 demo_quad_object_count(void)
{
    return quad_safe_object_count(demo_active_count);
}

static u32 demo_expected_frame_commands(void)
{
    if (demo_viewport_layout.mode == DEMO_VIEWPORT_MODE_QUAD)
        return quad_command_count(demo_quad_object_count(),
                                  MONITOR_RECT_COUNT);
    return 1u + demo_active_count;
}

static void demo_quad_monitor_info(monitor_info_t *info)
{
    const renderer_stats_t *stats = renderer_get_stats(&demo_renderer);

    if (info == 0)
        return;
    info->mode = (u32)demo_viewport_layout.mode;
    info->object_count = demo_quad_object_count();
    info->backend = renderer_get_backend(&demo_renderer);
    info->fps = demo_fps;
    info->frame_cycles = gpu_test_perf.frame_cycles;
    info->error = (stats != 0) ? stats->errors : 0u;
}

static renderer_result_t demo_submit_quad_monitor(void)
{
    monitor_info_t info;
    const quad_viewport_t *monitor_view =
        &demo_quad_viewports[QUAD_VIEWPORT_COUNT - 1u];

    demo_quad_monitor_info(&info);
    return monitor_render(&demo_renderer,
                          monitor_view->origin_x, monitor_view->origin_y,
                          monitor_view->width, monitor_view->height,
                          &info);
}

static void demo_begin_background(void)
{
    render_item_t item;
    renderer_result_t result;

    result = renderer_begin_frame(&demo_renderer);
    gpu_test_begin_result = demo_gpu_result(result);
    if (gpu_test_begin_result != GPU_OK)
        demo_stop("begin_frame", gpu_test_begin_result);

    if (demo_viewport_layout.mode == DEMO_VIEWPORT_MODE_QUAD) {
        static const uint16_t quad_colors[QUAD_VIEWPORT_COUNT] = {
            0x0000u, 0x0841u, 0x0020u, 0x1082u
        };

        result = quad_submit_backgrounds(&demo_renderer,
                                         demo_quad_viewports,
                                         quad_colors);
        gpu_test_background_result = demo_gpu_result(result);
        if (gpu_test_background_result != GPU_OK)
            demo_stop("quad_background", gpu_test_background_result);
        return;
    }

    item.kind = RENDER_ITEM_RECT;
    item.target_surface_id = demo_full_target_id();
    item.x = 0u;
    item.y = 0u;
    item.width = GPU_SCREEN_WIDTH;
    item.height = GPU_SCREEN_HEIGHT;
    item.color = (u16)0x0000u;
    item.src_offset = 0u;
    item.src_stride = 0u;
    result = renderer_submit_item(&demo_renderer, &item);
    gpu_test_background_result = demo_gpu_result(result);
    if (gpu_test_background_result != GPU_OK)
        demo_stop("background", gpu_test_background_result);
}

static void demo_complete_frame(u32 expected_command_count)
{
    const renderer_stats_t *stats;
    gpu_perf_t perf;
    renderer_result_t result;
    u64 frame_start_time = clint_getTime(BSP_CLINT);

    /* The current frame uses one background RECT plus one item per active
     * object.  NUM is intentionally limited so the FPGA path remains
     * FIFO-safe; the CPU path checks the same logical item count. */
    gpu_test_frame_command_count =
        renderer_frame_item_count(&demo_renderer);
    if (gpu_test_frame_command_count != expected_command_count)
        demo_stop("command_count", GPU_ERR_HW);

    result = renderer_submit_frame(&demo_renderer);
    gpu_test_submit_result = demo_gpu_result(result);
    if (gpu_test_submit_result != GPU_OK)
        demo_stop("submit", gpu_test_submit_result);

    result = renderer_wait_done(&demo_renderer, GPU_DONE_POLL_LIMIT);
    gpu_test_wait_result = demo_gpu_result(result);
    if (gpu_test_wait_result != GPU_OK)
        demo_stop("wait_done", gpu_test_wait_result);

    stats = renderer_get_stats(&demo_renderer);
    if (stats == 0)
        demo_stop("stats", GPU_ERR_HW);

    if (renderer_get_backend(&demo_renderer) == RENDERER_BACKEND_FPGA) {
        gpu_test_perf_result = demo_gpu_result(
            renderer_get_last_gpu_perf(&demo_renderer, &perf));
        if (gpu_test_perf_result != GPU_OK)
            demo_stop("get_perf", gpu_test_perf_result);
        gpu_test_perf = perf;
        demo_panel_perf_generation++;
        gpu_test_status = stats->last_gpu_status;
        gpu_test_fifo_status = stats->last_gpu_fifo_status;
        gpu_test_error_info = stats->last_gpu_error_info;
    } else {
        /* CPU backend intentionally has no GPU MMIO side effects.  The
         * software timer interval is exposed as FRAME_CYCLES so STATUS still
         * reports a useful cost without pretending the CPU owns DDR. */
        u64 cpu_frame_end = clint_getTime(BSP_CLINT);
        u64 cpu_elapsed = (cpu_frame_end > frame_start_time) ?
                          cpu_frame_end - frame_start_time : 0u;
        gpu_test_perf_result = GPU_OK;
        gpu_test_perf.blockcopy_cycles = 0u;
        gpu_test_perf.blockcopy_total_cycles = 0u;
        gpu_test_perf.rect_last_cycles = 0u;
        gpu_test_perf.rect_total_cycles = 0u;
        gpu_test_perf.frame_cycles = (cpu_elapsed > 0xFFFFFFFFu) ?
                                     0xFFFFFFFFu : (u32)cpu_elapsed;
        gpu_test_perf.rect_count = 0u;
        gpu_test_status = 0u;
        gpu_test_fifo_status = 0u;
        gpu_test_error_info = 0u;
    }

    demo_publish_renderer_stats();
    gpu_test_frame_count++;

    /* FPS is derived from the SoC timer, so CPU Q0 mode and FPGA
     * mode share the same measurement path without a framebuffer assumption. */
    {
        u64 now = clint_getTime(BSP_CLINT);
        if ((demo_last_frame_time != 0u) &&
            (now > demo_last_frame_time)) {
            u64 elapsed = now - demo_last_frame_time;
            u64 measured = (u64)BSP_CLINT_HZ / elapsed;
            demo_fps = (measured > 999u) ? 999u : (u32)measured;
        }
        demo_last_frame_time = now;
    }
}

static const int32_t step1_seed_x[STEP1_MAX_OBJECTS] = {
    72, 240, 408, 576, 744, 72, 240, 408, 576, 744,
    72, 240, 408, 576, 744, 72, 240, 408, 576, 744
};
static const int32_t step1_seed_y[STEP1_MAX_OBJECTS] = {
    72, 72, 72, 72, 72, 192, 192, 192, 192, 192,
    312, 312, 312, 312, 312, 432, 432, 432, 432, 432
};
static const int32_t step1_seed_vx[STEP1_MAX_OBJECTS] = {
    8, -8, 8, -8, 8, -8, 8, -8, 8, -8,
    8, -8, 8, -8, 8, -8, 8, -8, 8, -8
};
static const int32_t step1_seed_vy[STEP1_MAX_OBJECTS] = {
    4, 4, -4, -4, 4, 4, 4, -4, -4, 4,
    4, 4, -4, -4, 4, 4, 4, -4, -4, 4
};

static void step1_seed_object(blit_object_t *object, u32 index)
{
    object->x = step1_seed_x[index];
    object->y = step1_seed_y[index];
    object->vx = step1_seed_vx[index];
    object->vy = step1_seed_vy[index];
    object->width = STEP1_OBJECT_SIZE;
    object->height = STEP1_OBJECT_SIZE;
    object->color = demo_colors[index % 20u];
    object->enable = 1u;
}

static void step1_collision_present(step1_collision_state_t *collision)
{
    collision->shown_epoch = collision->frame_epoch;
    collision->shown_enabled = collision->frame_enabled;
}

static void step1_plan_scene_for(const blit_object_t *shown,
                                 u32 shown_count,
                                 blit_object_t *planned,
                                 u32 *next_count,
                                 step1_collision_state_t *collision,
                                 int advance)
{
    u32 i;
    u32 count = step1_pending_count;
    u32 epoch = step1_layout_epoch;
    u32 collide = step1_collision_requested && step1_seed_frames >= 2u;
    int reseed;

    if ((count == 0u) || (count > STEP1_MAX_OBJECTS))
        count = shown_count;
    /* Freeze configuration for this frame; UART may change the request
     * while it is being rendered. Only its present acknowledges this copy. */
    collision->frame_epoch = epoch;
    collision->frame_enabled = collide;
    reseed = collide && (!collision->shown_enabled ||
                         collision->shown_epoch != epoch ||
                         count != shown_count);
    for (i = 0u; i < count; i++) {
        if (!reseed && i < shown_count)
            planned[i] = shown[i];
        else
            step1_seed_object(&planned[i], i);
    }
    *next_count = count;
    /* Present the non-overlapping grid once, without touching shown/Front.
     * Existing erases still use the OLD count/positions and source tile. */
    if (reseed || !advance)
        return;

    for (i = 0u; i < count; i++)
        planned[i].x = step1_next_axis(planned[i].x, planned[i].vx,
                                       STEP1_MIN_X, STEP1_MAX_X,
                                       &planned[i].vx);
    if (collide)
        step1_resolve_axis(shown, planned, count, 1);
    for (i = 0u; i < count; i++)
        planned[i].y = step1_next_axis(planned[i].y, planned[i].vy,
                                       STEP1_MIN_Y, STEP1_MAX_Y,
                                       &planned[i].vy);
    if (collide)
        step1_resolve_axis(shown, planned, count, 0);
}

static void step1_plan_scene(u32 *next_count)
{
    step1_plan_scene_for(step1_shown, step1_scene_count, step1_planned,
                         next_count, &step1_serial_collision,
                         step1_seed_frames >= 2u && step1_motion_frames != 0u);
}

static u32 step1_command_budget(u32 old_count, u32 new_count)
{
    (void)old_count;
    return 3u + 2u * new_count + (step1_seed_frames < 2u ? 20u : 0u);
}
/* Each BLIT reads an immutable DDR tile, so overlaps, NUM changes and
 * single-buffer drawing cannot corrupt the source colours. */
static renderer_result_t step1_submit_coloured_gpu(const blit_object_t *planned,
                                                   u32 count, int serial)
{
    render_item_t item = {0};
    renderer_result_t result;
    u32 target, i;
    if (serial && step1_seed_frames < 2u) {
        result = demo_seed_palette(GPU_TARGET_Q1);
        if (result != RENDERER_OK) return result;
    }
    for (target = GPU_TARGET_Q1; target <= GPU_TARGET_Q2; ++target) {
        item.kind = RENDER_ITEM_RECT;
        item.target_surface_id = target;
        item.x = item.y = 0u; item.width = 960u; item.height = 540u;
        item.color = target == GPU_TARGET_Q1 ? 0x0841u : 0x0020u;
        item.src_offset = item.src_stride = 0u;
        result = renderer_submit_item(&demo_renderer, &item);
        if (result != RENDERER_OK) return result;
        for (i = 0u; i < count; ++i) {
            item.kind = target == GPU_TARGET_Q1 ? RENDER_ITEM_BLIT : RENDER_ITEM_RECT;
            item.x = (u32)planned[i].x; item.y = (u32)planned[i].y;
            item.width = item.height = STEP1_OBJECT_SIZE;
            item.color = planned[i].color;
            item.src_offset = target == GPU_TARGET_Q1 ? demo_palette_offset(i) : 0u;
            item.src_stride = target == GPU_TARGET_Q1 ? GPU_PALETTE_STRIDE : 0u;
            result = renderer_submit_item(&demo_renderer, &item);
            if (result != RENDERER_OK) return result;
        }
    }
    if (serial) {
        item.kind = RENDER_ITEM_RECT; item.target_surface_id = GPU_TARGET_Q3;
        item.x = item.y = 0u; item.width = 960u; item.height = 540u;
        item.color = 0x1082u; item.src_offset = item.src_stride = 0u;
        return renderer_submit_item(&demo_renderer, &item);
    }
    return RENDERER_OK;
}
static renderer_result_t step1_submit_gpu_scene(u32 old_count, u32 new_count)
{
    (void)old_count;
    return step1_submit_coloured_gpu(step1_planned, new_count, 1);
}
static renderer_result_t step1_submit_gpu_async_scene(
    const blit_object_t *shown, const blit_object_t *planned,
    u32 old_count, u32 new_count)
{
    (void)shown; (void)old_count;
    return step1_submit_coloured_gpu(planned, new_count, 0);
}

static int step1_async_start_gpu(void)
{
    renderer_result_t result;
    u32 next_count;
    u32 expected;

    if (step1_async_gpu_state != 0u)
        return 0;
    step1_plan_scene_for(step1_async_gpu_shown, step1_gpu_count,
                         step1_async_gpu_planned, &next_count,
                         &step1_gpu_collision, 1);
    expected = 2u + (2u * next_count);
    if ((next_count == 0u) || (next_count > STEP1_MAX_OBJECTS) ||
        (expected > 64u))
        return -12;

    result = renderer_begin_frame(&demo_renderer);
    if (result != RENDERER_OK)
        return -1;
    result = step1_submit_gpu_async_scene(step1_async_gpu_shown,
                                          step1_async_gpu_planned,
                                          step1_gpu_count, next_count);
    if (result != RENDERER_OK)
        return -2;
    if (renderer_frame_item_count(&demo_renderer) != expected)
        return -9;
    step1_frame_commands = expected;
    gpu_test_frame_command_count = expected;
    step1_async_gpu_inflight_count = next_count;
    /* Capture the reference immediately before the hardware commit. */
    step1_async_gpu_seq_base = gpu_get_async_gpu_seq();
    result = renderer_submit_frame(&demo_renderer);
    if (result != RENDERER_OK)
        return -3;
    step1_async_gpu_state = 1u;
    return 0;
}

static int step1_async_start_cpu(void)
{
    render_item_t cpu_items[STEP1_MAX_OBJECTS + 1u];
    u32 next_count;
    u32 i;

    if ((step1_async_cpu_state != 0u) || renderer_cpu_async_busy())
        return 0;
    step1_plan_scene_for(step1_async_cpu_shown, step1_async_cpu_count,
                         step1_async_cpu_planned, &next_count,
                         &step1_cpu_collision, 1);
    if ((next_count == 0u) || (next_count > STEP1_MAX_OBJECTS))
        return -13;
    cpu_items[0].kind = RENDER_ITEM_RECT;
    cpu_items[0].target_surface_id = GPU_TARGET_Q0;
    cpu_items[0].x = 0u; cpu_items[0].y = 0u;
    cpu_items[0].width = CPU_Q0_WIDTH_PIXELS;
    cpu_items[0].height = CPU_Q0_HEIGHT_PIXELS;
    cpu_items[0].color = 0u; cpu_items[0].src_offset = 0u;
    cpu_items[0].src_stride = 0u;
    for (i = 0u; i < next_count; i++) {
        cpu_items[i + 1u] = cpu_items[0];
        cpu_items[i + 1u].x = (u32)step1_async_cpu_planned[i].x;
        cpu_items[i + 1u].y = (u32)step1_async_cpu_planned[i].y;
        cpu_items[i + 1u].width = STEP1_OBJECT_SIZE;
        cpu_items[i + 1u].height = STEP1_OBJECT_SIZE;
        cpu_items[i + 1u].color = step1_async_cpu_planned[i].color;
    }
    renderer_cpu_set_target(GPU_TARGET_Q0);
    step1_async_cpu_inflight_count = next_count;
    step1_async_cpu_seq_base = gpu_get_async_q0_seq();
    if (renderer_cpu_async_start(&cpu_q0_renderer, cpu_items,
                                 next_count + 1u) != 0) {
        step1_async_cpu_inflight_count = 0u;
        return -4;
    }
    step1_async_cpu_state = 1u;
    return 0;
}

static int step1_async_service(void)
{
    u32 status;
    u32 gpu_seq;
    int result;
    u32 i;
    gpu_perf_t perf;

    if (step1_async_cpu_state != 0u) {
        result = renderer_cpu_async_service(&cpu_q0_renderer);
        if (result < 0)
            return -20;
        if (result > 0) {
            if (gpu_get_async_q0_seq() == step1_async_cpu_seq_base)
                return -24;
            for (i = 0u; i < step1_async_cpu_inflight_count; i++)
                step1_async_cpu_shown[i] = step1_async_cpu_planned[i];
            step1_async_cpu_count = step1_async_cpu_inflight_count;
            step1_collision_present(&step1_cpu_collision);
            step1_async_cpu_inflight_count = 0u;
            step1_async_cpu_state = 0u;
            step1_async_cpu_frames++;
        }
    }

    if (step1_async_gpu_state != 0u) {
        status = gpu_get_status();
        gpu_seq = gpu_get_async_gpu_seq();
        if ((status & GPU_STATUS_ERROR) != 0u)
            return -21;
        /* Completion is keyed to the pre-submit sequence.  A BUSY sample is
         * not required because a short frame can finish between polls. */
        if ((gpu_seq != step1_async_gpu_seq_base) &&
            ((status & GPU_STATUS_BUSY) == 0u) &&
            ((status & GPU_STATUS_DONE) != 0u)) {
            result = renderer_wait_done(&demo_renderer, 1u);
            if (result != RENDERER_OK)
                return -22 - result;
            if (renderer_get_last_gpu_perf(&demo_renderer, &perf) !=
                RENDERER_OK)
                return -23;
            for (i = 0u; i < step1_async_gpu_inflight_count; i++)
                step1_async_gpu_shown[i] = step1_async_gpu_planned[i];
            step1_gpu_count = step1_async_gpu_inflight_count;
            step1_collision_present(&step1_gpu_collision);
            step1_async_gpu_inflight_count = 0u;
            step1_async_gpu_state = 0u;
            step1_async_gpu_frames++;
            gpu_test_frame_count = step1_async_gpu_frames;
            gpu_test_perf = perf;
            demo_panel_perf_generation++;
        }
    }

    /* Each path is independently eligible for its next submission. */
    if ((step1_async_freeze_new == 0u) && (step1_async_gpu_state == 0u)) {
        result = step1_async_start_gpu();
        if (result != 0)
            return result;
    }
    if ((step1_async_freeze_new == 0u) && (step1_async_cpu_state == 0u)) {
        result = step1_async_start_cpu();
        if (result != 0)
            return result;
    }
    demo_publish_renderer_stats();
    return 0;
}

static int step1_async_try_enable(void)
{
    gpu_result_t result;
    u32 control;
    u32 i;

    if ((step1_async_pending == 0u) || (step1_async_mode != 0u))
        return 0;
    if ((step1_seed_frames < 2u) ||
        demo_renderer.frame_open || demo_renderer.frame_submitted ||
        cpu_q0_renderer.frame_open || cpu_q0_renderer.frame_submitted)
        return 0;
    if (step1_async_switch_wait >= STEP1_ASYNC_SWITCH_LIMIT) {
        step1_async_switch_error = GPU_ERR_BUSY;
        step1_async_pending = 0u;
        return -1;
    }

    step1_async_switch_wait++;
    result = gpu_set_async_enable(1);
    control = gpu_get_async_control();
    if ((result == GPU_OK) && ((control & 1u) != 0u)) {
        for (i = 0u; i < step1_scene_count; i++) {
            step1_async_cpu_shown[i] = step1_shown[i];
            step1_async_gpu_shown[i] = step1_shown[i];
        }
        step1_async_cpu_count = step1_scene_count;
        step1_gpu_count = step1_scene_count;
        step1_cpu_collision = step1_serial_collision;
        step1_gpu_collision = step1_serial_collision;
        step1_async_cpu_state = 0u;
        step1_async_gpu_state = 0u;
        step1_async_cpu_inflight_count = 0u;
        step1_async_gpu_inflight_count = 0u;
        step1_async_pending = 0u;
        step1_async_mode = 1u;
        step1_async_switch_error = 0;
        demo_telemetry_stop_serial();
        demo_panel_reset_window();
        return 1;
    }

    step1_async_switch_error = (result == GPU_OK) ? GPU_ERR_BUSY : result;
    if (step1_async_switch_wait >= STEP1_ASYNC_SWITCH_LIMIT) {
        step1_async_pending = 0u;
        return -1;
    }
    return 0;
}
/* GPU submits first and parks in WAIT_SWAP, THEN the unchanged CPU writer
 * gets OWNER_READY and fills Q0. Never wait for GPU swap before CPU starts. */
static int demo_cpu_q0_cycle(void)
{
    render_item_t item;
    renderer_result_t result;
    u32 next_count;
    u32 expected_commands;
    u32 i;
    gpu_perf_t perf;
    u64 frame_start_time = clint_getTime(BSP_CLINT);
    u64 cpu_phase_start;
    u64 cpu_phase_end;
    u64 gpu_wait_start;
    u64 gpu_wait_end;

    renderer_cpu_set_target(GPU_TARGET_Q0);
    step1_plan_scene(&next_count);
    expected_commands = step1_command_budget(step1_gpu_count, next_count);
    if ((expected_commands > 64u) ||
        (next_count == 0u) || (next_count > STEP1_MAX_OBJECTS))
        return -12;

    result = renderer_begin_frame(&demo_renderer);
    if (result != RENDERER_OK) return -1;
    result = step1_submit_gpu_scene(step1_gpu_count, next_count);
    if (result != RENDERER_OK) return -2;
    gpu_test_frame_command_count = renderer_frame_item_count(&demo_renderer);
    step1_frame_commands = expected_commands;
    if (gpu_test_frame_command_count != expected_commands)
        return -9;
    result = renderer_submit_frame(&demo_renderer);
    if (result != RENDERER_OK) return -3;

    cpu_phase_start = clint_getTime(BSP_CLINT);
    result = renderer_begin_frame(&cpu_q0_renderer);
    if (result != RENDERER_OK) return -4;
    item.target_surface_id = GPU_TARGET_Q0;
    item.kind = RENDER_ITEM_RECT;
    item.x = 0u; item.y = 0u;
    item.width = CPU_Q0_WIDTH_PIXELS;
    item.height = CPU_Q0_HEIGHT_PIXELS;
    item.color = 0u; item.src_offset = 0u; item.src_stride = 0u;
    result = renderer_submit_item(&cpu_q0_renderer, &item);
    if (result != RENDERER_OK) return -5;
    item.width = STEP1_OBJECT_SIZE;
    item.height = STEP1_OBJECT_SIZE;
    item.color = 0xF800u;
    for (i = 0u; i < next_count; i++) {
        item.x = (u32)step1_planned[i].x;
        item.y = (u32)step1_planned[i].y;
        item.color = step1_planned[i].color;
        result = renderer_submit_item(&cpu_q0_renderer, &item);
        if (result != RENDERER_OK) return -5;
    }
    result = renderer_submit_frame(&cpu_q0_renderer);
    if (result != RENDERER_OK) return -6;
    cpu_phase_end = clint_getTime(BSP_CLINT);
    result = renderer_wait_done(&cpu_q0_renderer, GPU_DONE_POLL_LIMIT);
    if (result != RENDERER_OK) return -7;
    gpu_wait_start = clint_getTime(BSP_CLINT);
    result = renderer_wait_done(&demo_renderer, GPU_DONE_POLL_LIMIT);
    if (result != RENDERER_OK) return -8;
    gpu_wait_end = clint_getTime(BSP_CLINT);

    if (renderer_get_last_gpu_perf(&demo_renderer, &perf) != RENDERER_OK)
        return -10;
    gpu_test_perf = perf;
    demo_panel_perf_generation++;
    if (step1_seed_frames >= 2u && perf.blockcopy_total_cycles == 0u)
        return -11; /* No silent RECT-only success on the board. */
    if (step1_seed_frames < 2u) {
        ++step1_seed_frames; /* commit confirmed by BOTH unchanged wait paths */
    } else {
        ++step1_motion_frames;
    }
    for (i = 0u; i < next_count; i++)
        step1_shown[i] = step1_planned[i];
    step1_scene_count = next_count;
    step1_collision_present(&step1_serial_collision);
    if ((step1_seed_frames >= 2u) && (step1_motion_frames != 0u))
        step1_gpu_count = next_count;
    ++gpu_test_frame_count;
    demo_publish_renderer_stats();
    demo_telemetry_account_frame(
        clint_getTime(BSP_CLINT) - frame_start_time,
        (cpu_phase_end >= cpu_phase_start) ?
            (cpu_phase_end - cpu_phase_start) : 0u,
        (gpu_wait_end >= gpu_wait_start) ?
            (gpu_wait_end - gpu_wait_start) : 0u,
        (u64)perf.frame_cycles);
    return 0;
}

static int32_t demo_next_axis(int32_t old, int32_t *velocity,
                              int32_t limit)
{
    int32_t next = old + *velocity * (int32_t)demo_speed_level;

    if (next < 0) {
        next = 0;
        *velocity = -*velocity;
    } else if (next > limit) {
        next = limit;
        *velocity = -*velocity;
    }
    return next;
}

static int32_t demo_next_x_axis(int32_t old, int32_t *velocity,
                                int32_t *phase, int32_t limit)
{
    int32_t accumulated;
    int32_t delta;
    int32_t next;

    /* Accumulate the scaled velocity and emit only whole eight-pixel steps.
     * This keeps source offsets and BLIT destinations aligned even for the
     * +/-4 and +/-12 effective speeds selected by SPD. */
    accumulated = *phase + *velocity * (int32_t)demo_speed_level;
    if (accumulated >= 0)
        delta = (accumulated / (int32_t)DEMO_X_FAST) *
                (int32_t)DEMO_X_FAST;
    else
        delta = -(((-accumulated) / (int32_t)DEMO_X_FAST) *
                  (int32_t)DEMO_X_FAST);
    *phase = accumulated - delta;

    next = old + delta;
    if (next < 0) {
        next = 0;
        *velocity = -*velocity;
        *phase = 0u;
    } else if (next > limit) {
        next = limit;
        *velocity = -*velocity;
        *phase = 0u;
    }
    return next;
}

static int demo_aabb_overlap(const blit_object_t *a,
                             const blit_object_t *b)
{
    return (a->enable != 0u) && (b->enable != 0u) &&
           a->x < b->x + (int32_t)b->width &&
           a->x + (int32_t)a->width > b->x &&
           a->y < b->y + (int32_t)b->height &&
           a->y + (int32_t)a->height > b->y;
}

static int demo_same_quad_viewport(u32 a, u32 b)
{
    u32 va, vb, slot;
    if (demo_viewport_layout.mode != DEMO_VIEWPORT_MODE_QUAD)
        return 1;
    if (!quad_object_viewport(a, &va, &slot) ||
        !quad_object_viewport(b, &vb, &slot))
        return 0;
    return va == vb;
}

/* Predict a whole frame before issuing commands.  If two predicted AABBs
 * overlap, reverse both velocity vectors before the move.  If the reversed
 * move is still geometrically ambiguous, hold those objects at their known
 * non-overlapping Front positions for one frame; this prevents any overlap
 * from reaching the BLIT destination while preserving the reflected speed. */
static void demo_plan_next_frame(blit_object_t *next_objects,
                                 int32_t *next_x_phase)
{
    if (scope_requested) {
        u32 fixed_i;
        for (fixed_i=0u;fixed_i<demo_active_count;fixed_i++) {
            next_objects[fixed_i]=demo_objects[fixed_i];
            next_x_phase[fixed_i]=demo_x_phase[fixed_i];
        }
        return;
    }
    uint8_t blocked[DEMO_MAX_OBJECT_COUNT] = { 0u };
    u32 i;
    u32 pass;

    for (i = 0u; i < demo_active_count; i++) {
        u32 object_width = demo_objects[i].width;
        u32 object_height = demo_objects[i].height;
        next_objects[i] = demo_objects[i];
        next_x_phase[i] = demo_x_phase[i];
        {
            u32 surface_width = (demo_viewport_layout.mode ==
                                  DEMO_VIEWPORT_MODE_QUAD) ?
                                 DEMO_QUAD_WIDTH : GPU_SCREEN_WIDTH;
            u32 surface_height = (demo_viewport_layout.mode ==
                                   DEMO_VIEWPORT_MODE_QUAD) ?
                                  DEMO_QUAD_HEIGHT : GPU_SCREEN_HEIGHT;
            next_objects[i].x = demo_next_x_axis(
            demo_objects[i].x, &next_objects[i].vx, &next_x_phase[i],
            (int32_t)((object_width < surface_width) ?
                      (surface_width - object_width) : 0u));
            next_objects[i].y = demo_next_axis(
            demo_objects[i].y, &next_objects[i].vy,
            (int32_t)((object_height < surface_height) ?
                      (surface_height - object_height) : 0u));
        }
    }

    /* A blocked object is rendered at its old Front position until the
     * reflected velocity gives a non-overlapping next frame. */
    for (pass = 0u; pass < demo_active_count; pass++) {
        int collision_found = 0;

        for (i = 0u; i < demo_active_count; i++) {
            u32 j;
            for (j = i + 1u; j < demo_active_count; j++) {
                if ((step1_collision_requested == 0u) ||
                    !demo_same_quad_viewport(i, j) ||
                    !demo_aabb_overlap(&next_objects[i], &next_objects[j]))
                    continue;

                collision_found = 1;
                if (blocked[i] == 0u) {
                    next_objects[i].vx = -next_objects[i].vx;
                    next_objects[i].vy = -next_objects[i].vy;
                    blocked[i] = 1u;
                }
                if (blocked[j] == 0u) {
                    next_objects[j].vx = -next_objects[j].vx;
                    next_objects[j].vy = -next_objects[j].vy;
                    blocked[j] = 1u;
                }
                next_objects[i].x = demo_objects[i].x;
                next_objects[i].y = demo_objects[i].y;
                next_objects[j].x = demo_objects[j].x;
                next_objects[j].y = demo_objects[j].y;
            }
        }

        if (!collision_found)
            break;
    }

}

static void demo_request_display(demo_viewport_mode_t mode, u32 source)
{
    if ((mode == DEMO_VIEWPORT_MODE_FULL) && (source > GPU_FULL_SOURCE_CPU))
        return;
    /* A user MODE/FULL request may supersede an EXIT recovery request, but it
     * must never be allowed to complete that recovery implicitly.  Keep the
     * saved target for an explicit safe retry. */
    if ((demo_bg_recovery_state == BG_RESTORE_STATUS_RESTORING) &&
        (demo_bg_recovery_requesting == 0u) &&
        (((u32)mode != demo_bg_recovery_target_mode) ||
         ((mode == DEMO_VIEWPORT_MODE_FULL) &&
          (source != demo_bg_recovery_target_source)))) {
        demo_bg_recovery_state = BG_RESTORE_STATUS_FAILED;
        demo_bg_recovery_switch_active = 0u;
        demo_bg_recovery_frame_done = 0u;
    }
    demo_pending_mode = mode;
    demo_pending_source = source;
    demo_pending_valid = 1u;
    if ((demo_switch_state == DEMO_SWITCH_IDLE) &&
        ((mode != demo_confirmed_mode) ||
         ((mode == DEMO_VIEWPORT_MODE_FULL) &&
          (source != demo_confirmed_source)))) {
        demo_switch_wait = 0u;
        demo_switch_rendered = 0u;
        demo_switch_error = 0;
        step1_async_freeze_new = 1u;
    }
}

static int demo_switch_service(void)
{
    u32 status;
    gpu_result_t result;

    if (demo_switch_state != DEMO_SWITCH_PREPARE)
        demo_panel_wait_active = 0u;

    if ((demo_switch_state == DEMO_SWITCH_IDLE) && step1_async_off_pending &&
        (step1_async_mode != 0u)) {
        demo_switch_mode = demo_confirmed_mode;
        demo_switch_source = demo_confirmed_source;
        demo_switch_state = DEMO_SWITCH_DRAIN;
        demo_switch_wait = 0u;
        demo_switch_rendered = 0u;
        step1_async_freeze_new = 1u;
    }

    if ((demo_switch_state == DEMO_SWITCH_IDLE) && demo_pending_valid) {
        if (!demo_options_pending && (demo_pending_mode == demo_confirmed_mode) &&
            ((demo_pending_mode != DEMO_VIEWPORT_MODE_FULL) ||
             (demo_pending_source == demo_confirmed_source)) &&
            !((demo_bg_recovery_state == BG_RESTORE_STATUS_RESTORING) &&
              (demo_bg_recovery_force_frame != 0u) &&
              ((u32)demo_pending_mode == demo_bg_recovery_target_mode) &&
              ((demo_pending_mode != DEMO_VIEWPORT_MODE_FULL) ||
               (demo_pending_source == demo_bg_recovery_target_source)))) {
            /* Consume a duplicate request that is already confirmed. */
            demo_pending_valid = 0u;
        } else {
            if (demo_confirmed_mode == DEMO_VIEWPORT_MODE_QUAD)
                demo_async_resume_requested = (step1_async_mode != 0u) || (step1_async_pending != 0u);
            demo_switch_mode = demo_pending_mode;
            demo_switch_source = demo_pending_source;
            demo_pending_valid = 0u;
            demo_switch_state = (step1_async_mode != 0u ||
                                 step1_async_cpu_state != 0u ||
                                 step1_async_gpu_state != 0u) ?
                                DEMO_SWITCH_DRAIN : DEMO_SWITCH_PREPARE;
            demo_switch_wait = 0u;
            demo_switch_rendered = 0u;
            demo_switch_error = 0;
            demo_bg_recovery_switch_active =
                (demo_bg_recovery_state == BG_RESTORE_STATUS_RESTORING) &&
                ((u32)demo_switch_mode == demo_bg_recovery_target_mode) &&
                ((demo_switch_mode != DEMO_VIEWPORT_MODE_FULL) ||
                 (demo_switch_source == demo_bg_recovery_target_source));
            step1_async_freeze_new = 1u;
        }
    }
    switch (demo_switch_state) {
    case DEMO_SWITCH_DRAIN:
        if (++demo_switch_wait >= DEMO_SWITCH_DRAIN_LIMIT) {
            return demo_switch_fail(1u, GPU_ERR_TIMEOUT, -1);
        }
        if ((step1_async_cpu_state == 0u) &&
            (step1_async_gpu_state == 0u) &&
            !renderer_cpu_async_busy()) {
            demo_switch_state = DEMO_SWITCH_DISABLE_ASYNC;
            demo_switch_wait = 0u;
        }
        break;
    case DEMO_SWITCH_DISABLE_ASYNC:
        result = gpu_set_async_enable(0);
        if (result != GPU_OK) {
            return demo_switch_fail(2u, result, -2);
        }
        if ((gpu_get_async_control() & 1u) == 0u) {
            step1_async_mode = 0u;
            step1_async_off_pending = 0u;
            demo_switch_state = DEMO_SWITCH_PREPARE;
            demo_switch_wait = 0u;
        } else if (++demo_switch_wait >= STEP1_ASYNC_SWITCH_LIMIT) {
            return demo_switch_fail(3u, GPU_ERR_BUSY, -3);
        }
        break;
    case DEMO_SWITCH_PREPARE:
        if ((gpu_get_panel_status() & GPU_PANEL_STATUS_BUSY) != 0u) {
            u64 now = clint_getTime(BSP_CLINT);
            /* Mailbox ACK occurs at vsync, not after a CPU poll count.
             * 250 ms permits 15 frames at 60 Hz, but still bounds a stuck ACK. */
            if (!demo_panel_wait_active) {
                demo_panel_wait_start = now;
                demo_panel_wait_active = 1u;
            } else if (now - demo_panel_wait_start >= (u64)BSP_CLINT_HZ / 4u)
                return demo_switch_fail(7u, GPU_ERR_BUSY, -7);
            break;
        }
        demo_panel_wait_active = 0u;
        if (demo_options_pending) {
            result = gpu_set_demo_options(demo_options_requested);
            if (result == GPU_ERR_BUSY && ++demo_switch_wait < DEMO_SWITCH_DRAIN_LIMIT) break;
            if (result != GPU_OK) return demo_switch_fail(10u, result, -10);
            demo_options_pending = 0u;
            demo_switch_wait = 0u;
        }
        demo_full_source = demo_switch_source;
        /* Drain the old FULL frame before clamping to the bounded QUAD pool. */
        if (demo_switch_mode == DEMO_VIEWPORT_MODE_QUAD &&
            step1_pending_count > STEP1_MAX_OBJECTS) {
            step1_pending_count = STEP1_MAX_OBJECTS;
            step1_layout_epoch++;
        }
        demo_active_count = step1_pending_count;
        demo_init_objects();
        demo_viewport_configure(demo_switch_mode);
        if (scope_requested) demo_init_objects(); /* same FULL seed in every window */
        if (demo_switch_mode == DEMO_VIEWPORT_MODE_QUAD) {
            step1_seed_frames = 0u;
            step1_motion_frames = 0u;
            step1_scene_count = 1u;
            step1_gpu_count = 0u;
            step1_seed_object(&step1_shown[0], 0u);
            step1_planned[0] = step1_shown[0];
            step1_serial_collision.shown_enabled = 0u;
            step1_serial_collision.frame_enabled = 0u;
        }
        if (gpu_set_display(demo_switch_mode == DEMO_VIEWPORT_MODE_QUAD ?
                            GPU_DISPLAY_MODE_QUAD : GPU_DISPLAY_MODE_FULL,
                            demo_switch_source) != GPU_OK) {
            return demo_switch_fail(4u, GPU_ERR_PARAM, -4);
        }
        demo_reseed_pending = (demo_switch_mode == DEMO_VIEWPORT_MODE_FULL) ? 1u : 0u;
        full_blit_seed_pending = (demo_switch_source == GPU_FULL_SOURCE_BLIT) ? 1u : 0u;
        demo_panel_invalidate_full();
        demo_switch_state = DEMO_SWITCH_RUN;
        demo_switch_wait = 0u;
        demo_switch_rendered = 0u;
        break;
    case DEMO_SWITCH_RUN:
        if (++demo_switch_wait >= (STEP1_ASYNC_SWITCH_LIMIT * 8u)) {
            return demo_switch_fail(5u, GPU_ERR_TIMEOUT, -5);
        }
        if (demo_switch_rendered != 0u) {
            status = gpu_get_display_status();
            if (((demo_switch_mode == DEMO_VIEWPORT_MODE_QUAD) &&
                 ((status & 3u) == GPU_DISPLAY_MODE_QUAD)) ||
                ((demo_switch_mode == DEMO_VIEWPORT_MODE_FULL) &&
                 ((status & 3u) == GPU_DISPLAY_MODE_FULL) &&
                 (((status >> 2) & 3u) == demo_switch_source))) {
                if (demo_bg_recovery_switch_active != 0u)
                    demo_bg_recovery_frame_done = 1u;
                demo_confirmed_mode = demo_switch_mode;
                demo_confirmed_source = demo_switch_source;
                demo_switch_state = DEMO_SWITCH_IDLE;
                demo_switch_wait = 0u;
                step1_async_freeze_new = 0u;
                demo_panel_reset_window();
                if (demo_confirmed_mode == DEMO_VIEWPORT_MODE_FULL) {
                    demo_full_snapshot_valid = 0u;
                    demo_panel_publish_full_label();
                }
            }
        }
        break;
    case DEMO_SWITCH_ERROR:
        if (demo_pending_valid &&
            (step1_async_cpu_state == 0u) &&
            (step1_async_gpu_state == 0u) &&
            !renderer_cpu_async_busy() &&
            !demo_renderer.frame_open && !demo_renderer.frame_submitted &&
            !cpu_q0_renderer.frame_open && !cpu_q0_renderer.frame_submitted) {
            if (demo_confirmed_mode == DEMO_VIEWPORT_MODE_QUAD)
                demo_async_resume_requested = (step1_async_mode != 0u) || (step1_async_pending != 0u);
            demo_switch_mode = demo_pending_mode;
            demo_switch_source = demo_pending_source;
            demo_pending_valid = 0u;
            demo_switch_state = ((step1_async_mode != 0u) ||
                                 (step1_async_cpu_state != 0u) ||
                                 (step1_async_gpu_state != 0u)) ?
                                DEMO_SWITCH_DRAIN : DEMO_SWITCH_PREPARE;
            demo_switch_wait = 0u;
            demo_switch_rendered = 0u;
            demo_switch_error = 0;
            step1_async_switch_error = 0;
            step1_failed = 0u;
            demo_bg_recovery_switch_active =
                (demo_bg_recovery_state == BG_RESTORE_STATUS_RESTORING) &&
                ((u32)demo_switch_mode == demo_bg_recovery_target_mode) &&
                ((demo_switch_mode != DEMO_VIEWPORT_MODE_FULL) ||
                 (demo_switch_source == demo_bg_recovery_target_source));
            step1_async_freeze_new = 1u;
        } else {
            step1_async_freeze_new = 1u;
            step1_async_switch_error = demo_switch_error;
            return -6;
        }
        break;
    default:
        break;
    }
    return 0;
}

/* Background upload owns the serialized FULL CPU writer.  This hook advances
 * the existing safe mode switch one bounded step at a time; it never starts a
 * second async frame and temporarily disables the renderer's UART callback
 * while the one seed frame is written. */
static int demo_bg_prepare_full_cpu(void)
{
    if (demo_bg_saved_valid == 0u) {
        demo_bg_saved_mode = (u32)demo_confirmed_mode;
        demo_bg_saved_source = demo_confirmed_source;
        demo_bg_saved_valid = 1u;
    }
    return demo_prepare_full_cpu();
}

/* Common narrow transition, without any BG session state. */
static int demo_prepare_full_cpu(void)
{
    int result;
    u32 writer_status;
    int async_inflight;
    /* A synchronous renderer CPU begin has not set renderer->frame_open yet.
     * Its service callback may still receive/parse BG bytes, but resource
     * coordination must wait for that call to return to the main loop. */
    if (renderer_cpu_sync_waiting()) {
        step1_async_freeze_new = 1u;
        return 0;
    }
    writer_status = cpu_writer_status();
    async_inflight = (step1_async_mode != 0u) ||
                     (step1_async_cpu_state != 0u) ||
                     (step1_async_gpu_state != 0u) ||
                     (renderer_cpu_async_busy() != 0);
    /* demo_uart_poll is also the synchronous writer wait callback.  A BG
     * BEGIN received there is only a request; leave the old writer and its
     * owner untouched until the next main-loop service. */
    if (!async_inflight &&
        (demo_renderer.frame_open || demo_renderer.frame_submitted ||
         cpu_q0_renderer.frame_open || cpu_q0_renderer.frame_submitted ||
         ((writer_status & (CPU_WR_STATUS_FRAME_ACTIVE |
                            CPU_WR_STATUS_ROW_ACTIVE |
                            CPU_WR_STATUS_DDR_INFLIGHT)) != 0u))) {
        step1_async_freeze_new = 1u;
        return 0;
    }
    if ((demo_confirmed_mode == DEMO_VIEWPORT_MODE_FULL) &&
        (demo_confirmed_source == GPU_FULL_SOURCE_CPU) &&
        (demo_switch_state == DEMO_SWITCH_IDLE) &&
        /* RUN may have left a duplicate FULL/CPU request. Let the switch
         * service consume it before handing the display to BG. */
        !demo_pending_valid &&
        !renderer_cpu_async_busy() && !demo_renderer.frame_open &&
        !demo_renderer.frame_submitted && !cpu_q0_renderer.frame_open &&
        !cpu_q0_renderer.frame_submitted &&
        ((writer_status & (CPU_WR_STATUS_FRAME_ACTIVE |
                           CPU_WR_STATUS_ROW_ACTIVE |
                           CPU_WR_STATUS_DDR_INFLIGHT)) == 0u) &&
        ((writer_status & CPU_WR_STATUS_OWNER_READY) != 0u))
        return 1;

    demo_request_display(DEMO_VIEWPORT_MODE_FULL, GPU_FULL_SOURCE_CPU);
    if ((demo_switch_state == DEMO_SWITCH_DRAIN) &&
        ((step1_async_mode != 0u) || (step1_async_cpu_state != 0u) ||
         (step1_async_gpu_state != 0u))) {
        result = step1_async_service();
        if (result != 0)
            return -1;
    }
    result = demo_switch_service();
    if (result != 0)
        return -1;
    if ((demo_switch_state == DEMO_SWITCH_RUN) &&
        (demo_switch_rendered == 0u)) {
        writer_status = cpu_writer_status();
        /* This hook is also called from cpu_writer_wait().  Do not start the
         * seed writer until the callback's old writer has really released
         * the owner; the next main-loop service will retry this branch. */
        if (demo_renderer.frame_open || demo_renderer.frame_submitted ||
            cpu_q0_renderer.frame_open || cpu_q0_renderer.frame_submitted ||
            renderer_cpu_async_busy() ||
            ((writer_status & (CPU_WR_STATUS_FRAME_ACTIVE |
                               CPU_WR_STATUS_ROW_ACTIVE |
                               CPU_WR_STATUS_DDR_INFLIGHT)) != 0u) ||
            ((writer_status & CPU_WR_STATUS_OWNER_READY) == 0u))
            return 0;
        renderer_cpu_set_service(0);
        result = demo_full_cpu_cycle();
        renderer_cpu_set_service(demo_uart_poll);
        if (result != 0)
            return -1;
        demo_switch_rendered = 1u;
    }
    return 0;
}

static int demo_switch_fail(u32 stage, int error, int raw)
{
    (void)stage;
    demo_switch_error = error;
    demo_switch_state = DEMO_SWITCH_ERROR;
    return raw;
}

static int demo_bg_restore_pending(void)
{
    return demo_bg_saved_valid != 0u;
}

static int demo_bg_restore(void)
{
    if (demo_bg_saved_valid == 0u)
        return -1;
    if (demo_bg_recovery_state == BG_RESTORE_STATUS_RESTORING)
        return 1;
    /* A retry is registered only at the existing idle/error boundary.  An
     * unrelated pending MODE request keeps the failed recovery explicit. */
    if ((demo_bg_recovery_state == BG_RESTORE_STATUS_FAILED) &&
        (demo_pending_valid != 0u))
        return -1;
    /* An aborted PREPARING can leave an old switch in flight. Queue the saved
     * target; the main loop drains that switch before starting recovery. */
    demo_bg_recovery_target_mode = demo_bg_saved_mode;
    demo_bg_recovery_target_source = demo_bg_saved_source;
    demo_bg_recovery_state = BG_RESTORE_STATUS_RESTORING;
    demo_bg_recovery_force_frame = 1u;
    demo_bg_recovery_switch_active = 0u;
    demo_bg_recovery_frame_done = 0u;
    demo_bg_recovery_requesting = 1u;
    demo_request_display((demo_viewport_mode_t)demo_bg_recovery_target_mode,
                         demo_bg_recovery_target_source);
    demo_bg_recovery_requesting = 0u;
    return 0;
}

/* Called by bg_uart_service() from the main loop.  A switch becoming IDLE or
 * matching the saved target alone is insufficient: recovery must have run the
 * requested frame through the existing writer/renderer path first. */
static int demo_bg_restore_status(void)
{
    if (demo_bg_recovery_state == BG_RESTORE_STATUS_FAILED)
        return BG_RESTORE_STATUS_FAILED;
    if (demo_bg_recovery_state != BG_RESTORE_STATUS_RESTORING)
        return demo_bg_recovery_state == BG_RESTORE_STATUS_RESTORED ?
               BG_RESTORE_STATUS_RESTORED : BG_RESTORE_STATUS_NONE;
    if (demo_switch_state == DEMO_SWITCH_ERROR &&
        !(demo_pending_valid &&
          (u32)demo_pending_mode == demo_bg_recovery_target_mode &&
          (demo_pending_mode != DEMO_VIEWPORT_MODE_FULL ||
           demo_pending_source == demo_bg_recovery_target_source))) {
        demo_bg_recovery_state = BG_RESTORE_STATUS_FAILED;
        return BG_RESTORE_STATUS_FAILED;
    }
    if ((demo_bg_recovery_switch_active != 0u) &&
        (demo_bg_recovery_frame_done != 0u) &&
        (demo_switch_state == DEMO_SWITCH_IDLE) &&
        ((u32)demo_confirmed_mode == demo_bg_recovery_target_mode) &&
        ((demo_confirmed_mode != DEMO_VIEWPORT_MODE_FULL) ||
         (demo_confirmed_source == demo_bg_recovery_target_source))) {
        demo_bg_recovery_state = BG_RESTORE_STATUS_RESTORED;
        demo_bg_recovery_force_frame = 0u;
        demo_bg_recovery_switch_active = 0u;
        demo_bg_saved_valid = 0u;
        return BG_RESTORE_STATUS_RESTORED;
    }
    return BG_RESTORE_STATUS_RESTORING;
}

static int demo_full_cpu_cycle(void)
{
    blit_object_t next_objects[DEMO_MAX_OBJECT_COUNT];
    int32_t next_x_phase[DEMO_MAX_OBJECT_COUNT];
    render_item_t item;
    renderer_result_t result;
    u32 i;

    if (demo_active_count != step1_pending_count) {
        demo_active_count = step1_pending_count;
        demo_init_objects();
    }
    demo_plan_next_frame(next_objects, next_x_phase);
    renderer_cpu_set_target(GPU_TARGET_FULL_CPU);
    result = renderer_begin_frame(&cpu_q0_renderer);
    if (result != RENDERER_OK) return -1;
    item.kind = RENDER_ITEM_RECT;
    item.target_surface_id = GPU_TARGET_FULL_CPU;
    item.x = 0u; item.y = 0u;
    item.width = GPU_SCREEN_WIDTH; item.height = GPU_SCREEN_HEIGHT;
    item.color = 0u; item.src_offset = 0u; item.src_stride = 0u;
    result = renderer_submit_item(&cpu_q0_renderer, &item);
    if (result != RENDERER_OK) return -2;
    for (i = 0u; i < demo_active_count; i++) {
        item.x = (u32)next_objects[i].x;
        item.y = (u32)next_objects[i].y;
        item.width = next_objects[i].width;
        item.height = next_objects[i].height;
        item.color = next_objects[i].color;
        result = renderer_submit_item(&cpu_q0_renderer, &item);
        if (result != RENDERER_OK) return -3;
    }
    result = renderer_submit_frame(&cpu_q0_renderer);
    if (result != RENDERER_OK) return -4;
    result = renderer_wait_done(&cpu_q0_renderer, GPU_DONE_POLL_LIMIT);
    if (result != RENDERER_OK) return -5;
    for (i = 0u; i < demo_active_count; i++) {
        demo_objects[i] = next_objects[i];
        demo_x_phase[i] = next_x_phase[i];
    }
    gpu_test_frame_count++;
    demo_full_snapshot_valid = 1u;
    return 0;
}
static void demo_draw_active_rects(const char *stage)
{
    u32 i;

    if (demo_viewport_layout.mode == DEMO_VIEWPORT_MODE_QUAD) {
        renderer_result_t result;

        for (i = 0u; i < demo_quad_object_count(); i++) {
            gpu_test_object_index = i;
            result = quad_submit_object(&demo_renderer,
                                        demo_quad_viewports, i,
                                        demo_objects[i].x,
                                        demo_objects[i].y,
                                        demo_objects[i].width,
                                        demo_objects[i].height,
                                        demo_objects[i].color);
            gpu_test_push0_result = demo_gpu_result(result);
            if (gpu_test_push0_result != GPU_OK)
                demo_stop(stage, gpu_test_push0_result);
        }
        result = demo_submit_quad_monitor();
        if (result != RENDERER_OK)
            demo_stop("quad_monitor", demo_gpu_result(result));
        return;
    }

    for (i = 0u; i < demo_active_count; i++) {
        demo_viewport_rect_t rect;
        render_item_t item;
        renderer_result_t result;

        gpu_test_object_index = i;
        if (!demo_viewport_transform_blit(&demo_objects[i], &rect))
            demo_stop(stage, GPU_ERR_PARAM);
        item.kind = (demo_full_source == GPU_FULL_SOURCE_BLIT &&
                     full_blit_seed_pending == 0u) ?
                    RENDER_ITEM_BLIT : RENDER_ITEM_RECT;
        item.target_surface_id = demo_full_target_id();
        item.x = rect.dst_x;
        item.y = rect.dst_y;
        item.width = rect.dst_width;
        item.height = rect.dst_height;
        item.color = demo_objects[i].color;
        item.src_offset = (item.kind == RENDER_ITEM_BLIT) ?
                          demo_palette_offset(i) : 0u;
        item.src_stride = (item.kind == RENDER_ITEM_BLIT) ?
                          GPU_PALETTE_STRIDE : 0u;
        result = renderer_submit_item(&demo_renderer, &item);
        gpu_test_push0_result = demo_gpu_result(result);
        if (gpu_test_push0_result != GPU_OK)
            demo_stop(stage, gpu_test_push0_result);
    }
}

static int demo_full_fpga_cycle(void)
{
    blit_object_t next_objects[DEMO_MAX_OBJECT_COUNT];
    int32_t next_x_phase[DEMO_MAX_OBJECT_COUNT];
    u32 i;
    renderer_result_t result;

    if (demo_active_count != step1_pending_count) {
        demo_active_count = step1_pending_count;
        demo_init_objects();
        full_blit_seed_pending = 1u;
    }
    demo_begin_background();
    if (step1_failed) return -1;
    if (demo_full_source == GPU_FULL_SOURCE_BLIT && full_blit_seed_pending) {
        result = demo_seed_palette(GPU_TARGET_FULL_BLIT);
        if (result != RENDERER_OK) return -6;
    }
    if (demo_reseed_pending != 0u) {
        demo_draw_active_rects("full_reseed");
        demo_complete_frame(renderer_frame_item_count(&demo_renderer));
        demo_reseed_pending = 0u;
        full_blit_seed_pending = 0u;
        return step1_failed ? -2 : 0;
    }
    demo_plan_next_frame(next_objects, next_x_phase);
    for (i = 0u; i < demo_active_count; i++) {
        demo_viewport_rect_t rect;
        render_item_t item;
        if (!demo_viewport_transform_blit(&next_objects[i], &rect))
            return -3;
        gpu_test_object_index = i;
        gpu_test_old_x[i] = (u32)demo_objects[i].x;
        gpu_test_old_y[i] = (u32)demo_objects[i].y;
        gpu_test_new_x[i] = (u32)next_objects[i].x;
        gpu_test_new_y[i] = (u32)next_objects[i].y;
        item.kind = (demo_full_source == GPU_FULL_SOURCE_BLIT &&
                     full_blit_seed_pending == 0u) ?
                    RENDER_ITEM_BLIT : RENDER_ITEM_RECT;
        item.target_surface_id = demo_full_target_id();
        item.x = rect.dst_x; item.y = rect.dst_y;
        item.width = rect.dst_width; item.height = rect.dst_height;
        item.color = next_objects[i].color;
        item.src_offset = (item.kind == RENDER_ITEM_BLIT) ?
                          demo_palette_offset(i) : 0u;
        item.src_stride = (item.kind == RENDER_ITEM_BLIT) ?
                          GPU_PALETTE_STRIDE : 0u;
        result = renderer_submit_item(&demo_renderer, &item);
        if (result != RENDERER_OK) return -4;
    }
    demo_complete_frame(renderer_frame_item_count(&demo_renderer));
    if (step1_failed) return -5;
    for (i = 0u; i < demo_active_count; i++) {
        demo_objects[i] = next_objects[i];
        demo_x_phase[i] = next_x_phase[i];
    }
    full_blit_seed_pending = 0u;
    return 0;
}
void main(void)
{
    u32 i;

    bsp_init();
    if ((gpu_get_demo_options() & 0xFFFF0000u) != GPU_DEMO_OPTIONS_MAGIC) {
        uart_writeStr(BSP_UART_TERMINAL, "ERR BIT_ELF_ABI: load showcase bit first\r\n");
        return;
    }
    if (read_u32(SCOPE_REG(0x200u)) != SCOPE_ABI) {
        uart_writeStr(BSP_UART_TERMINAL, "ERR SCOPE_ABI: load paired scope bit first\r\n");
        return;
    }
    scope_disarm();
    bg_uart_init();
    bg_uart_set_hooks(demo_bg_prepare_full_cpu, demo_bg_restore);
    bg_uart_set_restore_status_hook(demo_bg_restore_status);
    bg_uart_set_restore_pending_hook(demo_bg_restore_pending);
    demo_telemetry_init();
    step1_active = 1u;
    renderer_init(&demo_renderer, RENDERER_BACKEND_FPGA);
    renderer_init(&cpu_q0_renderer, RENDERER_BACKEND_CPU);
    renderer_cpu_set_target(GPU_TARGET_Q0);
    renderer_cpu_set_service(demo_uart_poll);
    (void)gpu_set_display(GPU_DISPLAY_MODE_QUAD, demo_full_source);
    demo_init_objects();
    step1_seed_object(&step1_shown[0], 0u);
    step1_planned[0] = step1_shown[0];
    step1_scene_count = 1u;
    step1_gpu_count = 0u;
    step1_pending_count = 4u;
    step1_collision_requested = 1u;
    step1_async_mode = 0u;
    step1_async_pending = 0u;
    step1_async_switch_wait = 0u;
    step1_async_switch_error = 0;
    step1_async_gpu_state = 0u;
    step1_async_gpu_seq_base = 0u;
    step1_async_cpu_seq_base = 0u;
    step1_async_gpu_inflight_count = 0u;
    step1_async_cpu_inflight_count = 0u;
    step1_async_cpu_count = 0u;
    step1_async_cpu_state = 0u;
    step1_async_cpu_frames = 0u;
    step1_async_gpu_frames = 0u;
    step1_async_freeze_new = 0u;
    step1_async_off_pending = 0u;
    demo_switch_state = DEMO_SWITCH_IDLE;
    demo_confirmed_mode = DEMO_VIEWPORT_MODE_QUAD;
    demo_confirmed_source = demo_full_source;
    demo_pending_valid = 0u;
    demo_switch_wait = 0u;
    demo_switch_rendered = 0u;
    demo_switch_error = 0;
    demo_full_snapshot_valid = 0u;
    demo_async_resume_requested = 1u;
    demo_panel_perf_generation = 0u;
    demo_panel_window_armed = 0u;
    /* The v1.0 presentation boots into four software-defined viewports.
     * MODE FULL selects the original BLIT path when needed for comparison. */
    demo_viewport_configure(DEMO_VIEWPORT_MODE_QUAD);
    uart_writeStr(BSP_UART_TERMINAL,
                  "\r\nCPU_BLIT_RECT ASYNC QUAD AUTO_START; UART: STATUS\r\n"
                  "Q0 CPU / Q1 BLIT / Q2 RECT: multicolor, 4 objects per viewport.\r\n"
                  "NUM 1|2|4|8|16|20 (FULL also 32|64); ASYNC ON enables independent Q0/Q1-Q2 service.\r\n"
                  "COLLIDE default ON; ASYNC default ON. BURST ON|OFF; DBUF ON|OFF; FIFO always ON.\r\n");
    for (i = 0u; i < DEMO_MAX_OBJECT_COUNT; i++) {
        gpu_test_old_x[i] = (u32)demo_objects[i].x;
        gpu_test_old_y[i] = (u32)demo_objects[i].y;
        gpu_test_new_x[i] = (u32)demo_objects[i].x;
        gpu_test_new_y[i] = (u32)demo_objects[i].y;
    }

    /* CPU_Q0 mode deliberately keeps the GPU frame outstanding while the
     * writer fills Q0; this is the owner handoff required by the manager. */
    gpu_test_bootstrap_done = 1u;
    uart_writeStr(BSP_UART_TERMINAL, "BUILD: SCOPE_BLOCKCOPY_20261101\r\n");
    while (1) {
        int cycle_result;
        int async_result;

        demo_uart_poll();
        /* PREPARING owns the transition and the serialized FULL CPU writer.
         * A BEGIN may arrive from the synchronous writer callback; let that
         * already-open transaction return through its existing wait path, but
         * do not start the next ordinary scene frame while preparation is
         * waiting for the writer to become idle. */
        if (demo_scene_work_blocked())
            continue;
        demo_telemetry_service();
        if (((demo_switch_state == DEMO_SWITCH_DRAIN) ||
             (demo_switch_state == DEMO_SWITCH_DISABLE_ASYNC)) &&
            ((step1_async_mode != 0u) ||
             (step1_async_cpu_state != 0u) ||
             (step1_async_gpu_state != 0u))) {
            async_result = step1_async_service();
            if (async_result != 0) {
                (void)demo_switch_fail(8u, async_result, async_result);
                step1_async_switch_error = async_result;
                step1_failed = 1u;
            }
        }
        demo_panel_service();
        if (demo_switch_service() != 0) {
            step1_failed = 1u;
            continue;
        }
        if (step1_failed)
            continue;

        if ((demo_switch_state == DEMO_SWITCH_RUN) &&
            (demo_switch_rendered == 0u)) {
            if (demo_switch_mode == DEMO_VIEWPORT_MODE_QUAD)
                cycle_result = demo_cpu_q0_cycle();
            else if (demo_switch_source == GPU_FULL_SOURCE_CPU)
                cycle_result = demo_full_cpu_cycle();
            else
                cycle_result = demo_full_fpga_cycle();
            if (cycle_result != 0) {
                (void)demo_switch_fail(9u, cycle_result, cycle_result);
                step1_async_switch_error = cycle_result;
                step1_failed = 1u;
                continue;
            }
            demo_switch_rendered = 1u;
            continue;
        }
        if (demo_switch_state != DEMO_SWITCH_IDLE)
            continue;
        if (scope_service()) continue;

        if ((demo_confirmed_mode == DEMO_VIEWPORT_MODE_QUAD) &&
            (demo_async_resume_requested != 0u) && (step1_seed_frames >= 2u) &&
            (step1_async_mode == 0u) && (step1_async_pending == 0u)) {
            step1_async_pending = 1u;
            step1_async_switch_wait = 0u;
            demo_async_resume_requested = 0u;
        }
        if (step1_async_pending != 0u && step1_async_mode == 0u) {
            int switch_result = step1_async_try_enable();
            if (switch_result < 0) {
                step1_failed = 1u;
                continue;
            }
            if (step1_async_mode == 0u)
                continue;
        }
        if (step1_async_off_pending != 0u) {
            /* demo_switch_service() starts the drain on the next boundary. */
            continue;
        }
        if (step1_async_mode != 0u) {
            int async_result = step1_async_service();
            if (async_result != 0) {
                step1_async_switch_error = async_result;
                step1_failed = 1u;
            }
            continue;
        }

        if (demo_confirmed_mode == DEMO_VIEWPORT_MODE_FULL) {
            if (demo_confirmed_source == GPU_FULL_SOURCE_CPU)
                cycle_result = demo_full_cpu_cycle();
            else
                cycle_result = demo_full_fpga_cycle();
        } else {
            cycle_result = demo_cpu_q0_cycle();
        }
        if (cycle_result != 0) {
            step1_async_switch_error = cycle_result;
            step1_failed = 1u;
        }
    }
}

