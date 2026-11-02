/////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2013-2023 Efinix Inc. All rights reserved.
//
// Description:
// Example top file for EfxSapphireSoc
//
// Language:  Verilog 2001
//
// ------------------------------------------------------------------------------
// REVISION:
//  $Snapshot: $
//  $Id:$
//
// History:
// 1.0 Initial Release. 
/////////////////////////////////////////////////////////////////////////////////
`timescale 1ns / 1ps

module top_soc (
input [3:0] system_gpio_0_io_read,
output [3:0] system_gpio_0_io_write,
output [3:0] system_gpio_0_io_writeEnable,
input		jtag_inst1_TCK,
input		jtag_inst1_TDI,
output		jtag_inst1_TDO,
input		jtag_inst1_SEL,
input		jtag_inst1_CAPTURE,
input		jtag_inst1_SHIFT,
input		jtag_inst1_UPDATE,
input		jtag_inst1_RESET,
output		system_uart_0_io_txd,
input		system_uart_0_io_rxd,

output      memoryCheckerPass,
output      systemClk_rstn,
input       systemClk_locked,
output      baseClk_pll_rstn,
input       io_systemClk,
input       io_asyncResetn,

output      pipeline_start_req,
input       pipeline_busy,
input       pipeline_done,
input       pipeline_error,
input [7:0] pipeline_error_code,
input [3:0] pipeline_error_opcode,
input [3:0] pipeline_error_flags,
output [7:0] pipeline_frame_command_count,
input [31:0] perf_blockcopy_cycles,
input [31:0] perf_blockcopy_total_cycles,
input [31:0] perf_rect_last_cycles,
input [31:0] perf_rect_total_cycles,
input [31:0] perf_frame_cycles,
input [31:0] perf_rect_count,
// TEMP_BG_PERF: observation inputs only.
input [31:0] bg_dbg_cycles,
input [31:0] bg_dbg_w_fire,
input [31:0] bg_dbg_wait_fifo_cycles,
input [31:0] bg_dbg_aw_stall,
input [31:0] bg_dbg_w_stall,
input [31:0] bg_dbg_b_wait,
input [31:0] bg_dbg_burst_count,
input [31:0] bg_dbg_status,
output      cmd_push_pulse,
output [127:0] cmd_push_data,
output [2:0] cmd_push_target_id,
output [2:0] target_context,
output [1:0] display_mode_req,
output [1:0] full_source_req,
input       fifo_full,
input       fifo_empty,
input [7:0] fifo_level,
// Fixed asset writer APB/MMIO sideband.  These are deliberately explicit
// full-width signals so APB address truncation cannot alias legacy registers.
output [11:0] asset_reg_addr,
output        asset_reg_write,
output [31:0] asset_reg_wdata,
input  [31:0] asset_reg_rdata,
input         asset_owner_active,
input [1:0] active_display_mode,
input [1:0] active_full_source,
input [6:0] surface_front_select,
input [31:0] surface_generation,
input        cpu_active,
input [31:0] cpu_status,
input [31:0] cpu_word_count,
output       cpu_target_write,
output [31:0] cpu_target_value,
output       cpu_xy_write,
output [31:0] cpu_xy_value,
output       cpu_row_words_write,
output [31:0] cpu_row_words_value,
output       cpu_data_write,
output [31:0] cpu_data_value,
output       cpu_control_write,
output [5:0]  cpu_control_value
,
output       async_enable_request,
output       async_mode_reject_pulse,
input        async_enable_accept,
input [31:0] async_q0_present_seq,
input [31:0] async_gpu_pair_present_seq,
input [31:0] async_gpu_render_cycles,
input [31:0] async_gpu_owner_wait_cycles,
input [31:0] async_event_status,
output      panel_commit_pulse,
output [31:0] panel_cpu_row,
output [31:0] panel_blit_row,
output [31:0] panel_rect_row,
output [31:0] panel_blit_time,
output [31:0] panel_rect_time,
output [31:0] panel_flags,
output [31:0] panel_error,
input       panel_commit_busy,
input demo_options_accept,
output demo_single_beat,
output demo_single_buffer,
output      diag_arm_pulse,
output      diag_clear_pulse,
input [31:0] diag_reg0, input [31:0] diag_reg1, input [31:0] diag_reg2,
input [31:0] diag_reg3, input [31:0] diag_reg4, input [31:0] diag_reg5,
input [31:0] diag_reg6, input [31:0] diag_reg7, input [31:0] diag_reg8,
input [31:0] diag_reg9, input [31:0] diag_reg10, input [31:0] diag_reg11,
input [31:0] diag_reg12, input [31:0] diag_reg13, input [31:0] diag_reg14,
input [31:0] diag_reg15, input [31:0] diag_reg16, input [31:0] diag_reg17,
input [31:0] diag_reg18, input [31:0] diag_reg19, input [31:0] diag_reg20
);
/////////////////////////////////////////////////////////////////////////////
//Reset and PLL
wire 		reset;
wire        w_io_asyncReset;
wire        synchronized_gpio_asyncResetn;
wire		io_systemReset;
wire 	    io_memoryReset;				
wire [1:0]  io_ddrA_b_payload_resp=2'b00;
wire [11:0] io_apbSlave_0_PADDR;
wire		io_apbSlave_0_PSEL;
wire		io_apbSlave_0_PENABLE;
wire		io_apbSlave_0_PREADY;
wire		io_apbSlave_0_PWRITE;
wire [31:0] io_apbSlave_0_PWDATA;
wire [31:0] io_apbSlave_0_PRDATA;
wire		io_apbSlave_0_PSLVERROR;


/////////////////////////////////////////////////////////////////////////////
//Reset and PLL
assign reset 	= ~(synchronized_gpio_asyncResetn & systemClk_locked);
assign systemClk_rstn 	= 1'b1;
assign baseClk_pll_rstn = 1'b1;
assign w_io_asyncReset = reset ;


/////////////////////////////////////////////////////////////////////////////
reset_synchronizer #(
.NUM_STAGE(5),
.ACTIVE_LOW(1)
) asyncResetn_synchronizer (
.reset_in(io_asyncResetn),
.reset_out(synchronized_gpio_asyncResetn),
.clk(io_systemClk)
);

apb_test_reg apb_test_reg_inst (
.clk       (io_systemClk),
.resetn    (~io_systemReset),
.PADDR     (io_apbSlave_0_PADDR),
.PSEL      (io_apbSlave_0_PSEL),
.PENABLE   (io_apbSlave_0_PENABLE),
.PWRITE    (io_apbSlave_0_PWRITE),
.PWDATA    (io_apbSlave_0_PWDATA),
.PRDATA    (io_apbSlave_0_PRDATA),
.PREADY    (io_apbSlave_0_PREADY),
.PSLVERROR (io_apbSlave_0_PSLVERROR),
.pipeline_start_req  (pipeline_start_req),
.pipeline_busy       (pipeline_busy),
.pipeline_done_pulse (pipeline_done),
.pipeline_error      (pipeline_error),
.pipeline_error_code (pipeline_error_code),
.pipeline_error_opcode(pipeline_error_opcode),
.pipeline_error_flags(pipeline_error_flags),
.pipeline_frame_command_count (pipeline_frame_command_count),
.perf_blockcopy_cycles (perf_blockcopy_cycles),
.perf_blockcopy_total_cycles (perf_blockcopy_total_cycles),
.perf_rect_last_cycles (perf_rect_last_cycles),
.perf_rect_total_cycles (perf_rect_total_cycles),
.perf_frame_cycles (perf_frame_cycles),
.perf_rect_count (perf_rect_count),
.bg_dbg_cycles (bg_dbg_cycles),
    .bg_dbg_w_fire (bg_dbg_w_fire),
    .bg_dbg_wait_fifo_cycles (bg_dbg_wait_fifo_cycles),
    .bg_dbg_aw_stall (bg_dbg_aw_stall),
    .bg_dbg_w_stall (bg_dbg_w_stall),
    .bg_dbg_b_wait (bg_dbg_b_wait),
    .bg_dbg_burst_count (bg_dbg_burst_count),
    .bg_dbg_status (bg_dbg_status),
.cmd_push_pulse         (cmd_push_pulse),
.cmd_push_data          (cmd_push_data),
.cmd_push_target_id     (cmd_push_target_id),
.target_context         (target_context),
.display_mode_req       (display_mode_req),
.full_source_req        (full_source_req),
.active_display_mode    (active_display_mode),
.active_full_source     (active_full_source),
.surface_front_select   (surface_front_select),
.surface_generation     (surface_generation),
.cpu_active             (cpu_active),
.cpu_status             (cpu_status),
.cpu_word_count         (cpu_word_count),
.cpu_target_write       (cpu_target_write),
.cpu_target_value       (cpu_target_value),
.cpu_xy_write           (cpu_xy_write),
.cpu_xy_value           (cpu_xy_value),
.cpu_row_words_write    (cpu_row_words_write),
.cpu_row_words_value    (cpu_row_words_value),
.cpu_data_write         (cpu_data_write),
.cpu_data_value         (cpu_data_value),
.cpu_control_write      (cpu_control_write),
.cpu_control_value      (cpu_control_value),
.async_enable_request   (async_enable_request),
.async_mode_reject_pulse(async_mode_reject_pulse),
.async_enable_accept    (async_enable_accept),
.async_q0_present_seq   (async_q0_present_seq),
.async_gpu_pair_present_seq(async_gpu_pair_present_seq),
.async_gpu_render_cycles(async_gpu_render_cycles),
.async_gpu_owner_wait_cycles(async_gpu_owner_wait_cycles),
.async_event_status     (async_event_status),
.panel_commit_pulse     (panel_commit_pulse),
.panel_cpu_row          (panel_cpu_row),
.panel_blit_row         (panel_blit_row),
.panel_rect_row         (panel_rect_row),
.panel_blit_time        (panel_blit_time),
.panel_rect_time       (panel_rect_time),
.panel_flags            (panel_flags),
.panel_error            (panel_error),
.panel_commit_busy      (panel_commit_busy),
.demo_options_accept(demo_options_accept),
.demo_single_beat(demo_single_beat),
.demo_single_buffer(demo_single_buffer),
.diag_arm_pulse         (diag_arm_pulse),
.diag_clear_pulse       (diag_clear_pulse),
.diag_reg0              (diag_reg0),
.diag_reg1              (diag_reg1),
.diag_reg2              (diag_reg2),
.diag_reg3              (diag_reg3),
.diag_reg4              (diag_reg4),
.diag_reg5              (diag_reg5),
.diag_reg6              (diag_reg6),
.diag_reg7              (diag_reg7),
.diag_reg8              (diag_reg8),
.diag_reg9              (diag_reg9),
.diag_reg10             (diag_reg10),
.diag_reg11             (diag_reg11),
.diag_reg12             (diag_reg12),
.diag_reg13             (diag_reg13),
.diag_reg14             (diag_reg14),
.diag_reg15             (diag_reg15),
.diag_reg16             (diag_reg16),
.diag_reg17             (diag_reg17),
.diag_reg18             (diag_reg18),
.diag_reg19             (diag_reg19),
.diag_reg20             (diag_reg20),
.fifo_full              (fifo_full),
.fifo_empty             (fifo_empty),
.fifo_level             (fifo_level),
.asset_reg_addr         (asset_reg_addr),
.asset_reg_write        (asset_reg_write),
.asset_reg_wdata        (asset_reg_wdata),
.asset_reg_rdata        (asset_reg_rdata),
.asset_owner_active     (asset_owner_active)
);



/////////////////////////////////////////////////////////////////////////////

riscv_soc soc_inst
(
.system_uart_0_io_txd(system_uart_0_io_txd),
.system_uart_0_io_rxd(system_uart_0_io_rxd),
.jtagCtrl_tck(jtag_inst1_TCK),
.jtagCtrl_tdi(jtag_inst1_TDI),
.jtagCtrl_tdo(jtag_inst1_TDO),
.jtagCtrl_enable(jtag_inst1_SEL),
.jtagCtrl_capture(jtag_inst1_CAPTURE),
.jtagCtrl_shift(jtag_inst1_SHIFT),
.jtagCtrl_update(jtag_inst1_UPDATE),
.jtagCtrl_reset(jtag_inst1_RESET),
.system_gpio_0_io_read(system_gpio_0_io_read),
.system_gpio_0_io_write(system_gpio_0_io_write),
.system_gpio_0_io_writeEnable(system_gpio_0_io_writeEnable),
.io_apbSlave_0_PADDR(io_apbSlave_0_PADDR),
.io_apbSlave_0_PSEL(io_apbSlave_0_PSEL),
.io_apbSlave_0_PENABLE(io_apbSlave_0_PENABLE),
.io_apbSlave_0_PREADY(io_apbSlave_0_PREADY),
.io_apbSlave_0_PWRITE(io_apbSlave_0_PWRITE),
.io_apbSlave_0_PWDATA(io_apbSlave_0_PWDATA),
.io_apbSlave_0_PRDATA(io_apbSlave_0_PRDATA),
.io_apbSlave_0_PSLVERROR(io_apbSlave_0_PSLVERROR),

.io_systemClk(io_systemClk),
.io_asyncReset(w_io_asyncReset),
.io_systemReset(io_systemReset)		
);

endmodule

//////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2013-2023 Efinix Inc. All rights reserved.
//
// This   document  contains  proprietary information  which   is
// protected by  copyright. All rights  are reserved.  This notice
// refers to original work by Efinix, Inc. which may be derivitive
// of other work distributed under license of the authors.  In the
// case of derivative work, nothing in this notice overrides the
// original author's license agreement.  Where applicable, the 
// original license agreement is included in it's original 
// unmodified form immediately below this header.
//
// WARRANTY DISCLAIMER.  
//     THE  DESIGN, CODE, OR INFORMATION ARE PROVIDED 鈥淎S IS鈥?AND 
//     EFINIX MAKES NO WARRANTIES, EXPRESS OR IMPLIED WITH 
//     RESPECT THERETO, AND EXPRESSLY DISCLAIMS ANY IMPLIED WARRANTIES, 
//     INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF 
//     MERCHANTABILITY, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR 
//     PURPOSE.  SOME STATES DO NOT ALLOW EXCLUSIONS OF AN IMPLIED 
//     WARRANTY, SO THIS DISCLAIMER MAY NOT APPLY TO LICENSEE.
//
// LIMITATION OF LIABILITY.  
//     NOTWITHSTANDING ANYTHING TO THE CONTRARY, EXCEPT FOR BODILY 
//     INJURY, EFINIX SHALL NOT BE LIABLE WITH RESPECT TO ANY SUBJECT 
//     MATTER OF THIS AGREEMENT UNDER TORT, CONTRACT, STRICT LIABILITY 
//     OR ANY OTHER LEGAL OR EQUITABLE THEORY (I) FOR ANY INDIRECT, 
//     SPECIAL, INCIDENTAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES OF ANY 
//     CHARACTER INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF 
//     GOODWILL, DATA OR PROFIT, WORK STOPPAGE, OR COMPUTER FAILURE OR 
//     MALFUNCTION, OR IN ANY EVENT (II) FOR ANY AMOUNT IN EXCESS, IN 
//     THE AGGREGATE, OF THE FEE PAID BY LICENSEE TO EFINIX HEREUNDER 
//     (OR, IF THE FEE HAS BEEN WAIVED, $100), EVEN IF EFINIX SHALL HAVE 
//     BEEN INFORMED OF THE POSSIBILITY OF SUCH DAMAGES.  SOME STATES DO 
//     NOT ALLOW THE EXCLUSION OR LIMITATION OF INCIDENTAL OR 
//     CONSEQUENTIAL DAMAGES, SO THIS LIMITATION AND EXCLUSION MAY NOT 
//     APPLY TO LICENSEE.
//
/////////////////////////////////////////////////////////////////////////////
