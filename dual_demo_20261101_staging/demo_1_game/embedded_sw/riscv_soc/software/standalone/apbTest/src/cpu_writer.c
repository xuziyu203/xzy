#include "cpu_writer.h"
#include "io.h"
#include "framebuffer_q8.h"
#include "bsp.h"

static int cpu_writer_error;
static uint64_t cpu_writer_owner_wait;
static uint64_t cpu_writer_row_wait;
static uint64_t cpu_writer_present_wait;
static uint32_t cpu_writer_target_id = 3u;
static uint32_t cpu_writer_end_stage;
static uint32_t cpu_writer_last_status_value;

static uint32_t cpu_writer_width_value(void)
{
    return cpu_writer_target_id == CPU_FULL_TARGET ?
           CPU_FULL_WIDTH_PIXELS : CPU_Q0_WIDTH_PIXELS;
}

static uint32_t cpu_writer_height_value(void)
{
    return cpu_writer_target_id == CPU_FULL_TARGET ?
           CPU_FULL_HEIGHT_PIXELS : CPU_Q0_HEIGHT_PIXELS;
}

static uint32_t cpu_writer_row_words_value(void)
{
    return cpu_writer_target_id == CPU_FULL_TARGET ?
           CPU_FULL_ROW_WORDS : CPU_Q0_ROW_WORDS;
}

static int cpu_writer_wait(uint32_t mask, uint32_t value, uint64_t timeout,
                           cpu_writer_service_fn service)
{
    uint64_t start = clint_getTime(BSP_CLINT);
    for (;;) {
        uint32_t status = read_u32(CPU_WR_STATUS);
        cpu_writer_last_status_value = status;
        if ((status & CPU_WR_STATUS_ERROR) != 0u) {
            cpu_writer_error = -2;
            return cpu_writer_error;
        }
        if ((status & mask) == value)
            return 0;
        if ((clint_getTime(BSP_CLINT) - start) >= timeout) {
            cpu_writer_error = -1;
            return cpu_writer_error;
        }
        if (service != 0)
            service();
    }
}

uint32_t cpu_writer_status(void) { return read_u32(CPU_WR_STATUS); }
uint32_t cpu_writer_last_end_stage(void) { return cpu_writer_end_stage; }
uint32_t cpu_writer_last_status(void) { return cpu_writer_last_status_value; }
uint32_t cpu_writer_word_count(void) { return read_u32(CPU_WR_WORD_COUNT); }
int cpu_writer_last_error(void) { return cpu_writer_error; }
void cpu_writer_set_target(uint32_t target_id)
{
    cpu_writer_target_id = (target_id == CPU_FULL_TARGET) ?
                           CPU_FULL_TARGET : 3u;
}
uint32_t cpu_writer_target(void) { return cpu_writer_target_id; }
uint32_t cpu_writer_width(void) { return cpu_writer_width_value(); }
uint32_t cpu_writer_height(void) { return cpu_writer_height_value(); }
uint32_t cpu_writer_row_words(void) { return cpu_writer_row_words_value(); }
uint64_t cpu_writer_owner_wait_cycles(void) { return cpu_writer_owner_wait; }
uint64_t cpu_writer_row_wait_cycles(void) { return cpu_writer_row_wait; }
uint64_t cpu_writer_present_wait_cycles(void) { return cpu_writer_present_wait; }

int cpu_writer_begin_frame(cpu_writer_service_fn service)
{
    uint64_t wait_start;
    uint64_t wait_end;
    if(!framebuffer_q8_supported())return -4;
    cpu_writer_error = 0;
    cpu_writer_owner_wait = 0u;
    cpu_writer_row_wait = 0u;
    cpu_writer_present_wait = 0u;
    write_u32(cpu_writer_target_id, CPU_WR_TARGET);
    write_u32(CPU_WR_CLEAR_STATUS, CPU_WR_CONTROL);
    /* The shared DDR writer owner must be idle before CPU acquires it.
     * OWNER_READY is level based and remains valid until BEGIN_FRAME is accepted. */
    wait_start = clint_getTime(BSP_CLINT);
    if (cpu_writer_wait(CPU_WR_STATUS_OWNER_READY,
                        CPU_WR_STATUS_OWNER_READY,
                        CPU_WR_TIMEOUT_WRITE_CYCLES, service) != 0)
        return cpu_writer_error;
    wait_end = clint_getTime(BSP_CLINT);
    cpu_writer_owner_wait = (wait_end >= wait_start) ?
                            (wait_end - wait_start) : 0u;
    write_u32(CPU_WR_BEGIN_FRAME, CPU_WR_CONTROL);
    return cpu_writer_wait(CPU_WR_STATUS_FRAME_ACTIVE,
                           CPU_WR_STATUS_FRAME_ACTIVE,
                           CPU_WR_TIMEOUT_WRITE_CYCLES, service);
}

