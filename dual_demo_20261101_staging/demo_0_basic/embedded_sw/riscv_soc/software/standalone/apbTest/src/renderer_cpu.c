#include "renderer.h"
#include "cpu_writer.h"
#include "io.h"

#define CPU_RENDER_MAX_ITEMS 65u /* background + up to 64 FULL demo objects */

#ifndef HOST_TEST
static uint16_t cpu_row[CPU_FULL_WIDTH_PIXELS];
static uint32_t cpu_frame_index;
#endif
static uint8_t cpu_row_leased;
static cpu_writer_service_fn cpu_service;
static uint32_t cpu_target_id = GPU_TARGET_Q0;
static uint32_t cpu_width(void)
{ return cpu_target_id == GPU_TARGET_FULL_CPU ? CPU_FULL_WIDTH_PIXELS : CPU_Q0_WIDTH_PIXELS; }
static uint32_t cpu_height(void)
{ return cpu_target_id == GPU_TARGET_FULL_CPU ? CPU_FULL_HEIGHT_PIXELS : CPU_Q0_HEIGHT_PIXELS; }
static uint32_t cpu_row_words_local(void)
{ return cpu_target_id == GPU_TARGET_FULL_CPU ? CPU_FULL_ROW_WORDS : CPU_Q0_ROW_WORDS; }
static render_item_t cpu_items[CPU_RENDER_MAX_ITEMS];
static uint32_t cpu_item_count;
static uint32_t cpu_row_borrow_block;
static uint8_t cpu_sync_waiting;

typedef enum {
    CPU_ASYNC_IDLE = 0,
    CPU_ASYNC_WAIT_OWNER,
    CPU_ASYNC_WAIT_FRAME,
    CPU_ASYNC_WAIT_ROW,
    CPU_ASYNC_WRITE_DATA,
    CPU_ASYNC_WAIT_ROW_DONE,
    CPU_ASYNC_WAIT_FRAME_READY,
    CPU_ASYNC_WAIT_PRESENT,
    CPU_ASYNC_ERROR
} cpu_async_state_t;

static cpu_async_state_t cpu_async_state;
static uint32_t cpu_async_row;
static uint32_t cpu_async_word;
static uint32_t cpu_async_words;
static uint32_t cpu_async_watchdog;
static int cpu_async_error;


void renderer_cpu_set_target(uint32_t target_id)
{
    cpu_target_id = (target_id == GPU_TARGET_FULL_CPU) ?
                     GPU_TARGET_FULL_CPU : GPU_TARGET_Q0;
    cpu_writer_set_target(cpu_target_id);
}

void renderer_cpu_set_service(void (*service)(void))
{
    cpu_service = service;
}

int renderer_cpu_sync_waiting(void)
{
    return cpu_sync_waiting != 0u;
}

renderer_result_t renderer_cpu_begin_frame(renderer_t *renderer)
{
    if (renderer == 0)
        return RENDERER_ERR_PARAM;
    if (cpu_row_leased)
        return RENDERER_ERR_BUSY;
    cpu_item_count = 0u;
#ifndef HOST_TEST
    cpu_writer_set_target(cpu_target_id);
#endif
#ifdef HOST_TEST
    return RENDERER_OK;
#else
    {
        int result;
        uint8_t previous_waiting = cpu_sync_waiting;
        /* The renderer frame flags are set by renderer.c only after this
         * call returns.  Keep a software ownership marker across the whole
         * synchronous writer wait so a UART callback cannot start BG's
         * resource path in that window. */
        cpu_sync_waiting = 1u;
        result = cpu_writer_begin_frame(cpu_service);
        cpu_sync_waiting = previous_waiting;
        return result == 0 ? RENDERER_OK : RENDERER_ERR_HW;
    }
#endif
}

