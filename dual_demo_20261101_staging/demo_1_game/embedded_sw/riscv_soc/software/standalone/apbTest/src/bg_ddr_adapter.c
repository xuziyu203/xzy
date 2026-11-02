#ifndef BUILD_VARIANT_GAME /* GHD3: full UART build only (single ELF: game_noserial.c stubs) */
#include "bg_ddr_adapter.h"
#include "renderer.h"
#include "cpu_writer.h"
#include "framebuffer_q8.h"
#include "gpu.h"
#include "io.h"
#include "bsp.h"
#include "bg_uart.h"

#define BG_DDR_WIDTH 1920u
#define BG_DDR_HEIGHT 1080u
#define BG_DDR_ROW_BYTES 3840u
#define BG_DDR_BLOCK_BYTES 128u
#define BG_DDR_BLOCKS_PER_ROW 30u
#define BG_DDR_FRAME_BYTES 4147200u

typedef enum { BG_DDR_IDLE, BG_DDR_LOADING, BG_DDR_STORED,
               BG_DDR_SHOWN, BG_DDR_ERROR } bg_ddr_state_t;

static bg_ddr_state_t bg_ddr_state;
static uint16_t *bg_ddr_row_buffer;
static uint32_t bg_ddr_row_index;
static uint32_t bg_ddr_block_index;
static uint32_t bg_ddr_generation;
static int bg_diag_begin_rc;
static int bg_diag_begin_state;
static int bg_diag_writer_error;
static uint32_t bg_diag_writer_status;
static uint32_t bg_diag_borrow_block;
static uint8_t bg_diag_writer_error_valid;
static uint8_t bg_diag_writer_status_valid;
static uint8_t bg_diag_borrow_valid;
static int bg_diag_show_state;
static int bg_diag_show_rc;
static int bg_diag_show_writer_error;
static uint32_t bg_diag_show_writer_status;
static uint32_t bg_diag_show_writer_stage;
static uint32_t bg_diag_show_gen_saved;
static uint32_t bg_diag_show_gen_now;
static uint32_t bg_diag_show_display;
static uint8_t bg_diag_show_valid;
static uint8_t bg_diag_show_end_issued;
static uint8_t bg_diag_show_writer_error_valid;
static uint8_t bg_diag_show_writer_status_valid;
static uint8_t bg_diag_show_gen_saved_valid;
static uint8_t bg_diag_show_gen_now_valid;
static uint8_t bg_diag_show_display_valid;

static void bg_ddr_diag_reset(void)
{
    bg_diag_begin_rc = 0;
    bg_diag_begin_state = (int)bg_ddr_state;
    bg_diag_writer_error = 0;
    bg_diag_writer_status = 0u;
    bg_diag_borrow_block = 0u;
    bg_diag_writer_error_valid = 0u;
    bg_diag_writer_status_valid = 0u;
    bg_diag_borrow_valid = 0u;
}

/* Called only at boot and after the UART accepts a new legal BEGIN. */
void bg_ddr_adapter_diag_show_reset(void)
{
    bg_diag_show_state = (int)bg_ddr_state;
    bg_diag_show_rc = 0;
    bg_diag_show_writer_error = 0;
    bg_diag_show_writer_status = 0u;
    bg_diag_show_writer_stage = CPU_WR_END_STAGE_NONE;
    bg_diag_show_gen_saved = 0u;
    bg_diag_show_gen_now = 0u;
    bg_diag_show_display = 0u;
    bg_diag_show_valid = 0u;
    bg_diag_show_end_issued = 0u;
    bg_diag_show_writer_error_valid = 0u;
    bg_diag_show_writer_status_valid = 0u;
    bg_diag_show_gen_saved_valid = 0u;
    bg_diag_show_gen_now_valid = 0u;
    bg_diag_show_display_valid = 0u;
}

static int bg_ddr_wait_idle(void)
{
    u64 start = clint_getTime(BSP_CLINT);
    for (;;) {
        uint32_t status = cpu_writer_status();
        if ((status & (CPU_WR_STATUS_FRAME_ACTIVE |
                       CPU_WR_STATUS_ROW_ACTIVE |
                       CPU_WR_STATUS_DDR_INFLIGHT)) == 0u)
            return 0;
        if ((clint_getTime(BSP_CLINT) - start) >=
            (u64)CPU_WR_TIMEOUT_WRITE_CYCLES)
            return -1;
    }
}

