#ifndef APBTEST_BG_DDR_ADAPTER_H
#define APBTEST_BG_DDR_ADAPTER_H

#include <stdint.h>

void bg_ddr_adapter_init(void);
int bg_ddr_adapter_begin(uint32_t length, uint32_t crc);
int bg_ddr_adapter_accept_block(const uint8_t *data);
int bg_ddr_adapter_finish(uint32_t received, uint32_t crc);
int bg_ddr_adapter_show(void);
int bg_ddr_adapter_abort(void);
int bg_ddr_adapter_exit(void);
int bg_ddr_adapter_active(void);
int bg_ddr_adapter_stored(void);
int bg_ddr_adapter_shown(void);
uint32_t bg_ddr_adapter_row(void);
int bg_ddr_adapter_diag_begin_rc(void);
int bg_ddr_adapter_diag_begin_state(void);
int bg_ddr_adapter_diag_writer_error_valid(void);
int bg_ddr_adapter_diag_writer_error(void);
int bg_ddr_adapter_diag_writer_status_valid(void);
uint32_t bg_ddr_adapter_diag_writer_status(void);
int bg_ddr_adapter_diag_borrow_valid(void);
uint32_t bg_ddr_adapter_diag_borrow_block(void);
int bg_ddr_adapter_error(void);
/* First adapter SHOW result per accepted UART BEGIN. Recovery/query calls
 * retain it. Invalid/unreached fields are exposed with separate valid bits. */
void bg_ddr_adapter_diag_show_reset(void);
int bg_ddr_adapter_diag_show_valid(void);
int bg_ddr_adapter_diag_show_state(void);
int bg_ddr_adapter_diag_show_rc(void);
int bg_ddr_adapter_diag_show_end_issued(void);
int bg_ddr_adapter_diag_show_writer_error_valid(void);
int bg_ddr_adapter_diag_show_writer_error(void);
int bg_ddr_adapter_diag_show_writer_status_valid(void);
uint32_t bg_ddr_adapter_diag_show_writer_status(void);
uint32_t bg_ddr_adapter_diag_show_writer_stage(void);
int bg_ddr_adapter_diag_show_gen_saved_valid(void);
uint32_t bg_ddr_adapter_diag_show_gen_saved(void);
int bg_ddr_adapter_diag_show_gen_now_valid(void);
uint32_t bg_ddr_adapter_diag_show_gen_now(void);
int bg_ddr_adapter_diag_show_display_valid(void);
uint32_t bg_ddr_adapter_diag_show_display(void);

#endif