renderer_result_t renderer_cpu_submit_item(renderer_t *renderer,
                                           const render_item_t *item)
{
    if (item == 0)
        return RENDERER_ERR_PARAM;
    if (item->kind != RENDER_ITEM_RECT)
        return RENDERER_ERR_PARAM;
    if ((item->target_surface_id != cpu_target_id) ||
        (item->width == 0u) || (item->height == 0u) ||
        (item->width > cpu_width()) || (item->height > cpu_height()) ||
        (item->x > cpu_width() - item->width) ||
        (item->y > cpu_height() - item->height))
        return RENDERER_ERR_PARAM;
    if (cpu_item_count >= CPU_RENDER_MAX_ITEMS)
        return RENDERER_ERR_FIFO_FULL;
    cpu_items[cpu_item_count++] = *item;
    (void)renderer;
    return RENDERER_OK;
}

#ifndef HOST_TEST
static uint16_t cpu_background_pixel(uint32_t x, uint32_t y)
{
    (void)x;
    (void)y;
    return 0x0000u;
}

static void cpu_overlay_items(uint32_t y)
{
    uint32_t i;
    uint32_t x;

    for (i = 0u; i < cpu_item_count; i++) {
        const render_item_t *item = &cpu_items[i];
        /* The full-surface item selects the background pattern. */
        if ((item->width == cpu_width()) &&
            (item->height == cpu_height()))
            continue;
        if ((y < item->y) || (y >= item->y + item->height))
            continue;
        for (x = item->x; x < item->x + item->width; x++)
            cpu_row[x] = item->color;
    }
}
#endif

#ifndef HOST_TEST
static void cpu_async_prepare_row(uint32_t y)
{
    uint32_t x;
    for (x = 0u; x < cpu_width(); x++)
        cpu_row[x] = cpu_background_pixel(x, y);
    cpu_overlay_items(y);
}
#endif

int renderer_cpu_async_busy(void)
{
#ifdef HOST_TEST
    return 0;
#else
    return cpu_async_state != CPU_ASYNC_IDLE &&
           cpu_async_state != CPU_ASYNC_ERROR;
#endif
}

int renderer_cpu_row_borrow(uint16_t **row)
{
#ifdef HOST_TEST
    cpu_row_borrow_block = (row == 0) ? RENDERER_CPU_ROW_BORROW_PARAM :
                                       RENDERER_CPU_ROW_BORROW_HOST;
    return -1;
#else
    cpu_row_borrow_block = 0u;
    if (row == 0)
        cpu_row_borrow_block |= RENDERER_CPU_ROW_BORROW_PARAM;
    if (cpu_row_leased)
        cpu_row_borrow_block |= RENDERER_CPU_ROW_BORROW_LEASED;
    if (renderer_cpu_async_busy())
        cpu_row_borrow_block |= RENDERER_CPU_ROW_BORROW_ASYNC;
    if (cpu_item_count != 0u)
        cpu_row_borrow_block |= RENDERER_CPU_ROW_BORROW_ITEMS;
    if (cpu_row_borrow_block != 0u)
        return -1;
    cpu_row_leased = 1u;
    *row = cpu_row;
    return 0;
#endif
}

uint32_t renderer_cpu_row_borrow_block(void)
{
    return cpu_row_borrow_block;
}

void renderer_cpu_row_release(void)
{
#ifndef HOST_TEST
    cpu_row_leased = 0u;
#endif
}