static void bg_ddr_release_row(void)
{
    renderer_cpu_row_release();
    bg_ddr_row_buffer = 0;
}

/* The writer wait callback deliberately does not call the UART poller.  It
 * only samples status; UART remains owned by the outer main-loop reader and
 * cannot recurse through a row write. */
static void bg_ddr_writer_service(void)
{
    bg_uart_service_during_write();
    (void)cpu_writer_status();
}

/* END_FRAME can complete before the scanout reader accepts the new
 * descriptor. Keep the single END_FRAME submission, but wait for the
 * corresponding generation edge and the requested display state. */
static int bg_ddr_wait_display_confirm(uint32_t *generation_out,
                                       uint32_t *status_out)
{
    u64 start = clint_getTime(BSP_CLINT);
    uint32_t generation = bg_ddr_generation;
    uint32_t status = 0u;

    for (;;) {
        status = gpu_get_display_status();
        generation = gpu_get_display_generation();
        if (generation != bg_ddr_generation &&
            (status & 3u) == GPU_DISPLAY_MODE_FULL &&
            ((status >> 2) & 3u) == GPU_FULL_SOURCE_CPU) {
            *generation_out = generation;
            *status_out = status;
            return 0;
        }
        if ((clint_getTime(BSP_CLINT) - start) >=
            (u64)CPU_WR_TIMEOUT_SWAP_CYCLES) {
            *generation_out = generation;
            *status_out = status;
            return -1;
        }
        bg_ddr_writer_service();
    }
}

void bg_ddr_adapter_init(void)
{
    bg_ddr_state = BG_DDR_IDLE;
    bg_ddr_row_buffer = 0;
    bg_ddr_row_index = 0u;
    bg_ddr_block_index = 0u;
    bg_ddr_generation = 0u;
    bg_ddr_diag_reset();
    bg_ddr_adapter_diag_show_reset();
}

int bg_ddr_adapter_begin(uint32_t length, uint32_t crc)
{
    int writer_result;

    (void)crc;
    bg_ddr_diag_reset();
    if ((length != BG_DDR_FRAME_BYTES) || (bg_ddr_state != BG_DDR_IDLE)) {
        bg_diag_begin_rc = -1;
        return -1;
    }
    if (renderer_cpu_row_borrow(&bg_ddr_row_buffer) != 0) {
        bg_diag_borrow_block = renderer_cpu_row_borrow_block();
        bg_diag_borrow_valid = 1u;
        bg_diag_begin_rc = -2;
        return -2;
    }
    renderer_cpu_set_target(GPU_TARGET_FULL_CPU);
    cpu_writer_set_target(CPU_FULL_TARGET);
    bg_ddr_generation = gpu_get_display_generation();
    writer_result = cpu_writer_begin_frame(bg_ddr_writer_service);
    if (writer_result != 0) {
        bg_diag_writer_error = cpu_writer_last_error();
        bg_diag_writer_status = cpu_writer_status();
        bg_diag_writer_error_valid = 1u;
        bg_diag_writer_status_valid = 1u;
        bg_ddr_release_row();
        bg_ddr_state = BG_DDR_ERROR;
        bg_diag_begin_rc = -3;
        return -3;
    }
    if (bg_uart_take_cancel()) {
        write_u32(CPU_WR_ABORT, CPU_WR_CONTROL);
        (void)bg_ddr_wait_idle();
        bg_ddr_release_row();
        bg_ddr_state = BG_DDR_IDLE;
        bg_diag_begin_rc = -4;
        return -4;
    }
    bg_ddr_row_index = 0u;
    bg_ddr_block_index = 0u;
    bg_ddr_state = BG_DDR_LOADING;
    bg_diag_begin_rc = 0;
    return 0;
}

