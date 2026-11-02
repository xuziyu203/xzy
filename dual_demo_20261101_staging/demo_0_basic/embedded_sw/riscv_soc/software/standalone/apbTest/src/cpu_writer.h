#ifndef SRC_CPU_WRITER_H_
#define SRC_CPU_WRITER_H_

#include <stdint.h>
#include "soc.h"

typedef void (*cpu_writer_service_fn)(void);

#define CPU_WR_TARGET_OFFSET       0x070u
#define CPU_WR_XY_OFFSET           0x074u
#define CPU_WR_ROW_WORDS_OFFSET    0x078u
#define CPU_WR_DATA_OFFSET         0x07Cu
#define CPU_WR_CONTROL_OFFSET      0x080u
#define CPU_WR_STATUS_OFFSET       0x084u
#define CPU_WR_WORD_COUNT_OFFSET   0x088u

#define CPU_WR_TARGET              (IO_APB_SLAVE_0_INPUT + CPU_WR_TARGET_OFFSET)
#define CPU_WR_XY                  (IO_APB_SLAVE_0_INPUT + CPU_WR_XY_OFFSET)
#define CPU_WR_ROW_WORDS           (IO_APB_SLAVE_0_INPUT + CPU_WR_ROW_WORDS_OFFSET)
#define CPU_WR_DATA                (IO_APB_SLAVE_0_INPUT + CPU_WR_DATA_OFFSET)
#define CPU_WR_CONTROL             (IO_APB_SLAVE_0_INPUT + CPU_WR_CONTROL_OFFSET)
#define CPU_WR_STATUS              (IO_APB_SLAVE_0_INPUT + CPU_WR_STATUS_OFFSET)
#define CPU_WR_WORD_COUNT          (IO_APB_SLAVE_0_INPUT + CPU_WR_WORD_COUNT_OFFSET)

#define CPU_WR_STATUS_FRAME_ACTIVE (1u << 0)
#define CPU_WR_STATUS_ROW_ACTIVE   (1u << 1)
#define CPU_WR_STATUS_DATA_READY   (1u << 2)
#define CPU_WR_STATUS_DDR_INFLIGHT (1u << 3)
#define CPU_WR_STATUS_WRITE_DONE   (1u << 4)
#define CPU_WR_STATUS_SWAP_DONE    (1u << 5)
#define CPU_WR_STATUS_ERROR        (1u << 6)
#define CPU_WR_STATUS_FRAME_READY  (1u << 7)
#define CPU_WR_STATUS_OWNER_READY  (1u << 25)
#define CPU_WR_STATUS_WAIT_OWNER   (1u << 26)
#define CPU_WR_STATUS_ROW_GRANTED  (1u << 27)

#define CPU_Q0_WIDTH_PIXELS        960u
#define CPU_Q0_HEIGHT_PIXELS       540u
#define CPU_Q0_STRIDE_BYTES        1920u
#define CPU_Q0_ROW_WORDS           (CPU_Q0_WIDTH_PIXELS / 2u)
#define CPU_Q0_ROW_BEATS           (CPU_Q0_ROW_WORDS / 4u)

#define CPU_FULL_TARGET             2u
#define CPU_FULL_WIDTH_PIXELS       1920u
#define CPU_FULL_HEIGHT_PIXELS      1080u
#define CPU_FULL_STRIDE_BYTES       3840u
#define CPU_FULL_ROW_WORDS          (CPU_FULL_WIDTH_PIXELS / 2u)
#define CPU_FULL_ROW_BEATS          (CPU_FULL_ROW_WORDS / 4u)

#define CPU_WR_BEGIN_FRAME  0x01u
#define CPU_WR_BEGIN_ROW    0x02u
#define CPU_WR_END_ROW      0x04u
#define CPU_WR_END_FRAME    0x08u
#define CPU_WR_CLEAR_STATUS 0x10u
#define CPU_WR_ABORT        0x20u

#define CPU_WR_TIMEOUT_WRITE_CYCLES (5ull * 100000000ull)
#define CPU_WR_TIMEOUT_SWAP_CYCLES  (500ull * 100000000ull / 1000ull)

#define CPU_WR_END_STAGE_NONE        0u
#define CPU_WR_END_STAGE_FRAME_READY 1u
#define CPU_WR_END_STAGE_SWAP_DONE   2u
#define CPU_WR_END_STAGE_DONE        3u

uint32_t cpu_writer_status(void);
uint32_t cpu_writer_word_count(void);
void cpu_writer_set_target(uint32_t target_id);
uint32_t cpu_writer_target(void);
uint32_t cpu_writer_width(void);
uint32_t cpu_writer_height(void);
uint32_t cpu_writer_row_words(void);
int cpu_writer_begin_frame(cpu_writer_service_fn service);
int cpu_writer_write_row(uint32_t x, uint32_t y, const uint16_t *pixels,
                         uint32_t pixel_count, cpu_writer_service_fn service);
int cpu_writer_end_frame(cpu_writer_service_fn service);
int cpu_writer_last_error(void);
/* Cached diagnostic values only: no additional MMIO reads. The adapter
 * copies these immediately after end_frame fails, before another writer. */
uint32_t cpu_writer_last_end_stage(void);
uint32_t cpu_writer_last_status(void);
uint64_t cpu_writer_owner_wait_cycles(void);
uint64_t cpu_writer_row_wait_cycles(void);
uint64_t cpu_writer_present_wait_cycles(void);

#endif