int renderer_cpu_async_start(renderer_t *renderer,
                             const render_item_t *items, uint32_t count)
{
#ifdef HOST_TEST
    (void)renderer; (void)items; (void)count;
    return -9;
#else
    uint32_t i;
    uint32_t target_id;
    if ((renderer == 0) || (items == 0) ||
        (count == 0u) || (count > CPU_RENDER_MAX_ITEMS) ||
        renderer_cpu_async_busy())
        return -3;
    cpu_item_count = count;
    target_id = items[0].target_surface_id;
    if ((target_id != GPU_TARGET_Q0) && (target_id != GPU_TARGET_FULL_CPU)) {
        cpu_async_error = -3;
        cpu_async_state = CPU_ASYNC_ERROR;
        return cpu_async_error;
    }
    cpu_target_id = target_id;
    cpu_writer_set_target(cpu_target_id);
    for (i = 0u; i < count; i++) {
        if ((items[i].kind != RENDER_ITEM_RECT) ||
            (items[i].target_surface_id != target_id) ||
            (items[i].width == 0u) || (items[i].height == 0u) ||
            (items[i].width > cpu_width()) || (items[i].height > cpu_height()) ||
            (items[i].x > cpu_width() - items[i].width) ||
            (items[i].y > cpu_height() - items[i].height)) {
            cpu_async_error = -3;
            cpu_async_state = CPU_ASYNC_ERROR;
            return cpu_async_error;
        }
        cpu_items[i] = items[i];
    }
    renderer->frame_open = 1u;
    renderer->frame_submitted = 0u;
    renderer->frame_item_count = count;
    renderer->stats.frames_started++;
    renderer->stats.current_frame_items = count;
    write_u32(cpu_target_id, CPU_WR_TARGET);
    write_u32(CPU_WR_CLEAR_STATUS, CPU_WR_CONTROL);
    cpu_async_row = 0u;
    cpu_async_word = 0u;
    cpu_async_words = cpu_row_words_local();
    cpu_async_watchdog = 0u;
    cpu_async_error = 0;
    cpu_async_state = CPU_ASYNC_WAIT_OWNER;
    return 0;
#endif
}

int renderer_cpu_async_service(renderer_t *renderer)
{
#ifdef HOST_TEST
    (void)renderer;
    return -9;
#else
    uint32_t status;
    uint32_t budget;

    if ((renderer == 0) || (cpu_async_state == CPU_ASYNC_IDLE))
        return 0;
    if (cpu_async_state == CPU_ASYNC_ERROR)
        return cpu_async_error != 0 ? cpu_async_error : -2;
    if (++cpu_async_watchdog > 500000000u) {
        cpu_async_error = -1;
        cpu_async_state = CPU_ASYNC_ERROR;
        write_u32(CPU_WR_ABORT, CPU_WR_CONTROL);
        return cpu_async_error;
    }
    status = cpu_writer_status();
    if ((status & CPU_WR_STATUS_ERROR) != 0u) {
        cpu_async_error = -2;
        cpu_async_state = CPU_ASYNC_ERROR;
        return cpu_async_error;
    }
    switch (cpu_async_state) {
    case CPU_ASYNC_WAIT_OWNER:
        if ((status & CPU_WR_STATUS_OWNER_READY) != 0u) {
            write_u32(CPU_WR_BEGIN_FRAME, CPU_WR_CONTROL);
            cpu_async_state = CPU_ASYNC_WAIT_FRAME;
        }
        break;
    case CPU_ASYNC_WAIT_FRAME:
        if ((status & CPU_WR_STATUS_FRAME_ACTIVE) != 0u) {
            cpu_async_prepare_row(0u);
            write_u32(0u, CPU_WR_XY);
            write_u32(cpu_row_words_local(), CPU_WR_ROW_WORDS);
            write_u32(CPU_WR_BEGIN_ROW, CPU_WR_CONTROL);
            cpu_async_word = 0u;
            cpu_async_state = CPU_ASYNC_WAIT_ROW;
        }
        break;
    case CPU_ASYNC_WAIT_ROW:
        if ((status & CPU_WR_STATUS_DATA_READY) != 0u)
            cpu_async_state = CPU_ASYNC_WRITE_DATA;
        break;
    case CPU_ASYNC_WRITE_DATA:
        budget = 32u; /* 64 pixels, then return to GPU/UART service. */
        while ((budget != 0u) && (cpu_async_word < cpu_async_words)) {
            status = cpu_writer_status();
            if ((status & CPU_WR_STATUS_DATA_READY) == 0u)
                break;
            write_u32((uint32_t)cpu_row[2u * cpu_async_word] |
                      ((uint32_t)cpu_row[2u * cpu_async_word + 1u] << 16),
                      CPU_WR_DATA);
            cpu_async_word++;
            budget--;
        }
        if (cpu_async_word == cpu_async_words) {
            write_u32(CPU_WR_END_ROW, CPU_WR_CONTROL);
            cpu_async_state = CPU_ASYNC_WAIT_ROW_DONE;
        }
        break;
    case CPU_ASYNC_WAIT_ROW_DONE:
        if ((status & CPU_WR_STATUS_ROW_ACTIVE) == 0u) {
            cpu_async_row++;
            if (cpu_async_row >= cpu_height()) {
                write_u32(CPU_WR_END_FRAME, CPU_WR_CONTROL);
                cpu_async_state = CPU_ASYNC_WAIT_FRAME_READY;
            } else {
                cpu_async_prepare_row(cpu_async_row);
                write_u32(cpu_async_row << 16, CPU_WR_XY);
                write_u32(cpu_row_words_local(), CPU_WR_ROW_WORDS);
                write_u32(CPU_WR_BEGIN_ROW, CPU_WR_CONTROL);
                cpu_async_word = 0u;
                cpu_async_state = CPU_ASYNC_WAIT_ROW;
            }
        }
        break;
    case CPU_ASYNC_WAIT_FRAME_READY:
        if ((status & CPU_WR_STATUS_FRAME_READY) != 0u)
            cpu_async_state = CPU_ASYNC_WAIT_PRESENT;
        break;
    case CPU_ASYNC_WAIT_PRESENT:
        if ((status & CPU_WR_STATUS_SWAP_DONE) != 0u) {
            renderer->frame_open = 0u;
            renderer->frame_submitted = 0u;
            renderer->stats.frames_submitted++;
            renderer->stats.frames_completed++;
            renderer->stats.last_frame_items = renderer->frame_item_count;
            renderer->stats.current_frame_items = 0u;
            renderer->stats.last_result = RENDERER_OK;
            renderer->frame_item_count = 0u;
            cpu_item_count = 0u;
            cpu_async_state = CPU_ASYNC_IDLE;
            return 1;
        }
        break;
    default:
        break;
    }
    return 0;
#endif
}