int bg_ddr_adapter_accept_block(const uint8_t *data)
{
    uint32_t i;
    if ((data == 0) || (bg_ddr_state != BG_DDR_LOADING) ||
        (bg_ddr_row_index >= BG_DDR_HEIGHT) ||
        (bg_ddr_block_index >= BG_DDR_BLOCKS_PER_ROW))
        return -1;
    for (i = 0u; i < BG_DDR_BLOCK_BYTES; i += 2u)
        bg_ddr_row_buffer[bg_ddr_block_index * (BG_DDR_BLOCK_BYTES / 2u) +
                          i / 2u] = (uint16_t)data[i] |
                                    ((uint16_t)data[i + 1u] << 8);
    bg_ddr_block_index++;
    if (bg_ddr_block_index == BG_DDR_BLOCKS_PER_ROW) {
        /* Wire CRC remains over the full RGB565 row; convert only after all
         * packets have arrived. Forward in-place writes never clobber a future source pixel. */
        for(uint32_t x=0;x<1920u;x++)((uint8_t*)bg_ddr_row_buffer)[x]=framebuffer_q8_encode(bg_ddr_row_buffer[x],x,bg_ddr_row_index);
        if (cpu_writer_write_row(0u, bg_ddr_row_index, (const uint8_t*)bg_ddr_row_buffer,
                                 BG_DDR_WIDTH, bg_ddr_writer_service) != 0) {
            bg_ddr_state = BG_DDR_ERROR;
            return -2;
        }
        if (bg_uart_take_cancel()) {
            bg_ddr_state = BG_DDR_ERROR;
            return -3;
        }
        bg_ddr_row_index++;
        bg_ddr_block_index = 0u;
    }
    return 0;
}

int bg_ddr_adapter_finish(uint32_t received, uint32_t crc)
{
    (void)crc;
    if ((bg_ddr_state != BG_DDR_LOADING) ||
        (received != BG_DDR_FRAME_BYTES) ||
        (bg_ddr_row_index != BG_DDR_HEIGHT) || (bg_ddr_block_index != 0u))
        return -1;
    bg_ddr_state = BG_DDR_STORED;
    return 0;
}

int bg_ddr_adapter_show(void)
{
    uint32_t status;
    uint32_t generation;
    int result;
    int first_show = (bg_diag_show_valid == 0u);

    if (first_show)
        bg_diag_show_state = (int)bg_ddr_state;
    if (bg_ddr_state == BG_DDR_SHOWN) {
        result = 1;
        goto recorded;
    }
    if (bg_ddr_state != BG_DDR_STORED) {
        result = -1;
        goto recorded;
    }
    /* Take the transaction baseline immediately before the sole END_FRAME
     * write. The upload-time value is not a submission identity. */
    bg_ddr_generation = gpu_get_display_generation();
    if (first_show) {
        bg_diag_show_gen_saved = bg_ddr_generation;
        bg_diag_show_gen_saved_valid = 1u;
    }
    result = cpu_writer_end_frame(bg_ddr_writer_service);
    /* end_frame unconditionally writes END_FRAME before either wait. */
    if (first_show) bg_diag_show_end_issued = 1u;
    if (result != 0) {
        if (first_show) {
            bg_diag_show_writer_error = cpu_writer_last_error();
            bg_diag_show_writer_status = cpu_writer_last_status();
            bg_diag_show_writer_stage = cpu_writer_last_end_stage();
            bg_diag_show_writer_error_valid = 1u;
            bg_diag_show_writer_status_valid = 1u;
        }
        bg_ddr_state = BG_DDR_ERROR;
        result = -2;
        goto recorded;
    }
    result = bg_ddr_wait_display_confirm(&generation, &status);
    if (first_show) {
        bg_diag_show_gen_now = generation;
        bg_diag_show_display = status;
        bg_diag_show_gen_now_valid = 1u;
        bg_diag_show_display_valid = 1u;
    }
    if (result != 0) {
        bg_ddr_state = BG_DDR_ERROR;
        result = -3;
        goto recorded;
    }
    bg_ddr_state = BG_DDR_SHOWN;
    result = 0;
recorded:
    if (first_show) {
        bg_diag_show_rc = result;
        bg_diag_show_valid = 1u;
    }
    return result;
}

int bg_ddr_adapter_abort(void)
{
    if ((bg_ddr_state == BG_DDR_LOADING) ||
        (bg_ddr_state == BG_DDR_STORED) ||
        (bg_ddr_state == BG_DDR_ERROR)) {
        write_u32(CPU_WR_ABORT, CPU_WR_CONTROL);
        if (bg_ddr_wait_idle() != 0) {
            bg_ddr_state = BG_DDR_ERROR;
            return -1;
        }
        bg_ddr_release_row();
        bg_ddr_state = BG_DDR_IDLE;
        return 0;
    }
    return 0;
}