int cpu_writer_write_row(uint32_t x, uint32_t y, const uint8_t *pixels,
                         uint32_t pixel_count, cpu_writer_service_fn service)
{
    uint32_t i;
    uint32_t words;
    uint64_t wait_start;
    uint64_t wait_end;
    if ((pixels == 0) || ((x & 15u) != 0u) || ((pixel_count & 15u) != 0u) ||
        (pixel_count == 0u) || (pixel_count > cpu_writer_width_value()) ||
        (x + pixel_count > cpu_writer_width_value()) ||
        (y >= cpu_writer_height_value()))
        return -3;
    words = pixel_count / 4u;
    write_u32((x & 0xFFFu) | ((y & 0xFFFu) << 16), CPU_WR_XY);
    write_u32(words, CPU_WR_ROW_WORDS);
    write_u32(CPU_WR_BEGIN_ROW, CPU_WR_CONTROL);
    wait_start = clint_getTime(BSP_CLINT);
    if (cpu_writer_wait(CPU_WR_STATUS_DATA_READY,
                        CPU_WR_STATUS_DATA_READY,
                        CPU_WR_TIMEOUT_WRITE_CYCLES, service) != 0)
        return -1;
    wait_end = clint_getTime(BSP_CLINT);
    if (wait_end >= wait_start)
        cpu_writer_row_wait += wait_end - wait_start;
    for (i = 0; i < words; i++) {
        write_u32((uint32_t)pixels[4u * i] | ((uint32_t)pixels[4u*i+1u]<<8) |
                  ((uint32_t)pixels[4u*i+2u]<<16) | ((uint32_t)pixels[4u*i+3u]<<24), CPU_WR_DATA);
        if ((i & 63u) == 63u && service != 0)
            service();
    }
    write_u32(CPU_WR_END_ROW, CPU_WR_CONTROL);
    wait_start = clint_getTime(BSP_CLINT);
    {
        int result = cpu_writer_wait(CPU_WR_STATUS_ROW_ACTIVE, 0u,
                                     CPU_WR_TIMEOUT_WRITE_CYCLES, service);
        wait_end = clint_getTime(BSP_CLINT);
        if (wait_end >= wait_start)
            cpu_writer_row_wait += wait_end - wait_start;
        return result;
    }
}

int cpu_writer_end_frame(cpu_writer_service_fn service)
{
    uint64_t wait_start;
    uint64_t wait_end;
    cpu_writer_end_stage = CPU_WR_END_STAGE_FRAME_READY;
    cpu_writer_last_status_value = 0u;
    write_u32(CPU_WR_END_FRAME, CPU_WR_CONTROL);
    wait_start = clint_getTime(BSP_CLINT);
    if (cpu_writer_wait(CPU_WR_STATUS_FRAME_READY,
                        CPU_WR_STATUS_FRAME_READY,
                        CPU_WR_TIMEOUT_WRITE_CYCLES, service) != 0)
        return cpu_writer_error;
    wait_end = clint_getTime(BSP_CLINT);
    if (wait_end >= wait_start)
        cpu_writer_present_wait += wait_end - wait_start;
    wait_start = clint_getTime(BSP_CLINT);
    cpu_writer_end_stage = CPU_WR_END_STAGE_SWAP_DONE;
    {
        int result = cpu_writer_wait(CPU_WR_STATUS_SWAP_DONE,
                                     CPU_WR_STATUS_SWAP_DONE,
                                     CPU_WR_TIMEOUT_SWAP_CYCLES, service);
        wait_end = clint_getTime(BSP_CLINT);
        if (wait_end >= wait_start)
            cpu_writer_present_wait += wait_end - wait_start;
        if (result == 0)
            cpu_writer_end_stage = CPU_WR_END_STAGE_DONE;
        return result;
    }
}