renderer_result_t renderer_cpu_submit_frame(renderer_t *renderer)
{
    uint32_t x, y;
    uint32_t object_items = 0u;
    if (renderer == 0)
        return RENDERER_ERR_PARAM;
#ifdef HOST_TEST
    (void)x; (void)y;
    return RENDERER_OK;
#else
    for (x = 0u; x < cpu_item_count; x++) {
        if ((cpu_items[x].width != cpu_width()) ||
            (cpu_items[x].height != cpu_height()))
            object_items++;
    }
    for (y = 0; y < cpu_height(); y++) {
        for (x = 0; x < cpu_width(); x++)
            cpu_row[x] = cpu_background_pixel(x, y);
        if (object_items != 0u)
            cpu_overlay_items(y);
        else {
            uint32_t square_x =
                (cpu_frame_index * 8u) % (cpu_width() - 64u);
            uint32_t square_y = 220u;
            for (x = square_x; x < square_x + 64u; x++) {
                if (y >= square_y && y < square_y + 64u)
                    cpu_row[x] = 0xF800u;
            }
        }
        if (cpu_writer_write_row(0u, y, cpu_row, cpu_width(),
                                 cpu_service) != 0)
            return RENDERER_ERR_HW;
    }
    cpu_frame_index++;
    return RENDERER_OK;
#endif
}

renderer_result_t renderer_cpu_wait_done(renderer_t *renderer,
                                         uint32_t poll_limit)
{
    (void)renderer;
    (void)poll_limit;
#ifdef HOST_TEST
    return RENDERER_OK;
#else
    int result;
    result = cpu_writer_end_frame(cpu_service);
    if (result == 0)
        cpu_item_count = 0u;
    return result == 0 ? RENDERER_OK : RENDERER_ERR_HW;
#endif
}

renderer_result_t renderer_cpu_get_last_gpu_perf(const renderer_t *renderer,
                                                 gpu_perf_t *perf)
{
    (void)renderer;
    (void)perf;
    return RENDERER_ERR_UNSUPPORTED;
}