int bg_ddr_adapter_exit(void)
{
    if (bg_ddr_state == BG_DDR_LOADING || bg_ddr_state == BG_DDR_STORED)
        return -1;
    if (bg_ddr_state == BG_DDR_ERROR) {
        if (bg_ddr_wait_idle() != 0)
            return -2;
        bg_ddr_release_row();
        bg_ddr_state = BG_DDR_IDLE;
        return 0;
    }
    if (bg_ddr_state == BG_DDR_SHOWN || bg_ddr_state == BG_DDR_IDLE) {
        bg_ddr_release_row();
        bg_ddr_state = BG_DDR_IDLE;
        return 0;
    }
    return -3;
}

int bg_ddr_adapter_active(void)
{
    return bg_ddr_state == BG_DDR_LOADING || bg_ddr_state == BG_DDR_STORED ||
           bg_ddr_state == BG_DDR_SHOWN || bg_ddr_state == BG_DDR_ERROR;
}

int bg_ddr_adapter_stored(void) { return bg_ddr_state == BG_DDR_STORED; }
int bg_ddr_adapter_shown(void) { return bg_ddr_state == BG_DDR_SHOWN; }
int bg_ddr_adapter_error(void) { return bg_ddr_state == BG_DDR_ERROR; }
uint32_t bg_ddr_adapter_row(void) { return bg_ddr_row_index; }
int bg_ddr_adapter_diag_begin_rc(void) { return bg_diag_begin_rc; }
int bg_ddr_adapter_diag_begin_state(void) { return bg_diag_begin_state; }
int bg_ddr_adapter_diag_writer_error_valid(void)
{ return bg_diag_writer_error_valid != 0u; }
int bg_ddr_adapter_diag_writer_error(void) { return bg_diag_writer_error; }
int bg_ddr_adapter_diag_writer_status_valid(void)
{ return bg_diag_writer_status_valid != 0u; }
uint32_t bg_ddr_adapter_diag_writer_status(void) { return bg_diag_writer_status; }
int bg_ddr_adapter_diag_borrow_valid(void)
{ return bg_diag_borrow_valid != 0u; }
uint32_t bg_ddr_adapter_diag_borrow_block(void) { return bg_diag_borrow_block; }
int bg_ddr_adapter_diag_show_valid(void) { return bg_diag_show_valid != 0u; }
int bg_ddr_adapter_diag_show_state(void) { return bg_diag_show_state; }
int bg_ddr_adapter_diag_show_rc(void) { return bg_diag_show_rc; }
int bg_ddr_adapter_diag_show_end_issued(void) { return bg_diag_show_end_issued != 0u; }
int bg_ddr_adapter_diag_show_writer_error_valid(void)
{ return bg_diag_show_writer_error_valid != 0u; }
int bg_ddr_adapter_diag_show_writer_error(void) { return bg_diag_show_writer_error; }
int bg_ddr_adapter_diag_show_writer_status_valid(void)
{ return bg_diag_show_writer_status_valid != 0u; }
uint32_t bg_ddr_adapter_diag_show_writer_status(void)
{ return bg_diag_show_writer_status; }
uint32_t bg_ddr_adapter_diag_show_writer_stage(void)
{ return bg_diag_show_writer_stage; }
int bg_ddr_adapter_diag_show_gen_saved_valid(void)
{ return bg_diag_show_gen_saved_valid != 0u; }
uint32_t bg_ddr_adapter_diag_show_gen_saved(void) { return bg_diag_show_gen_saved; }
int bg_ddr_adapter_diag_show_gen_now_valid(void)
{ return bg_diag_show_gen_now_valid != 0u; }
uint32_t bg_ddr_adapter_diag_show_gen_now(void) { return bg_diag_show_gen_now; }
int bg_ddr_adapter_diag_show_display_valid(void)
{ return bg_diag_show_display_valid != 0u; }
uint32_t bg_ddr_adapter_diag_show_display(void) { return bg_diag_show_display; }
#endif
