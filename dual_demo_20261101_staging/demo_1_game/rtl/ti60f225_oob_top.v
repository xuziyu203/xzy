`include "ddr3_controller/ddr3_parameter.vh"
// `include "define.v"

module ti60f225_oob_top #(

	parameter                       RANK_RATIO         = 1,       // # of unique CS outputs per rank
	parameter                       ASYN_AXI_CLK       = `ASYN_AXI_CLK, 
	parameter                       RANKS              = `RANKS,
	parameter                       CK_WIDTH           = `CK_WIDTH,       // # of CK/CK# outputs to memory   
	parameter                       CKE_WIDTH          = `CKE_WIDTH,       // # of cke outputs
	parameter                       CS_WIDTH           = `CS_WIDTH,       // # of unique CS outputs
	parameter                              BANK_WIDTH         = `BANK_WIDTH,       // # of bank bits
	parameter                       ROW_WIDTH          = `ROW_WIDTH,       // DRAM address bus width
	parameter                       COL_WIDTH          = `COL_WIDTH,      // column address width
	parameter                       DM_WIDTH           = `DM_WIDTH,       // # of DM (data mask)
	parameter                       DQS_WIDTH          = `DQS_WIDTH,       // # of DQS (strobe)
	parameter                       DQ_WIDTH           = `DQ_WIDTH,      // # of DQ (data)
	parameter                        ODT_WIDTH          = `ODT_WIDTH,
	parameter                       DQ_CNT_WIDTH       = `DQ_CNT_WIDTH,       // = ceil(log2(DQ_WIDTH))
	parameter                       DQS_CNT_WIDTH      = `DQS_CNT_WIDTH,       // = ceil(log2(DQS_WIDTH))  
	parameter                       DRAM_WIDTH         = `DRAM_WIDTH,       // # of DQ per DQS   
	parameter                       DATA_WIDTH         = `DATA_WIDTH,
	parameter                       ADDR_WIDTH         = `ADDR_WIDTH,    
	parameter                       AXI_ID_WIDTH       = `AXI_ID_WIDTH,
	parameter                       AXI_ADDR_WIDTH     = `AXI_ADDR_WIDTH,
	parameter                       AXI_DATA_WIDTH     = `AXI_DATA_WIDTH
)
(

  (* syn_peri_port = 0 *) input jtag_inst1_CAPTURE,
  (* syn_peri_port = 0 *) input jtag_inst1_DRCK,
  (* syn_peri_port = 0 *) input jtag_inst1_RESET,
  (* syn_peri_port = 0 *) input jtag_inst1_RUNTEST,
  (* syn_peri_port = 0 *) input jtag_inst1_SEL,
  (* syn_peri_port = 0 *) input jtag_inst1_SHIFT,
  (* syn_peri_port = 0 *) input jtag_inst1_TCK,
  (* syn_peri_port = 0 *) input jtag_inst1_TDI,
  (* syn_peri_port = 0 *) input jtag_inst1_TMS,
  (* syn_peri_port = 0 *) input jtag_inst1_UPDATE,
  (* syn_peri_port = 0 *) output jtag_inst1_TDO,

   input wire i2c_scl_in,input wire i2c_sda_in,
   // Board manual (user, 2026-10-29): KEY1=P14 fire, KEY2=N14 shield, KEY3=A3 start, active low.
   // KEY0=C4 is i_arstn (PLL reset path, unchanged logic); it is never a software action.
   input wire key1_n, input wire key2_n, input wire key3_n,
   output wire i2c_scl_out,output wire i2c_sda_out,output wire i2c_scl_oe,output wire i2c_sda_oe,
   //Clocks 
	input	wire	i_arstn,
    input	wire	i_mipi_rx_pclk,
    input wire  vid_clk_dvi2,
    input wire hdmi_tx_slow_clk,
    input wire CLK_5M,
    input  wire CLK_25M,
	output  wire    pll_inst1_RSTN,
	input	wire	i_pll_locked,
	input 	wire  	pll_locked,
	output  wire    pll_inst4_RSTN,
	input   wire    pll_inst4_LOCKED,
	output  wire  	USER_PLL_RSTN,
    output 	wire 	DDR3_PLL_RSTN,
	input 	wire  	user_pll_locked,
    //CSI Interface
    input	 wire		io_cam_sda_IN,
    output wire   io_cam_sda_OUT,
    output wire		io_cam_sda_OE,

    output io_cam_scl_OUT,
    input io_cam_scl_IN,
    output io_cam_scl_OE,

    output	wire		o_cam_rst_p,
    
    input	wire		i_cam_ck_LP_P_IN,
    input	wire		i_cam_ck_LP_N_IN,
    output	wire		o_cam_ck_HS_TERM,
    output	wire		o_cam_ck_HS_ENA,
    input	wire		i_cam_ck_CLKOUT,
    
    input	wire	[7:0]			cam_d0_HS_IN,
    input	  wire		      cam_d0_LP_P_IN,
    input	  wire		      cam_d0_LP_N_IN,
    output	wire		      cam_d0_HS_TERM,
    output	wire		      cam_d0_HS_ENA,
    output	wire		      cam_d0_RST,
    output	wire		      cam_d0_FIFO_RD,
    input	  wire		      cam_d0_FIFO_EMPTY,
    
    input 	wire	[7:0]			cam_d1_HS_IN,
    input	  wire		        cam_d1_LP_P_IN,
    input	  wire		        cam_d1_LP_N_IN,
    output	wire		        cam_d1_HS_TERM,
    output	wire		        cam_d1_HS_ENA,
    output	wire		        cam_d1_RST,
    output	wire		        cam_d1_FIFO_RD,
    input	  wire		        cam_d1_FIFO_EMPTY,
    
    input 	wire	[7:0]			cam_d2_HS_IN,
    input	  wire		        cam_d2_LP_P_IN,
    input	  wire		        cam_d2_LP_N_IN,
    output	wire		        cam_d2_HS_TERM,
    output	wire		        cam_d2_HS_ENA,
    output	wire		        cam_d2_RST,
    output	wire		        cam_d2_FIFO_RD,
    input	  wire		        cam_d2_FIFO_EMPTY,  
   
    input 	wire	[7:0]			cam_d3_HS_IN,
    input	  wire		        cam_d3_LP_P_IN,
    input	  wire		        cam_d3_LP_N_IN,
    output	wire		        cam_d3_HS_TERM,
    output	wire		        cam_d3_HS_ENA,
    output	wire		        cam_d3_RST,
    output	wire		        cam_d3_FIFO_RD,
    input	  wire		        cam_d3_FIFO_EMPTY,

	
	input 					tx_cal_clk_90edge,
  	input 					rx_cal_clk,
  	input 					tx_cal_clk,
	input cpu_clk200,
    input cpu_pll_locked,
    output cpu_pll_reset,
	input                              core_clk,     // CORE CLK @ 100MHz
	input                              sdram_clk,    // SDRAM CK @ 400MHz
	// PLL status flags  
	output [2:0]                       pll_shift,  
	output [4:0]                       pll_shift_sel,
	output                             pll_shift_ena,  
	// memory interface ports
	output                             ddr_ck_hi,
	output                             ddr_ck_lo,
	output                             ddr_reset_n,
	output [CKE_WIDTH-1:0]             ddr_cke,     
	output [ROW_WIDTH-1:0]             ddr_addr,
	output [BANK_WIDTH-1:0]            ddr_ba,
	output                             ddr_cas_n,
 
	output [CS_WIDTH*RANK_RATIO-1:0]   ddr_cs_n,
	output                             ddr_ras_n,
	output                             ddr_we_n,
	
	input  [DQS_WIDTH-1:0]             ddr_dqs_in_hi,
	input  [DQS_WIDTH-1:0]             ddr_dqs_in_lo,
	input  [DQ_WIDTH-1:0]              ddr_dq_in_hi,
	input  [DQ_WIDTH-1:0]              ddr_dq_in_lo,
	
	output [DQS_WIDTH-1:0]             ddr_dqs_oe,
	output [DQS_WIDTH-1:0]             ddr_dqs_oe_n,
	output [DQ_WIDTH-1:0]              ddr_dq_oe,  
	output [DQS_WIDTH-1:0]             ddr_dqs_out_hi,
	output [DQS_WIDTH-1:0]             ddr_dqs_out_lo,
	output [DQ_WIDTH-1:0]              ddr_dq_out_hi,
	output [DQ_WIDTH-1:0]              ddr_dq_out_lo,
	output [DM_WIDTH-1:0]              ddr_dm_hi,
	output [DM_WIDTH-1:0]              ddr_dm_lo,
	output [ODT_WIDTH-1:0]             ddr_odt,

       //LED
       output [3:0] led,
       output wire frame_probe, // oscilloscope frame probe (GPIOL_P_13 / F3, 1.8 V)

       // MIPI DSI
       input	wire	                     i_mipi_tx_pclk		,
       output	wire	                     mipi_dp_clk_LP_P_OUT		,
       output	wire	                     mipi_dp_clk_LP_N_OUT		,
       output	wire	[7:0] 	              mipi_dp_clk_HS_OUT		,
       output	wire	                     mipi_dp_clk_HS_OE		,
       output	wire	                     mipi_dp_data3_LP_P_OUT	,
       output	wire	                     mipi_dp_data2_LP_P_OUT	,
       output	wire	                     mipi_dp_data1_LP_P_OUT	,
       output	wire	                     mipi_dp_data0_LP_P_OUT	,
       output	wire	                     mipi_dp_data3_LP_N_OUT	,
       output	wire	                     mipi_dp_data2_LP_N_OUT	,
       output	wire	                     mipi_dp_data1_LP_N_OUT	,
       output	wire	                     mipi_dp_data0_LP_N_OUT	,
       output	wire	[7:0] 	              mipi_dp_data0_HS_OUT	       ,
       output	wire	[7:0] 	              mipi_dp_data1_HS_OUT	       ,
       output	wire	[7:0] 	              mipi_dp_data2_HS_OUT	       ,
       output	wire	[7:0] 	              mipi_dp_data3_HS_OUT	       ,
       output	wire	                     mipi_dp_data3_HS_OE		,
       output	wire	                     mipi_dp_data2_HS_OE		,
       output	wire	                     mipi_dp_data1_HS_OE		,
       output	wire	                     mipi_dp_data0_HS_OE		,

       output	wire	                     mipi_dp_clk_RST		,
       output	wire	                     mipi_dp_data0_RST		,
       output	wire	                     mipi_dp_data1_RST		,
       output	wire	                     mipi_dp_data2_RST		,
       output	wire	                     mipi_dp_data3_RST		,
       output	wire	                     mipi_dp_clk_LP_P_OE		,
       output	wire	                     mipi_dp_clk_LP_N_OE		,
       output	wire	                     mipi_dp_data3_LP_P_OE	,
       output	wire	                     mipi_dp_data3_LP_N_OE	,
       output	wire	                     mipi_dp_data2_LP_P_OE	,
       output	wire	                     mipi_dp_data2_LP_N_OE	,
       output	wire	                     mipi_dp_data1_LP_P_OE	,
       output	wire	                     mipi_dp_data1_LP_N_OE	,
       output	wire	                     mipi_dp_data0_LP_P_OE	,
       output	wire	                     mipi_dp_data0_LP_N_OE	,

       input  wire	                     mipi_dp_data0_LP_P_IN	,
       input  wire	                     mipi_dp_data0_LP_N_IN	,
       output	wire	                     LCD_RST_P			,
       output wire                        LCD_POWER			,
    input  wire                        uart_rxd,
    output wire                        uart_txd,
       // hdmi interface

    output tmds_tx_clk_TX_OE,
    output [9:0] tmds_tx_clk_TX_DATA,
    output tmds_tx_clk_TX_RST,
    output tmds_tx_data0_TX_OE,
    output [9:0] tmds_tx_data0_TX_DATA,
    output tmds_tx_data0_TX_RST,
    output tmds_tx_data1_TX_OE,
    output [9:0] tmds_tx_data1_TX_DATA,
    output tmds_tx_data1_TX_RST,
    output tmds_tx_data2_TX_OE,
    output [9:0] tmds_tx_data2_TX_DATA,
    output tmds_tx_data2_TX_RST


);

function integer log2;
	input	integer	val;
	integer	i;
	begin
		log2 = 0;
		for (i=0; 2**i<val; i=i+1)
			log2 = i+1;
	end
endfunction

localparam WR_FIFO_DEPTH  = 1024;
localparam RD_FIFO_DEPTH  = 1024;
localparam MAX_HRES = 1920;
localparam HFP      = 88;
localparam HSP      = 44;
localparam HBP      = 148;

localparam MAX_VRES = 1080;
localparam VFP      = 4;
localparam VSP      = 5;
localparam VBP      = 36;
//===============================================================================
wire       riscv_pipeline_busy;
wire hdmi_init_done;
reg init_write_complete;
wire framebuffer_read_error;
wire [6:0] surface_await_mask;
wire pipeline_done_publish=0;
//signal
//===============================================================================
   
wire                              app_sr_active;
wire                              app_ref_ack;
wire                              app_zq_ack;
wire                              cal_done;

// Slave Interface Write Address Ports
wire [AXI_ID_WIDTH-1:0]           s_axi_awid;
wire [AXI_ADDR_WIDTH-1:0]         s_axi_awaddr;
wire [7:0]                        s_axi_awlen;
wire [2:0]                        s_axi_awsize;
wire [1:0]                        s_axi_awburst;
wire [0:0]                        s_axi_awlock;
wire [3:0]                        s_axi_awcache;
wire [2:0]                        s_axi_awprot;
wire [3:0]                        s_axi_awqos;
wire                              s_axi_awvalid;
wire                              s_axi_awready;
// Slave Interface Write Data Ports
wire [AXI_DATA_WIDTH-1:0]         s_axi_wdata;
wire [(AXI_DATA_WIDTH/8)-1:0]     s_axi_wstrb;
wire                              s_axi_wlast;
wire                              s_axi_wvalid;
wire                              s_axi_wready;
// Slave Interface Write Response Ports
wire                              s_axi_bready;
wire [AXI_ID_WIDTH-1:0]           s_axi_bid;
wire [1:0]                        s_axi_bresp;
wire                              s_axi_bvalid;
// Slave Interface Read Address Ports
wire [AXI_ID_WIDTH-1:0]           s_axi_arid;
wire [AXI_ADDR_WIDTH-1:0]         s_axi_araddr;
wire [7:0]                        s_axi_arlen;
wire [2:0]                        s_axi_arsize;
wire [1:0]                        s_axi_arburst;
wire [0:0]                        s_axi_arlock;
wire [3:0]                        s_axi_arcache;
wire [2:0]                        s_axi_arprot;
wire [3:0]                        s_axi_arqos;
wire                              s_axi_arvalid;
wire                              s_axi_arready;
// Slave Interface Read Data Ports
wire                              s_axi_rready;
wire [AXI_ID_WIDTH-1:0]           s_axi_rid;
wire [AXI_DATA_WIDTH-1:0]         s_axi_rdata;
wire [1:0]                        s_axi_rresp;
wire                              s_axi_rlast;
wire                              s_axi_rvalid;

// frame_buffer AXI master: write -> Random Write S1, read -> HDMI Reader S2
wire [3:0]                        fb_axi_awid;
wire [31:0]                       fb_axi_awaddr;
wire [7:0]                        fb_axi_awlen;
wire [2:0]                        fb_axi_awsize;
wire [1:0]                        fb_axi_awburst;
wire [3:0]                        fb_axi_awcache;
wire [2:0]                        fb_axi_awprot;
wire                              fb_axi_awqos;
wire                              fb_axi_awlock;
wire                              fb_axi_awvalid;
wire                              fb_axi_awready;
wire [127:0]                      fb_axi_wdata;
wire [15:0]                       fb_axi_wstrb;
wire                              fb_axi_wlast;
wire                              fb_axi_wvalid;
wire                              fb_axi_wready;
wire [3:0]                        fb_axi_bid;
wire [1:0]                        fb_axi_bresp;
wire                              fb_axi_bvalid;
wire                              fb_axi_bready;

wire [3:0]                        fb_axi_arid;
wire [31:0]                       fb_axi_araddr;
wire [7:0]                        fb_axi_arlen;
wire [2:0]                        fb_axi_arsize;
wire [1:0]                        fb_axi_arburst;
wire [3:0]                        fb_axi_arcache;
wire [2:0]                        fb_axi_arprot;
wire                              fb_axi_arqos;
wire                              fb_axi_arlock;
wire                              fb_axi_arvalid;
wire                              fb_axi_arready;
wire [3:0]                        fb_axi_rid;
wire [127:0]                      fb_axi_rdata;
wire [1:0]                        fb_axi_rresp;
wire                              fb_axi_rlast;
wire                              fb_axi_rvalid;
wire                              fb_axi_rready;

// BLIT reads current Front and writes current Back; RECT also writes Back.
localparam [31:0] FB0_BASE              = 32'h0000_0000;
localparam [31:0] FB1_BASE              = 32'h0040_0000;
localparam [31:0] FRAME_STRIDE_BYTES    = 32'd3840;
localparam [31:0] FRAME_SIZE_BYTES      = 32'd4147200;
localparam [11:0] BLOCK_TEST_WIDTH      = 12'd1920;
localparam [11:0] BLOCK_TEST_HEIGHT     = 12'd1080;

(* mark_debug = "true" *) wire     block_en_test;
(* mark_debug = "true" *) wire     block_ack_test;
(* mark_debug = "true" *) wire [31:0] block_src_base;
(* mark_debug = "true" *) wire [31:0] block_dst_base;
// Latched absolute start addresses and pixel dimensions of the copy task.
wire [11:0] block_width;
wire [11:0] block_height;
(* mark_debug = "true" *) wire [31:0] front_buffer_base;
(* mark_debug = "true" *) wire [31:0] back_buffer_base;
wire [31:0] target_stride_bytes;
wire [11:0] target_surface_width;
wire [11:0] target_surface_height;
wire [31:0] full_front_base;
wire [31:0] full_back_base;
wire [31:0] quad_front_q0;
wire [31:0] quad_front_q1;
wire [31:0] quad_front_q2;
wire [31:0] quad_front_q3;
wire [1:0] active_display_mode;
wire [1:0] active_full_source;
wire [6:0] surface_front_select;
wire [6:0] surface_pending_mask;
wire [6:0] surface_present_ack_mask;
wire [31:0] surface_generation;
wire        cpu_target_write;
wire [31:0] cpu_target_value;
wire        cpu_xy_write;
wire [31:0] cpu_xy_value;
wire        cpu_row_words_write;
wire [31:0] cpu_row_words_value;
wire        cpu_data_write;
wire [31:0] cpu_data_value;
wire        cpu_control_write;
wire [5:0]  cpu_control_value;
wire        cpu_active;
wire [31:0] cpu_status;
wire [31:0] cpu_word_count;
wire        cpu_fifo_rst_p;
wire        cpu_fifo_wr_en;
wire [143:0] cpu_fifo_wr_data;
wire        cpu_fifo_full;
wire        cpu_ddr_wr_start;
wire [32:0] cpu_wr_start_addr;
wire [23:0] cpu_wr_len;
wire        cpu_ddr_wr_done;
wire        cpu_ddr_wr_error;
wire [11:0] asset_reg_addr;
wire        asset_reg_write;
wire [31:0] asset_reg_wdata;
wire [31:0] asset_only_rdata, enemy_reg_rdata;
wire enemy_owner, enemy_render_done;
wire enemy_probe_composing, enemy_probe_present; // frame probe
wire [31:0] enemy_back_base;
wire enemy_fifo_reset,enemy_fifo_valid,enemy_wr_start;
wire [143:0] enemy_fifo_data;
wire [32:0] enemy_wr_addr;
wire [23:0] enemy_wr_beats;
wire [31:0] enemy_araddr, shared_araddr;
wire [7:0] enemy_arlen, shared_arlen;
wire enemy_arvalid,enemy_arready,enemy_rvalid,enemy_rlast,enemy_rready;
wire [127:0] enemy_rdata,shared_rdata;
wire [1:0] enemy_rresp,shared_rresp;
wire [3:0] enemy_rid,shared_rid;
wire shared_arvalid,shared_arready,shared_rvalid,shared_rlast,shared_rready;
wire enemy_owner_grant;
surface_table enemy_surface(.target_id(3'd2),.front_select(surface_front_select[2]),
 .front_base(),.back_base(enemy_back_base),.stride_bytes(),.width(),.height());

wire [31:0] asset_reg_rdata;
wire [31:0] ddrcrc_reg_rdata; // GHD DDR CRC reader, APB 0x3E0..0x3FC
assign asset_reg_rdata = (asset_reg_addr >= 12'h3e0) ? ddrcrc_reg_rdata :
                         asset_reg_addr >= 12'h160 ? enemy_reg_rdata : asset_only_rdata;
wire        asset_owner_active;
wire        asset_owner_grant;
wire        asset_write_select;
wire        asset_fifo_rst_p;
wire        asset_fifo_wr_en;
wire [143:0] asset_fifo_wr_data;
wire        asset_ddr_wr_start;
wire [32:0] asset_wr_start_addr;
wire [23:0] asset_wr_len_beats;
wire        asset_fifo_full;
wire        asset_ddr_wr_done;
wire        asset_ddr_wr_error;
wire        asset_ddr_wr_idle;
wire        asset_ddr_wr_accept;
wire        asset_ddr_fifo_empty;

wire        cpu_frame_written_pulse;
wire        cpu_write_done_pulse;
wire        cpu_swap_done_pulse;
wire [31:0] cpu_table_front_base;
wire [31:0] cpu_table_back_base;
wire [31:0] cpu_table_stride;
wire [11:0] cpu_table_width;
wire [11:0] cpu_table_height;
wire        cpu_row_request;
wire        cpu_owner_active;
wire        cpu_row_owner_grant;
wire        rect_owner_request;
wire        rect_owner_active;
wire        rect_owner_grant;
wire        cpu_frame_grant;
wire [2:0]  cpu_target_id;
wire [6:0]  cpu_frame_target_mask;
wire  [1:0]  async_write_owner;
localparam [1:0] ASYNC_OWNER_NONE = 2'd0,
                 ASYNC_OWNER_CPU  = 2'd1,
                 ASYNC_OWNER_RECT = 2'd2;
wire        riscv_async_enable_request;
wire        riscv_async_mode_reject_pulse;
wire        async_enable_accept;
wire [31:0] async_q0_present_seq;
wire [31:0] async_gpu_pair_present_seq;
wire [31:0] async_gpu_render_cycles;
wire [31:0] async_gpu_owner_wait_cycles;
wire [31:0] async_event_status;
wire        panel_commit_pulse;
wire        panel_commit_busy;
wire diag_arm_pulse, diag_clear_pulse;
wire [31:0] diag_reg0, diag_reg1, diag_reg2, diag_reg3, diag_reg4, diag_reg5, diag_reg6, diag_reg7, diag_reg8, diag_reg9, diag_reg10, diag_reg11, diag_reg12, diag_reg13, diag_reg14, diag_reg15, diag_reg16, diag_reg17, diag_reg18, diag_reg19, diag_reg20;
wire [31:0] panel_cpu_row;
wire [31:0] panel_blit_row;
wire [31:0] panel_rect_row;
wire [31:0] panel_blit_time;
wire [31:0] panel_rect_time;
wire [31:0] panel_flags;
wire [31:0] panel_error;
wire [223:0] panel_snapshot_core = {panel_error, panel_flags,
                                      panel_rect_time, panel_blit_time,
                                      panel_rect_row, panel_blit_row,
                                      panel_cpu_row};
wire [223:0] panel_snapshot_pix;
wire        panel_snapshot_valid_pix;
wire [31:0] async_q0_present_seq_r;
wire [31:0] async_gpu_pair_present_seq_r;
wire [31:0] async_gpu_render_cycles_r;
wire [31:0] async_gpu_owner_wait_cycles_r;
reg [31:0] async_event_status_r;
assign async_q0_present_seq = async_q0_present_seq_r;
assign async_gpu_pair_present_seq = async_gpu_pair_present_seq_r;
assign async_gpu_render_cycles = async_gpu_render_cycles_r;
assign async_gpu_owner_wait_cycles = async_gpu_owner_wait_cycles_r;
assign async_event_status = async_event_status_r;
assign async_enable_accept = init_write_complete && hdmi_init_done &&
                             !riscv_pipeline_busy && !cpu_active &&
                             (active_display_mode == 2'd1) &&
                             (surface_pending_mask == 7'd0);

localparam [3:0]
    PIPE_IDLE         = 4'd0,
    PIPE_PREP_COPY    = 4'd1,
    PIPE_WAIT_REQ     = 4'd2,
    PIPE_WAIT_DONE    = 4'd3,
    PIPE_CMD_FETCH    = 4'd4,
    PIPE_CMD_DECODE   = 4'd5,
    PIPE_EXEC_START   = 4'd6,
    PIPE_EXEC_WAIT    = 4'd7,
    PIPE_CMD_RETIRE   = 4'd8,
    PIPE_WAIT_SWAP    = 4'd9,
    PIPE_ERROR        = 4'd10,
    PIPE_CMD_VALIDATE = 4'd11,
    PIPE_EVENT_FLUSH = 4'd12;

localparam [3:0]
    GPU_CMD_OP_INVALID = 4'h0,
    GPU_CMD_OP_RECT    = 4'h1,
    GPU_CMD_OP_BLIT    = 4'h2;

localparam [1:0]
    GPU_EXEC_NONE = 2'd0,
    GPU_EXEC_RECT = 2'd1,
    GPU_EXEC_BLIT = 2'd2;

localparam [7:0]
    GPU_ERR_NONE               = 8'h00,
    GPU_ERR_INVALID_OPCODE     = 8'h01,
    GPU_ERR_UNSUPPORTED_OPCODE = 8'h02,
    GPU_ERR_ILLEGAL_FLAGS      = 8'h03,
    GPU_ERR_MALFORMED_RECT     = 8'h04,
    GPU_ERR_MALFORMED_BLIT     = 8'h05,
    GPU_ERR_FIFO_UNDERFLOW     = 8'h06,
    GPU_ERR_COUNT_MISMATCH     = 8'h07,
    GPU_ERR_BLOCK_COPY         = 8'h08,
    GPU_ERR_EXECUTION_UNIT     = 8'h09;

// Keep the original continuous automatic test path available in the RTL.
// APB one-shot control is active while this constant is disabled.
localparam AUTO_TEST_ENABLE = 1'b0;
// RECT fill and frame pre-copy are independent. This demo never pre-copies.
localparam SOLID_FILL_DEMO_ENABLE = 1'b1;
localparam FRAME_PRECOPY_ENABLE = 1'b0;
// One-shot DDR address probe. Keep disabled for the normal demo build; set to
// one only for a dedicated probe bitstream and restore to zero afterwards.
localparam DDR_PROBE_ENABLE = 1'b0;
localparam [31:0] DDR_PROBE_CANDIDATE_ADDR = 32'h0080_0000;
localparam [31:0] DDR_PROBE_SRC_OFFSET = 32'h0000_0100;
localparam [31:0] DDR_PROBE_DST_OFFSET = 32'h0000_0200;
localparam [11:0] DDR_PROBE_WIDTH = 12'd8;
localparam [11:0] DDR_PROBE_HEIGHT = 12'd1;
localparam [127:0] DDR_PROBE_EXPECTED_DATA =
    128'hFFFF_FFFF_FFFF_FFFF_FFFF_FFFF_FFFF_FFFF;
localparam [31:0] DDR_PROBE_TIMEOUT_CYCLES = 32'd1_000_000;

(* mark_debug = "true" *) wire [3:0] frame_pipeline_state;

localparam [3:0]
    PROBE_IDLE         = 4'd0,
    PROBE_TEST1        = 4'd1,
    PROBE_WAIT1_REQ    = 4'd2,
    PROBE_WAIT1_DONE   = 4'd3,
    PROBE_RELEASE1     = 4'd4,
    PROBE_TEST2        = 4'd5,
    PROBE_WAIT2_REQ    = 4'd6,
    PROBE_WAIT2_DONE   = 4'd7,
    PROBE_RELEASE2     = 4'd8,
    PROBE_PASS         = 4'd9,
    PROBE_FAIL         = 4'd10;

(* mark_debug = "true" *) wire [3:0] probe_state;
(* mark_debug = "true" *) wire [31:0] probe_cycles;
// Sticky flags: [0] copy, [1] DDR writer, [2] RRESP, [3] BRESP,
// [4] Test2 data mismatch, [5] timeout, [6] read count, [7] write/B count.
(* mark_debug = "true" *) wire [7:0] probe_error_flags;
(* mark_debug = "true" *) wire [31:0] probe_read_beats;
(* mark_debug = "true" *) wire [31:0] probe_write_beats;
(* mark_debug = "true" *) wire [31:0] probe_bresp_count;
(* mark_debug = "true" *) wire [1:0] probe_done_seen;
(* mark_debug = "true" *) wire [1:0] probe_last_rresp;
(* mark_debug = "true" *) wire [1:0] probe_last_bresp;
(* mark_debug = "true" *) wire [31:0] probe_front_base;
(* mark_debug = "true" *) wire [31:0] probe_back_base;
(* mark_debug = "true" *) wire [127:0] probe_read_data;
(* mark_debug = "true" *) wire [31:0] probe_last_araddr;
(* mark_debug = "true" *) wire [31:0] probe_last_awaddr;
(* mark_debug = "true" *) wire probe_mismatch;

wire [31:0] block_test_src_addr;
wire [31:0] block_test_dst_addr;

assign block_test_src_addr = block_src_base;
assign block_test_dst_addr = block_dst_base;

// Block Copy task/status signals.
(* mark_debug = "true" *) wire    block_copy_req;
(* mark_debug = "true" *) wire    block_copy_done;
(* mark_debug = "true" *) wire    block_copy_error;
(* mark_debug = "true" *) wire    block_copy_ddr_wr_error;
(* mark_debug = "true" *) wire     block_error_seen;
(* mark_debug = "true" *) wire     wr_error_seen;
wire                              block_copy_rd_sw;
wire                              block_copy_wr_sw;
wire [31:0]                       block_copy_test_wdata;
wire [31:0]                       block_copy_test_rdata;
wire [31:0]                       block_copy_wr_fifo_test;

// Block Copy AXI master -> axi_control S0
wire [3:0]                        block_axi_awid;
wire [31:0]                       block_axi_awaddr;
wire [7:0]                        block_axi_awlen;
wire [2:0]                        block_axi_awsize;
wire [1:0]                        block_axi_awburst;
wire [3:0]                        block_axi_awcache;
wire [2:0]                        block_axi_awprot;
wire                              block_axi_awlock;
(* mark_debug = "true" *) wire    block_axi_awvalid;
wire                              block_axi_awcobuf;
wire                              block_axi_awapcmd;
wire                              block_axi_awallstrb;
wire                              block_axi_awqos;
(* mark_debug = "true" *) wire    block_axi_awready;
wire [127:0]                      block_axi_wdata;
wire [15:0]                       block_axi_wstrb;
wire                              block_axi_wlast;
(* mark_debug = "true" *) wire    block_axi_wvalid;
(* mark_debug = "true" *) wire    block_axi_wready;
wire [3:0]                        block_axi_bid;
wire [1:0]                        block_axi_bresp;
(* mark_debug = "true" *) wire    block_axi_bvalid;
wire                              block_axi_bready;

wire [3:0]                        block_axi_arid;
wire [31:0]                       block_axi_araddr;
wire [7:0]                        block_axi_arlen;
wire [2:0]                        block_axi_arsize;
wire [1:0]                        block_axi_arburst;
wire [3:0]                        block_axi_arcache;
wire [2:0]                        block_axi_arprot;
wire                              block_axi_arlock;
(* mark_debug = "true" *) wire    block_axi_arvalid;
wire                              block_axi_arapcmd;
wire                              block_axi_arqos;
(* mark_debug = "true" *) wire    block_axi_arready;
wire [3:0]                        block_axi_rid;
wire [127:0]                      block_axi_rdata;
wire [1:0]                        block_axi_rresp;
wire                              block_axi_rlast;
(* mark_debug = "true" *) wire    block_axi_rvalid;
wire                              block_axi_rready;

wire probe_r_fire = block_axi_rvalid && block_axi_rready;
wire probe_b_fire = block_axi_bvalid && block_axi_bready;
wire probe_w_fire = block_axi_wvalid && block_axi_wready;
wire probe_hold_pipeline = DDR_PROBE_ENABLE && (probe_state != PROBE_PASS);
wire probe_active = DDR_PROBE_ENABLE &&
                    (probe_state != PROBE_IDLE) &&
                    (probe_state != PROBE_PASS) &&
                    (probe_state != PROBE_FAIL);
wire probe_test2_read = (probe_state == PROBE_WAIT2_REQ) ||
                        (probe_state == PROBE_WAIT2_DONE) ||
                        (probe_state == PROBE_RELEASE2);
wire [7:0] probe_error_bits_now =
        (block_copy_error ? 8'h01 : 8'h00) |
        (block_copy_ddr_wr_error ? 8'h02 : 8'h00) |
        ((probe_r_fire && (block_axi_rresp != 2'b00)) ? 8'h04 : 8'h00) |
        ((probe_b_fire && (block_axi_bresp != 2'b00)) ? 8'h08 : 8'h00) |
        ((probe_test2_read && probe_r_fire &&
          (block_axi_rdata != DDR_PROBE_EXPECTED_DATA)) ? 8'h10 : 8'h00);
wire probe_timeout_now = probe_active &&
                         (probe_cycles >= DDR_PROBE_TIMEOUT_CYCLES);
wire probe_frame_allowed = !DDR_PROBE_ENABLE ||
                           (probe_state == PROBE_PASS);
wire gamma_valid;
wire gamma_hs;
wire gamma_vs;
////////////////////////////////////////////////////////////////
// System & Debugger
wire    w_arstn;

wire	w_mipi_rx_pclk_arstn;
wire	w_mipi_rx_pclk_arst;

wire		       w_mipi_rx_vs;
wire		       w_mipi_rx_hs;
wire	       w_mipi_rx_de;
wire	[63:0]			w_mipi_rx_data	;

wire					io_systemReset;
wire vid_rst_n;


//===================================================================================================
//reset module
//===================================================================================================

assign w_arstn  		       = i_pll_locked & pll_locked & user_pll_locked;
assign pll_inst1_RSTN  		= i_arstn;
assign USER_PLL_RSTN      		= i_arstn;
assign DDR3_PLL_RSTN 		= i_arstn;
reset_ctrl
#(
	.NUM_RST		(2),
	.CYCLE			(1),
	.IN_RST_ACTIVE	(4'b00),
	.OUT_RST_ACTIVE	(4'b10)
)
inst_reset_ctrl
(
	.i_arst		({2{w_arstn}}),//({{4{i_pll_locked}},                  {2{i_arstn}}}),
	.i_clk			({2{i_mipi_rx_pclk}}),
	.o_srst		(vid_rst_n)
);


wire rx_pclk;
wire rx_vs;
wire rx_de;
//=====================================================================================
//frame buffer
//=====================================================================================

wire ch0_vs;
wire ch0_hs;
wire ch0_de;

wire renderer_vs;
wire renderer_hs;
wire renderer_de;
wire [15:0] renderer_vin;
wire renderer_pixel_ready;
reg start;
wire test_rst_n;

wire [3:0] riscv_gpio_write_unused;
wire [3:0] riscv_gpio_write_enable_unused;
wire       riscv_memory_checker_pass_unused;
wire       riscv_system_clk_rstn_unused;
wire       riscv_base_clk_pll_rstn_unused;
wire       riscv_pipeline_start_req;

wire       riscv_pipeline_done;
wire       riscv_pipeline_error;
wire [7:0] riscv_pipeline_frame_command_count;
wire       riscv_cmd_push_pulse;
wire [127:0] riscv_cmd_push_data;
wire [2:0] riscv_cmd_push_target_id;
wire [2:0] riscv_target_context;
wire [1:0] riscv_display_mode_req;
wire [1:0] riscv_full_source_req;
wire [130:0] command_fifo_head_data;
wire       command_fifo_empty;
wire       command_fifo_full;
wire [7:0] command_fifo_level;
wire       command_fifo_pop;
wire [127:0] active_command;
wire [130:0] command_prefetch;
wire [2:0]   active_target_id;
wire [7:0]  command_remaining;
wire [1:0]  active_engine;
wire [7:0]  pipeline_error_code;
wire [3:0]  pipeline_error_opcode;
wire [3:0]  pipeline_error_flags;

wire [31:0] perf_blockcopy_cycles;
wire [31:0] perf_blockcopy_total_cycles;
wire [31:0] perf_rect_last_cycles;
wire [31:0] perf_rect_total_cycles;
wire [31:0] perf_frame_cycles;
wire [31:0] perf_rect_count;
wire [31:0] rect_current_cycles;
wire perf_frame_active;
wire perf_block_active;
wire perf_rect_active;

// Temporary passive background diagnostic counters (APB 0x040-0x05C).
wire [31:0] bg_dbg_cycles;
wire [31:0] bg_dbg_w_fire;
wire [31:0] bg_dbg_wait_fifo_cycles;
wire [31:0] bg_dbg_aw_stall;
wire [31:0] bg_dbg_w_stall;
wire [31:0] bg_dbg_b_wait;
wire [31:0] bg_dbg_burst_count;
wire [31:0] bg_dbg_status;
wire bg_dbg_wait_fifo_insufficient;
wire bg_dbg_active;
wire bg_dbg_valid;
wire bg_dbg_aborted;

wire [3:0]  command_opcode;
wire [3:0]  command_flags;
wire [11:0] command_x;
wire [11:0] command_y;
wire [11:0] command_width;
wire [11:0] command_height;
wire [7:0]  command_alpha;
wire [31:0] command_src_address;
wire [15:0] command_src_stride;
wire [15:0] command_color_or_key;
// Validated command results are registered in command_validate.vh.

assign command_opcode       = active_command[3:0];
assign command_flags        = active_command[7:4];
assign command_x            = active_command[19:8];
assign command_y            = active_command[31:20];
assign command_width        = active_command[43:32];
assign command_height       = active_command[55:44];
assign command_alpha        = active_command[63:56];
assign command_src_address  = active_command[95:64];
assign command_src_stride   = active_command[111:96];
assign command_color_or_key = active_command[127:112];



assign test_rst_n = w_arstn & cal_done;
// Keep the DDR calibration indication in the HDMI clock domain.  The
// synchronizer is async-asserted by the board reset and sync-deasserted after
// two HDMI pixel-clock edges; HDMI-local state does not consume cal_done
// directly as a combinational reset enable.
wire hdmi_rst_n;
hdmi_reset_sync u_hdmi_reset_sync (
    .clk        (hdmi_tx_slow_clk),
    .arst_n     (w_arstn),
    .release_in (cal_done),
    .rst_n      (hdmi_rst_n)
);
wire [31:0] game_idle_status={23'd0,!asset_ddr_fifo_empty,!asset_ddr_wr_idle,
 enemy_owner,asset_owner_active,cpu_active,framebuffer_read_error,
 init_write_complete,surface_await_mask[2],surface_pending_mask[2]};
wire diag_begin_frame_target2 = cpu_control_write && cpu_control_value[0] && cpu_frame_grant && (cpu_target_id == 3'd2) && (cpu_target_value[2:0] == 3'd2);

assign cpu_pll_reset = i_arstn;
top_soc u_riscv_top (
.i2c_scl_in(i2c_scl_in),.i2c_sda_in(i2c_sda_in),
.i2c_scl_out(i2c_scl_out),.i2c_sda_out(i2c_sda_out),.i2c_scl_oe(i2c_scl_oe),.i2c_sda_oe(i2c_sda_oe),
    .io_systemClk                 (cpu_clk200),
    .io_peripheralClk             (core_clk),
    .systemClk_locked             (i_pll_locked & cpu_pll_locked),
    .io_asyncResetn               (test_rst_n),

    .system_uart_0_io_txd         (uart_txd),
    .system_uart_0_io_rxd         (uart_rxd),

    .jtag_inst1_TCK               (jtag_inst1_TCK),
    .jtag_inst1_TDI               (jtag_inst1_TDI),
    .jtag_inst1_TDO               (jtag_inst1_TDO),
    .jtag_inst1_SEL               (jtag_inst1_SEL),
    .jtag_inst1_CAPTURE           (jtag_inst1_CAPTURE),
    .jtag_inst1_SHIFT             (jtag_inst1_SHIFT),
    .jtag_inst1_UPDATE            (jtag_inst1_UPDATE),
    .jtag_inst1_RESET             (jtag_inst1_RESET),

    .system_gpio_0_io_read        ({key3_n,key2_n,key1_n,1'b0}),
    .system_gpio_0_io_write       (riscv_gpio_write_unused),
    .system_gpio_0_io_writeEnable (riscv_gpio_write_enable_unused),

    .memoryCheckerPass            (riscv_memory_checker_pass_unused),
    .systemClk_rstn               (riscv_system_clk_rstn_unused),
    .baseClk_pll_rstn             (riscv_base_clk_pll_rstn_unused),

    .pipeline_start_req           (riscv_pipeline_start_req),
    .pipeline_busy                (riscv_pipeline_busy),
    .pipeline_done                (riscv_pipeline_done),
    .pipeline_error               (riscv_pipeline_error),
    .pipeline_error_code          (pipeline_error_code),
    .pipeline_error_opcode        (pipeline_error_opcode),
    .pipeline_error_flags         (pipeline_error_flags),
    .pipeline_frame_command_count (riscv_pipeline_frame_command_count),
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
    .cmd_push_pulse               (riscv_cmd_push_pulse),
    .cmd_push_data                (riscv_cmd_push_data),
    .cmd_push_target_id           (riscv_cmd_push_target_id),
    .target_context               (riscv_target_context),
    .display_mode_req             (riscv_display_mode_req),
    .full_source_req              (riscv_full_source_req),
    .active_display_mode          (active_display_mode),
    .active_full_source           (active_full_source),
    .surface_front_select         (surface_front_select),
    .surface_generation           (surface_generation),
    .cpu_active                  (cpu_active),
    .cpu_status                  (cpu_status),
    .cpu_word_count              (cpu_word_count),
    .cpu_target_write            (cpu_target_write),
    .cpu_target_value            (cpu_target_value),
    .cpu_xy_write                (cpu_xy_write),
    .cpu_xy_value                (cpu_xy_value),
    .cpu_row_words_write         (cpu_row_words_write),
    .cpu_row_words_value         (cpu_row_words_value),
    .cpu_data_write              (cpu_data_write),
    .cpu_data_value              (cpu_data_value),
    .cpu_control_write           (cpu_control_write),
    .cpu_control_value           (cpu_control_value),
    .async_enable_request        (riscv_async_enable_request),
    .async_mode_reject_pulse     (riscv_async_mode_reject_pulse),
    .async_enable_accept         (async_enable_accept),
    .async_q0_present_seq        (game_idle_status),
    .async_gpu_pair_present_seq  (async_gpu_pair_present_seq),
    .async_gpu_render_cycles     (async_gpu_render_cycles),
    .async_gpu_owner_wait_cycles (async_gpu_owner_wait_cycles),
    .async_event_status          (async_event_status),
    .panel_commit_pulse          (panel_commit_pulse),
    .panel_cpu_row               (panel_cpu_row),
    .panel_blit_row              (panel_blit_row),
    .panel_rect_row              (panel_rect_row),
    .panel_blit_time             (panel_blit_time),
    .panel_rect_time             (panel_rect_time),
    .panel_flags                 (panel_flags),
    .panel_error                 (panel_error),
    .panel_commit_busy           (panel_commit_busy),
    .diag_arm_pulse              (diag_arm_pulse),
    .diag_clear_pulse            (diag_clear_pulse),
    .diag_reg0                   (diag_reg0),
    .diag_reg1                   (diag_reg1),
    .diag_reg2                   (diag_reg2),
    .diag_reg3                   (diag_reg3),
    .diag_reg4                   (diag_reg4),
    .diag_reg5                   (diag_reg5),
    .diag_reg6                   (diag_reg6),
    .diag_reg7                   (diag_reg7),
    .diag_reg8                   (diag_reg8),
    .diag_reg9                   (diag_reg9),
    .diag_reg10                  (diag_reg10),
    .diag_reg11                  (diag_reg11),
    .diag_reg12                  (diag_reg12),
    .diag_reg13                  (diag_reg13),
    .diag_reg14                  (diag_reg14),
    .diag_reg15                  (diag_reg15),
    .diag_reg16                  (diag_reg16),
    .diag_reg17                  (diag_reg17),
    .diag_reg18                  (diag_reg18),
    .diag_reg19                  (diag_reg19),
    .diag_reg20                  (diag_reg20),
    .fifo_full                    (command_fifo_full),
    .fifo_empty                   (command_fifo_empty),
    .fifo_level                   (command_fifo_level),
    .asset_reg_addr               (asset_reg_addr),
    .asset_reg_write              (asset_reg_write),
    .asset_reg_wdata              (asset_reg_wdata),
    .asset_reg_rdata              (asset_reg_rdata),
    .asset_owner_active           (asset_owner_active | enemy_owner)
);

q3_perf_snapshot_cdc #(.DATA_WIDTH(224)) u_q3_perf_snapshot_cdc (
    .core_clk             (core_clk),
    .pix_clk              (hdmi_tx_slow_clk),
    .rst_n                (test_rst_n),
    .shadow_data_core     (panel_snapshot_core),
    .commit_pulse_core    (panel_commit_pulse),
    .busy_core            (panel_commit_busy),
    .vsync_pix            (ch0_vs),
    .snapshot_pix         (panel_snapshot_pix),
    .snapshot_valid_pix   (panel_snapshot_valid_pix)
);


reg [4:0] init_start_delay;

always @(posedge core_clk or negedge test_rst_n)
begin
    if(!test_rst_n)
    begin
        init_start_delay <= 5'b00000;
    end
    else
    begin
        init_start_delay <= {
            init_start_delay[3:0],
            1'b1
        };
    end
end


wire hdmi_init_start;

assign hdmi_init_start = &init_start_delay;
wire [AXI_ADDR_WIDTH-1:0] renderer_wr_start_addr;
wire [23:0]               renderer_wr_len;

wire hdmi_init_vs;
wire hdmi_init_hs;
wire hdmi_init_de;
wire [15:0] hdmi_init_vin;
wire [23:0] hdmi_init_wr_len;
wire hdmi_init_pixel_ready;
wire init_bank_sel;
wire [AXI_ADDR_WIDTH-1:0] hdmi_init_wr_start_addr = init_bank_sel ?
        32'h0140_0000 :
        32'h0100_0000;
wire wr_done;
(* mark_debug = "true" *) wire renderer_ddr_wr_error;
wire init_hdmi_wr_start;


hdmi_init #(
    .WIDTH(1920),
    .HEIGHT(1080)
)hdmi_init_inst
(
    .clk(core_clk),
    .rst_n(test_rst_n),
    .wr_done(wr_done),
    .init_color(16'h0000),
    .init_start(hdmi_init_start),
    .ready(hdmi_init_pixel_ready),
    .vs(hdmi_init_vs),
    .hs(hdmi_init_hs),
    .de(hdmi_init_de),
    .vin(hdmi_init_vin),
    .init_bank_sel(init_bank_sel),
    .init_done(hdmi_init_done),
    .init_hdmi_wr_start(init_hdmi_wr_start),
    .wr_len(hdmi_init_wr_len)
);


(* mark_debug = "true" *) wire rect_renderer_done;
wire renderer_wr_start;


(* mark_debug = "true" *) wire renderer_start_pulse;
(* mark_debug = "true" *) wire swap_pulse;
(* mark_debug = "true" *) wire framebuffer_render_done;
wire [6:0] frame_target_mask;
wire color_change;

assign riscv_pipeline_busy =
        (frame_pipeline_state != PIPE_IDLE);

assign riscv_pipeline_error =
        (frame_pipeline_state == PIPE_ERROR);

// Complete means that the rendered Back Buffer has become the Front Buffer.
assign riscv_pipeline_done = pipeline_done_publish;
wire async_present_done =
        (riscv_display_mode_req == 2'd0) ?
        surface_present_ack_mask[riscv_full_source_req] :
        (surface_present_ack_mask[5] && surface_present_ack_mask[4]);
wire perf_present_raw =
        (riscv_async_enable_request ? async_present_done : swap_pulse) &&
        (frame_pipeline_state == PIPE_WAIT_SWAP);

// Pop only while PIPE_CMD_FETCH owns a valid FIFO head. active_command and
// the FIFO read pointer therefore update on the same core_clk edge.
assign command_fifo_pop =
        (frame_pipeline_state == PIPE_CMD_FETCH) &&
        !command_fifo_empty &&
        (command_remaining != 8'd0) &&
        !block_copy_error &&
        !block_copy_ddr_wr_error;


always @(posedge core_clk or negedge test_rst_n)
 if(!test_rst_n) init_write_complete<=0;
 else if(hdmi_init_done) init_write_complete<=1;
wire [2:0] renderer_wr_start_offset;

wire framebuffer_pixel_ready;
wire [15:0] frame_buffer_data;
wire framebuffer_rd_quiescent;
wire framebuffer_descriptor_accept;

wire [AXI_ADDR_WIDTH-1:0] fb_descriptor_start_addr;


wire tx_underflow_seen;

assign hdmi_init_pixel_ready =
        (!hdmi_init_done) ?
        framebuffer_pixel_ready :
        1'b0;

assign renderer_pixel_ready =
        (hdmi_init_done) ?
        framebuffer_pixel_ready :
        1'b0;

`include "rtl/scanout_commit_control.vh"

// Retained global health sources. Removed ASYNC producers no longer exist.
always @(posedge core_clk or negedge test_rst_n) begin
 if(!test_rst_n) async_event_status_r<=0;
 else begin
  if(framebuffer_read_error) async_event_status_r[4]<=1;
  if(tx_underflow_seen) async_event_status_r[0]<=1;
  if(riscv_async_mode_reject_pulse) async_event_status_r[2]<=1;
 end
end
display_surface_manager u_display_surface_manager (
    .clk                  (core_clk),
    .rst_n                (test_rst_n),
    .requested_mode       (riscv_display_mode_req),
    .requested_full_source(riscv_full_source_req),
    .frame_boundary       (surface_commit_boundary),
    .render_done          (framebuffer_render_done | cpu_frame_written_pulse | enemy_render_done),
    .render_target_mask   ((framebuffer_render_done ? frame_target_mask : 7'b0000000) |
                           (cpu_frame_written_pulse ? cpu_frame_target_mask : 7'b0000000) |
                           (enemy_render_done ? 7'b0000100 : 7'b0000000)),
    .async_enable         (riscv_async_enable_request),
    .query_target_id      (3'd2),
    .descriptor_accept    (framebuffer_descriptor_accept),
    .read_error           (framebuffer_read_error),
    .await_mask           (surface_await_mask),
    .active_mode          (active_display_mode),
    .active_full_source   (active_full_source),
    .front_select         (surface_front_select),
    .swap_pulse           (swap_pulse),
    .present_ack_mask     (surface_present_ack_mask),
    .pending_mask         (surface_pending_mask),
    .generation           (surface_generation),
    .query_front_base     (front_buffer_base),
    .query_back_base      (back_buffer_base),
    .query_stride_bytes   (target_stride_bytes),
    .query_width          (target_surface_width),
    .query_height         (target_surface_height),
    .full_front_base      (full_front_base),
    .full_back_base       (full_back_base),
    .quad_front_q0        (quad_front_q0),
    .quad_front_q1        (quad_front_q1),
    .quad_front_q2        (quad_front_q2),
    .quad_front_q3        (quad_front_q3)
);

// CPU selects target3 for QUAD Q0 or target2 for FULL CPU; active tasks keep the latched target.
surface_table u_cpu_surface_table (
    .target_id(cpu_target_id), .front_select(surface_front_select[cpu_target_id]),
    .front_base(cpu_table_front_base), .back_base(cpu_table_back_base),
    .stride_bytes(cpu_table_stride), .width(cpu_table_width), .height(cpu_table_height)
);

wire full_cpu_request = (riscv_display_mode_req == 2'd0) &&
                        (riscv_full_source_req == 2'd2);
assign cpu_target_id=3'd2;

assign cpu_frame_grant = init_write_complete && hdmi_init_done &&
 !enemy_owner && !asset_owner_active && !cpu_active && !framebuffer_read_error &&
 surface_pending_mask==0 && surface_await_mask==0 &&
 !fb_axi_awvalid && !fb_axi_wvalid && !fb_axi_bvalid &&
 asset_ddr_wr_idle && asset_ddr_fifo_empty;
assign cpu_row_owner_grant=cpu_frame_grant;
assign rect_owner_request=0;
assign rect_owner_active=0;
assign rect_owner_grant=0;
cpu_framebuffer_writer u_cpu_framebuffer_writer (
    .clk(core_clk), .rst_n(test_rst_n),
    .target_write(cpu_target_write), .target_value(cpu_target_value),
    .xy_write(cpu_xy_write), .xy_value(cpu_xy_value),
    .row_words_write(cpu_row_words_write), .row_words_value(cpu_row_words_value),
    .data_write(cpu_data_write), .data_value(cpu_data_value),
    .control_write(cpu_control_write), .control_value(cpu_control_value),
    .fifo_full(cpu_fifo_full),
    .owner_grant(cpu_frame_grant),
    .row_owner_grant(cpu_row_owner_grant),
    .table_front_base(cpu_table_front_base), .table_back_base(cpu_table_back_base),
    .table_stride_bytes(cpu_table_stride), .table_width(cpu_table_width),
    .table_height(cpu_table_height), .table_front_select(surface_front_select[cpu_target_id]),
    .active_full_mode((active_display_mode == 2'd0) || full_cpu_request ||
                       (cpu_active && cpu_frame_target_mask[2])),
    .active_quad_mode(active_display_mode == 2'd1),
    .active_cpu_source((active_display_mode == 2'd1) ||
                       (active_full_source == 2'd2) ||
                       (riscv_full_source_req == 2'd2) ||
                       (cpu_active && cpu_frame_target_mask[2])),
    .current_front_select(surface_front_select[cpu_target_id]),
    .async_mode(riscv_async_enable_request),
    .present_ack(surface_present_ack_mask[cpu_target_id]),
    .ddr_wr_done(cpu_ddr_wr_done), .ddr_wr_error(cpu_ddr_wr_error),
    .cpu_active(cpu_active), .cpu_frame_active(),
    .cpu_row_request(cpu_row_request),
    .cpu_owner_active(cpu_owner_active),
    .cpu_fifo_rst_p(cpu_fifo_rst_p), .cpu_fifo_wr_en(cpu_fifo_wr_en),
    .cpu_fifo_wr_data(cpu_fifo_wr_data), .cpu_ddr_wr_start(cpu_ddr_wr_start),
    .cpu_wr_start_addr(cpu_wr_start_addr), .cpu_wr_len(cpu_wr_len),
    .frame_written_pulse(cpu_frame_written_pulse), .write_done_pulse(cpu_write_done_pulse),
    .swap_done_pulse(cpu_swap_done_pulse), .status_word(cpu_status),
    .word_count(cpu_word_count), .error_code(),
    .frame_target_mask(cpu_frame_target_mask)
);


// Asset upload may lease the shared DDR writer only at an idle display
// boundary.  The reader is never reset; an in-flight descriptor keeps the
// lease closed until its normal quiescent point.
wire ddrcrc_busy; // GHD: the CRC reader holds the compositor read port; no new owner starts meanwhile
assign asset_owner_grant = !enemy_owner && !ddrcrc_busy && init_write_complete && hdmi_init_done &&
                           !riscv_pipeline_busy && !cpu_active &&
                           !rect_owner_active && !block_copy_req &&
                           command_fifo_empty && framebuffer_rd_quiescent &&
                           !framebuffer_descriptor_accept &&
                           asset_ddr_wr_idle && asset_ddr_fifo_empty;
assign asset_write_select = asset_owner_active;

asset_upload_writer u_asset_upload_writer (
    .clk(core_clk), .rst_n(test_rst_n),
    .reg_addr(asset_reg_addr), .reg_write(asset_reg_write),
    .reg_wdata(asset_reg_wdata), .reg_rdata(asset_only_rdata),
    .owner_grant(asset_owner_grant), .owner_active(asset_owner_active),
    .fifo_full(asset_fifo_full), .fifo_empty(asset_ddr_fifo_empty),
    .ddr_idle(asset_ddr_wr_idle), .ddr_accept(asset_ddr_wr_accept),
    .ddr_done(asset_ddr_wr_done), .ddr_error(asset_ddr_wr_error),
    .fifo_reset(asset_fifo_rst_p), .fifo_valid(asset_fifo_wr_en),
    .fifo_data(asset_fifo_wr_data), .wr_start(asset_ddr_wr_start),
    .wr_addr(asset_wr_start_addr), .wr_beats(asset_wr_len_beats)
);

  frame_buffer #(
.I_VID_WIDTH         (16),
.O_VID_WIDTH         (16),
.START_ADDR          (32'h01000000     ),
.AXI_DATA_WIDTH      ( AXI_DATA_WIDTH	),
.AXI_ADDR_WIDTH      ( AXI_ADDR_WIDTH	),
.WR_FIFO_DEPTH	      ( 1024		),    
.RD_FIFO_DEPTH 	      ( 1024 	),
.BURST_LEN  	       (127),
.FB_NUM	            (3),
.MAX_VID_WIDTH	    (1920) ,
.MAX_VID_HIGHT	    (1080) 
)checker0(
       .axi_clk		(core_clk 	        ),
       .rst_n			( test_rst_n ),
      .frame_start_in         (init_write_complete ? renderer_vs : hdmi_init_vs),
      // Initialization continues to use the proven pixel-stream path.
      .solid_fill_enable      (1'b0),
      .solid_fill_color       (command_color_or_key),
       .bg_dbg_wait_fifo_insufficient(bg_dbg_wait_fifo_insufficient),
        .wr_start_addr          ({1'b0, (init_write_complete ? renderer_wr_start_addr : hdmi_init_wr_start_addr)}),
/*i*/.i_clk			              (core_clk      ),
/*i*/.pixel_valid			        (init_write_complete ? renderer_de : hdmi_init_de	),
/*i*/.pixel_ready			        ( framebuffer_pixel_ready	),
/*i*/.vin 			              (init_write_complete ? renderer_vin : hdmi_init_vin),
     .wr_start                (init_write_complete ? renderer_wr_start : init_hdmi_wr_start),               
/*i*/.o_clk			              (hdmi_tx_slow_clk),//(i_mipi_rx_pclk	),
/*i*/.o_hs    		            (ch0_hs	),			
/*i*/.o_vs    		            (ch0_vs		),			
/*i*/.o_de    		            (ch0_de		),			
/*i*/.vout    		            (frame_buffer_data	),//ch0_r,
     .wr_len                  (init_write_complete ? renderer_wr_len : hdmi_init_wr_len),
     .render_done              (framebuffer_render_done),
       .tx_underflow_seen        (tx_underflow_seen),
     .use_external_buffers     (1'b1),
     .external_front_buffer_base(full_front_base),
     .external_back_buffer_base(full_back_base),
     .external_swap_pulse      (swap_pulse),
     .quad_scanout_enable      (1'b0),
     .quad_front_q0            (quad_front_q0),
     .quad_front_q1            (quad_front_q1),
     .quad_front_q2            (quad_front_q2),
     .quad_front_q3            (quad_front_q3),
    .init_bank_sel(init_bank_sel),
    .init_write_complete        (init_write_complete),

/*i*/.H_FRONT_PORCH (HFP  		),//
/*i*/.H_SYNC 	 	    (HSP	 	),//
/*i*/.H_VALID 	 	  (MAX_HRES     ),//		
/*i*/.H_BACK_PORCH	(HBP		),//		
/*i*/.V_FRONT_PORCH (VFP  		),//		
/*i*/.V_SYNC 	 	    (VSP 	 	),//		
/*i*/.V_VALID 	 	  (MAX_VRES     ),//		
/*i*/.V_BACK_PORCH	(VBP		),//	
      .start_pixel_offset(init_write_complete ? renderer_wr_start_offset : 3'd0),
       .awid			(fb_axi_awid	),
       .awaddr		(fb_axi_awaddr	),
       .awlen			(fb_axi_awlen	),
       .awsize		(fb_axi_awsize	),
       .awburst		(fb_axi_awburst),
       .awprot    (fb_axi_awprot ),
       .awcache   (fb_axi_awcache),
       .awlock		(fb_axi_awlock	),
       .awqos     (fb_axi_awqos),
       .awvalid		(fb_axi_awvalid),
       .awready		(fb_axi_awready),

       .arid			(fb_axi_arid	  ),
       .araddr		(fb_axi_araddr	),
       .arlen			(fb_axi_arlen	),
       .arsize		(fb_axi_arsize	),
       .arburst 	(fb_axi_arburst),

       .arprot    (fb_axi_arprot ),
       .arcache   (fb_axi_arcache),
       .arlock 		(fb_axi_arlock	),
       .arqos     (fb_axi_arqos),
       .arvalid		(fb_axi_arvalid),
       .arready		(fb_axi_arready),

       .wdata			(fb_axi_wdata	),
       .wstrb			(fb_axi_wstrb	),
       .wlast			(fb_axi_wlast	),
       .wvalid		(fb_axi_wvalid	),
       .wready		(fb_axi_wready	),


       .rid			  (fb_axi_rid	),
       .rdata			(fb_axi_rdata	),
       .rlast			(fb_axi_rlast	),
       .rvalid 		(fb_axi_rvalid	),
       .rready		(fb_axi_rready	),
       .rresp			(fb_axi_rresp	),

       .bid			  (fb_axi_bid	),
       .bvalid		(fb_axi_bvalid	),
       .bready	  (fb_axi_bready	),

       .s_axi_bresp(fb_axi_bresp),
       .ddr_wr_done(wr_done),
       .ddr_wr_error(renderer_ddr_wr_error),
       .cpu_write_select(cpu_active),
       .cpu_fifo_rst_p(cpu_fifo_rst_p),
       .cpu_fifo_wr_en(cpu_fifo_wr_en),
       .cpu_fifo_wr_data(cpu_fifo_wr_data),
       .cpu_ddr_wr_start(cpu_ddr_wr_start),
       .cpu_wr_start_addr_absolute(cpu_wr_start_addr),
       .cpu_wr_len_beats(cpu_wr_len),
       .cpu_fifo_full(cpu_fifo_full),
       .cpu_ddr_wr_done(cpu_ddr_wr_done),
       .cpu_ddr_wr_error(cpu_ddr_wr_error),
       .asset_write_select(asset_write_select | enemy_owner),
       .asset_fifo_rst_p(enemy_owner ? enemy_fifo_reset : asset_fifo_rst_p),
       .asset_fifo_wr_en(enemy_owner ? enemy_fifo_valid : asset_fifo_wr_en),
       .asset_fifo_wr_data(enemy_owner ? enemy_fifo_data : asset_fifo_wr_data),
       .asset_ddr_wr_start(enemy_owner ? enemy_wr_start : asset_ddr_wr_start),
       .asset_wr_start_addr(enemy_owner ? enemy_wr_addr : asset_wr_start_addr),
       .asset_wr_len_beats(enemy_owner ? enemy_wr_beats : asset_wr_len_beats),
       .asset_fifo_full(asset_fifo_full),
       .asset_ddr_wr_done(asset_ddr_wr_done),
       .asset_ddr_wr_error(asset_ddr_wr_error),
       .asset_ddr_wr_idle(asset_ddr_wr_idle),
       .asset_ddr_wr_accept(asset_ddr_wr_accept),
       .asset_ddr_fifo_empty(asset_ddr_fifo_empty),
       .rd_quiescent(framebuffer_rd_quiescent),
       .descriptor_accept(framebuffer_descriptor_accept),
       .descriptor_release(scanout_descriptor_release),
       .read_error(framebuffer_read_error),
       .descriptor_start_addr_debug(fb_descriptor_start_addr)
);

full_cpu_diag_snapshot u_full_cpu_diag_snapshot (
    .clk(core_clk), .rst_n(test_rst_n),
    .arm_pulse(diag_arm_pulse), .clear_pulse(diag_clear_pulse),
    .begin_frame_target2(diag_begin_frame_target2),
    .begin_back_base(cpu_table_back_base), .begin_front_base(cpu_table_front_base),
    .begin_generation(surface_generation), .begin_pending_mask(surface_pending_mask),
    .cpu_frame_written_pulse(cpu_frame_written_pulse),
    .cpu_write_done_pulse(cpu_write_done_pulse),
    .cpu_swap_done_pulse(cpu_swap_done_pulse), .cpu_status(cpu_status),
    .cpu_word_count(cpu_word_count), .cpu_ddr_wr_start(cpu_ddr_wr_start),
    .cpu_wr_start_addr(cpu_wr_start_addr), .cpu_wr_len(cpu_wr_len),
    .cpu_write_select(cpu_active),
    .cpu_ddr_wr_done(cpu_ddr_wr_done), .cpu_ddr_wr_error(cpu_ddr_wr_error),
    .awaddr(fb_axi_awaddr), .awlen(fb_axi_awlen), .awvalid(fb_axi_awvalid),
    .awready(fb_axi_awready), .wdata(fb_axi_wdata), .wstrb(fb_axi_wstrb),
    .wlast(fb_axi_wlast), .wvalid(fb_axi_wvalid), .wready(fb_axi_wready),
    .bvalid(fb_axi_bvalid), .bready(fb_axi_bready), .bresp(fb_axi_bresp),
    .descriptor_accept(framebuffer_descriptor_accept),
    .descriptor_start_addr(fb_descriptor_start_addr),
    .surface_generation(surface_generation),
    .surface_present_ack_mask(surface_present_ack_mask),
    .araddr(fb_axi_araddr), .arvalid(fb_axi_arvalid), .arready(fb_axi_arready),
    .arid(fb_axi_arid), .rdata(fb_axi_rdata), .rvalid(fb_axi_rvalid),
    .rready(fb_axi_rready), .rid(fb_axi_rid), .rresp(fb_axi_rresp),
    .rlast(fb_axi_rlast),
    .diag_reg0(diag_reg0), .diag_reg1(diag_reg1), .diag_reg2(diag_reg2),
    .diag_reg3(diag_reg3), .diag_reg4(diag_reg4), .diag_reg5(diag_reg5),
    .diag_reg6(diag_reg6), .diag_reg7(diag_reg7), .diag_reg8(diag_reg8),
    .diag_reg9(diag_reg9), .diag_reg10(diag_reg10), .diag_reg11(diag_reg11),
    .diag_reg12(diag_reg12), .diag_reg13(diag_reg13), .diag_reg14(diag_reg14),
    .diag_reg15(diag_reg15), .diag_reg16(diag_reg16), .diag_reg17(diag_reg17),
    .diag_reg18(diag_reg18), .diag_reg19(diag_reg19), .diag_reg20(diag_reg20)
);

assign enemy_owner_grant = !asset_owner_active && !enemy_owner && !ddrcrc_busy &&
 init_write_complete && hdmi_init_done && !riscv_async_enable_request &&
 !riscv_pipeline_busy && !cpu_active && !rect_owner_active && !block_copy_req &&
 command_fifo_empty && surface_pending_mask==0 && surface_await_mask==0 &&
 full_cpu_request && active_display_mode==0 && active_full_source==2 &&
 !framebuffer_read_error && asset_ddr_wr_idle && asset_ddr_fifo_empty;
wire enemy_perf_ar_selected;
// GHD3: PERF_DETAIL=1 = per-frame cycle buckets (0x190..0x1D8) for the performance model (observation only)
`include "rtl/frame_probe_mode.vh"
// Oscilloscope frame probe on core_clk: FRAME_BUSY (compose of one frame) or FRAME_DONE_PULSE (10 us per completed frame).
frame_probe #(.MODE(`FRAME_PROBE_MODE),.PULSE_CYCLES(1000)) u_frame_probe (
 .clk(core_clk),.rst_n(test_rst_n),
 .composing(enemy_probe_composing),.in_present(enemy_probe_present),.probe(frame_probe));
enemy_compositor #(.PERF_DETAIL(1)) u_enemy_compositor (
 .clk(core_clk),.rst_n(test_rst_n),.reg_addr(asset_reg_addr),
 .reg_write(asset_reg_write),.reg_wdata(asset_reg_wdata),.reg_rdata(enemy_reg_rdata),
 .owner_grant(enemy_owner_grant),.back_base(enemy_back_base),.generation(surface_generation),
 .present_ack(surface_present_ack_mask[2]),
 .display_ok(active_display_mode==0 && active_full_source==2),.display_error(framebuffer_read_error),
 .owner_active(enemy_owner),.render_done(enemy_render_done),.probe_composing(enemy_probe_composing),.probe_present(enemy_probe_present),
 .araddr(enemy_araddr),.arlen(enemy_arlen),.arvalid(enemy_arvalid),.arready(enemy_arready),
 .rdata(enemy_rdata),.rresp(enemy_rresp),.rid(enemy_rid),.rlast(enemy_rlast),
 .rvalid(enemy_rvalid),.rready(enemy_rready),
 .fifo_full(asset_fifo_full),.fifo_empty(asset_ddr_fifo_empty),.ddr_idle(asset_ddr_wr_idle),
 .ddr_accept(asset_ddr_wr_accept),.ddr_done(asset_ddr_wr_done),.ddr_error(asset_ddr_wr_error),
 .fifo_reset(enemy_fifo_reset),.fifo_valid(enemy_fifo_valid),.fifo_data(enemy_fifo_data),
 .wr_start(enemy_wr_start),.wr_addr(enemy_wr_addr),.wr_beats(enemy_wr_beats),
 .perf_ar_selected(enemy_perf_ar_selected),.perf_awvalid(fb_axi_awvalid),.perf_awready(fb_axi_awready),
 .perf_wvalid(fb_axi_wvalid),.perf_wready(fb_axi_wready),.perf_wlast(fb_axi_wlast),
 .perf_bvalid(fb_axi_bvalid),.perf_bready(fb_axi_bready));
// GHD3: DDR CRC reader and its share of the arbiter compositor port (rtl/ddr_crc_port.v). The reader starts only with no
// compositor/asset owner and no compositor read outstanding; its busy flag gates both owner grants (assignments above).
wire [31:0] arb_c_addr; wire [7:0] arb_c_len; wire arb_c_valid, arb_c_ready, arb_c_rvalid, arb_c_rready;
ddr_crc_port u_ddr_crc_port (
 .clk(core_clk),.rst_n(test_rst_n),.reg_addr(asset_reg_addr),.reg_write(asset_reg_write),.reg_wdata(asset_reg_wdata),.reg_rdata(ddrcrc_reg_rdata),
 .enemy_owner(enemy_owner),.asset_owner(asset_owner_active),.busy(ddrcrc_busy),
 .e_araddr(enemy_araddr),.e_arlen(enemy_arlen),.e_arvalid(enemy_arvalid),.e_arready(enemy_arready),.e_rvalid(enemy_rvalid),.e_rready(enemy_rready),
 .c_addr(arb_c_addr),.c_len(arb_c_len),.c_valid(arb_c_valid),.c_ready(arb_c_ready),
 .c_data(enemy_rdata),.c_resp(enemy_rresp),.c_last(enemy_rlast),.c_rvalid(arb_c_rvalid),.c_rready(arb_c_rready));
enemy_read_arbiter u_enemy_read_arbiter (
 .clk(core_clk),.rst_n(test_rst_n),
 .h_addr(fb_axi_araddr),.h_len(fb_axi_arlen),.h_valid(fb_axi_arvalid),.h_ready(fb_axi_arready),
 .h_data(fb_axi_rdata),.h_resp(fb_axi_rresp),.h_id(fb_axi_rid),.h_last(fb_axi_rlast),
 .h_rvalid(fb_axi_rvalid),.h_rready(fb_axi_rready),
 .c_addr(arb_c_addr),.c_len(arb_c_len),.c_valid(arb_c_valid),.c_ready(arb_c_ready),
 .c_data(enemy_rdata),.c_resp(enemy_rresp),.c_id(enemy_rid),.c_last(enemy_rlast),
 .c_rvalid(arb_c_rvalid),.c_rready(arb_c_rready),
 .m_addr(shared_araddr),.m_len(shared_arlen),.m_valid(shared_arvalid),.m_ready(shared_arready),
 .m_data(shared_rdata),.m_resp(shared_rresp),.m_id(shared_rid),.m_last(shared_rlast),
 .m_rvalid(shared_rvalid),.m_rready(shared_rready),.idle(),.perf_c_selected(enemy_perf_ar_selected));

axi_control u_axi_control
(
  .clk                              (core_clk),
  .rst_n                            (test_rst_n),

  // S2: frame_buffer read -> HDMI Reader
  .hdmi_arid                        (fb_axi_arid),
  .hdmi_araddr                      (shared_araddr),
  .hdmi_arlen                       (shared_arlen),
  .hdmi_arsize                      (fb_axi_arsize),
  .hdmi_arburst                     (fb_axi_arburst),
  .hdmi_arcache                     (fb_axi_arcache),
  .hdmi_arprot                      (fb_axi_arprot),
  .hdmi_arqos                       ({3'b000, fb_axi_arqos}),
  .hdmi_arvalid                     (shared_arvalid),
  .hdmi_arready                     (shared_arready),
  .hdmi_rid                         (shared_rid),
  .hdmi_rdata                       (shared_rdata),
  .hdmi_rresp                       (shared_rresp),
  .hdmi_rlast                       (shared_rlast),
  .hdmi_rvalid                      (shared_rvalid),
  .hdmi_rready                      (shared_rready),

  // S1: frame_buffer write -> Random Write
  .rw_awid                          (fb_axi_awid),
  .rw_awaddr                        (fb_axi_awaddr),
  .rw_awlen                         (fb_axi_awlen),
  .rw_awsize                        (fb_axi_awsize),
  .rw_awburst                       (fb_axi_awburst),
  .rw_awcache                       (fb_axi_awcache),
  .rw_awprot                        (fb_axi_awprot),
  .rw_awqos                         ({3'b000, fb_axi_awqos}),
  .rw_awvalid                       (fb_axi_awvalid),
  .rw_awready                       (fb_axi_awready),
  .rw_wdata                         (fb_axi_wdata),
  .rw_wstrb                         (fb_axi_wstrb),
  .rw_wlast                         (fb_axi_wlast),
  .rw_wvalid                        (fb_axi_wvalid),
  .rw_wready                        (fb_axi_wready),
  .rw_bid                           (fb_axi_bid),
  .rw_bresp                         (fb_axi_bresp),
  .rw_bvalid                        (fb_axi_bvalid),
  .rw_bready                        (fb_axi_bready),

  // S0: Block Copy AXI master
  .block_awid                       (block_axi_awid),
  .block_awaddr                     (block_axi_awaddr),
  .block_awlen                      (block_axi_awlen),
  .block_awsize                     (block_axi_awsize),
  .block_awburst                    (block_axi_awburst),
  .block_awcache                    (block_axi_awcache),
  .block_awprot                     (block_axi_awprot),
  .block_awqos                      ({3'b000, block_axi_awqos}),
  .block_awvalid                    (block_axi_awvalid),
  .block_awready                    (block_axi_awready),
  .block_wdata                      (block_axi_wdata),
  .block_wstrb                      (block_axi_wstrb),
  .block_wlast                      (block_axi_wlast),
  .block_wvalid                     (block_axi_wvalid),
  .block_wready                     (block_axi_wready),
  .block_bid                        (block_axi_bid),
  .block_bresp                      (block_axi_bresp),
  .block_bvalid                     (block_axi_bvalid),
  .block_bready                     (block_axi_bready),
  .block_arid                       (block_axi_arid),
  .block_araddr                     (block_axi_araddr),
  .block_arlen                      (block_axi_arlen),
  .block_arsize                     (block_axi_arsize),
  .block_arburst                    (block_axi_arburst),
  .block_arcache                    (block_axi_arcache),
  .block_arprot                     (block_axi_arprot),
  .block_arqos                      ({3'b000, block_axi_arqos}),
  .block_arvalid                    (block_axi_arvalid),
  .block_arready                    (block_axi_arready),
  .block_rid                        (block_axi_rid),
  .block_rdata                      (block_axi_rdata),
  .block_rresp                      (block_axi_rresp),
  .block_rlast                      (block_axi_rlast),
  .block_rvalid                     (block_axi_rvalid),
  .block_rready                     (block_axi_rready),

  // M0: the only AXI master connected to DDR3 Controller
  .m_axi_awid                       (s_axi_awid),
  .m_axi_awaddr                     (s_axi_awaddr),
  .m_axi_awlen                      (s_axi_awlen),
  .m_axi_awsize                     (s_axi_awsize),
  .m_axi_awburst                    (s_axi_awburst),
  .m_axi_awcache                    (s_axi_awcache),
  .m_axi_awprot                     (s_axi_awprot),
  .m_axi_awqos                      (s_axi_awqos),
  .m_axi_awlock                     (s_axi_awlock),
  .m_axi_awvalid                    (s_axi_awvalid),
  .m_axi_awready                    (s_axi_awready),
  .m_axi_wdata                      (s_axi_wdata),
  .m_axi_wstrb                      (s_axi_wstrb),
  .m_axi_wlast                      (s_axi_wlast),
  .m_axi_wvalid                     (s_axi_wvalid),
  .m_axi_wready                     (s_axi_wready),
  .m_axi_bid                        (s_axi_bid),
  .m_axi_bresp                      (s_axi_bresp),
  .m_axi_bvalid                     (s_axi_bvalid),
  .m_axi_bready                     (s_axi_bready),
  .m_axi_arid                       (s_axi_arid),
  .m_axi_araddr                     (s_axi_araddr),
  .m_axi_arlen                      (s_axi_arlen),
  .m_axi_arsize                     (s_axi_arsize),
  .m_axi_arburst                    (s_axi_arburst),
  .m_axi_arcache                    (s_axi_arcache),
  .m_axi_arprot                     (s_axi_arprot),
  .m_axi_arqos                      (s_axi_arqos),
  .m_axi_arlock                     (s_axi_arlock),
  .m_axi_arvalid                    (s_axi_arvalid),
  .m_axi_arready                    (s_axi_arready),
  .m_axi_rid                        (s_axi_rid),
  .m_axi_rdata                      (s_axi_rdata),
  .m_axi_rresp                      (s_axi_rresp),
  .m_axi_rlast                      (s_axi_rlast),
  .m_axi_rvalid                     (s_axi_rvalid),
  .m_axi_rready                     (s_axi_rready)
);

ddr3_top                 u_ddr3_top
(
  .axi_clk                (core_clk            ),
  .core_clk               (core_clk            ),
  .sdram_clk              (sdram_clk           ),  
  .rx_cal_clk             (rx_cal_clk          ),
  .tx_cal_clk             (tx_cal_clk          ),
  .tx_cal_clk_90edge      (tx_cal_clk_90edge   ),
  .rstn                   (w_arstn             ),      
  .pll_shift              (pll_shift           ),
  .pll_shift_sel          (pll_shift_sel       ),
  .pll_shift_ena          (pll_shift_ena       ),       
  ///////////////DDR BUS
  .ddr_ck_hi              (ddr_ck_hi           ),
  .ddr_ck_lo              (ddr_ck_lo           ),
  .ddr_cke                (ddr_cke             ),    
  .ddr_reset_n            (ddr_reset_n         ),
  .ddr_cs_n               (ddr_cs_n            ),
  .ddr_ras_n              (ddr_ras_n           ),
  .ddr_cas_n              (ddr_cas_n           ),
  .ddr_we_n               (ddr_we_n            ),     
  .ddr_addr               (ddr_addr            ),
  .ddr_ba                 (ddr_ba              ),

  .ddr_dqs_oe             (ddr_dqs_oe          ),
  .ddr_dqs_oe_n           (ddr_dqs_oe_n        ),
  .ddr_dq_oe              (ddr_dq_oe           ),
  .ddr_dqs_in_hi          (ddr_dqs_in_hi       ),
  .ddr_dqs_in_lo          (ddr_dqs_in_lo       ),
  .ddr_dq_in_hi           (ddr_dq_in_hi        ),
  .ddr_dq_in_lo           (ddr_dq_in_lo        ),

  .ddr_dqs_out_hi         (ddr_dqs_out_hi      ),
  .ddr_dqs_out_lo         (ddr_dqs_out_lo      ),
  .ddr_dq_out_hi          (ddr_dq_out_hi       ),
  .ddr_dq_out_lo          (ddr_dq_out_lo       ),

  .ddr_dm_hi              (ddr_dm_hi           ),
  .ddr_dm_lo              (ddr_dm_lo           ),
  .ddr_odt                (ddr_odt             ),

  // Application interface ports
  .app_sr_req                     (1'b0),
  .app_ref_req                    (1'b0),
  .app_zq_req                     (1'b0),
  .app_sr_active                  (app_sr_active),
  .app_ref_ack                    (app_ref_ack),
  .app_zq_ack                     (app_zq_ack),

  // Slave Interface Write Address Ports
  .s_axi_awid                     (s_axi_awid        ),
  .s_axi_awaddr                   (s_axi_awaddr      ),
  .s_axi_awlen                    (s_axi_awlen       ),
  .s_axi_awsize                   (s_axi_awsize      ),
  .s_axi_awburst                  (s_axi_awburst     ),
  .s_axi_awlock                   (s_axi_awlock      ),
  .s_axi_awcache                  (s_axi_awcache     ),
  .s_axi_awprot                   (s_axi_awprot      ),
  .s_axi_awqos                    (s_axi_awqos       ),
  .s_axi_awvalid                  (s_axi_awvalid     ),
  .s_axi_awready                  (s_axi_awready     ),
  // Slave Interface Write Data Ports
  .s_axi_wdata                    (s_axi_wdata       ),
  .s_axi_wstrb                    (s_axi_wstrb       ),
  .s_axi_wlast                    (s_axi_wlast       ),
  .s_axi_wvalid                   (s_axi_wvalid      ),
  .s_axi_wready                   (s_axi_wready      ),
  // Slave Interface Write Response Ports
  .s_axi_bid                      (s_axi_bid         ),
  .s_axi_bresp                    (s_axi_bresp       ),
  .s_axi_bvalid                   (s_axi_bvalid      ),
  .s_axi_bready                   (s_axi_bready      ),
  // Slave Interface Read Address Ports
  .s_axi_arid                     (s_axi_arid        ),
  .s_axi_araddr                   (s_axi_araddr      ),
  .s_axi_arlen                    (s_axi_arlen       ),
  .s_axi_arsize                   (s_axi_arsize      ),
  .s_axi_arburst                  (s_axi_arburst     ),
  .s_axi_arlock                   (s_axi_arlock      ),
  .s_axi_arcache                  (s_axi_arcache     ),
  .s_axi_arprot                   (s_axi_arprot      ),
  .s_axi_arqos                    (s_axi_arqos       ),
  .s_axi_arvalid                  (s_axi_arvalid     ),
  .s_axi_arready                  (s_axi_arready     ),
  // Slave Interface Read Data Ports
  .s_axi_rid                      (s_axi_rid         ),
  .s_axi_rdata                    (s_axi_rdata       ),
  .s_axi_rresp                    (s_axi_rresp       ),
  .s_axi_rlast                    (s_axi_rlast       ),
  .s_axi_rvalid                   (s_axi_rvalid      ),
  .s_axi_rready                   (s_axi_rready      ),
  //DEBUG       
  .wrlvl_dq_check                 (wrlvl_dq_check    ) ,
  .rd_level_dqs_check             (rd_level_dqs_check) ,
  .init_cur_state                 (init_cur_state    ) ,
  .idelay_ld                      (idelay_ld         ) ,
  .mpr_rdlvl_dly                  (mpr_rdlvl_dly     ) ,
  .cal_done                       (cal_done          ) 
);
//=================================================================================
//hdmi tx ctrl 
//=================================================================================

assign tmds_tx_data0_TX_OE = 1'b1;
assign tmds_tx_data1_TX_OE = 1'b1;
assign tmds_tx_data2_TX_OE = 1'b1;
assign tmds_tx_clk_TX_OE   = 1'b1;

assign tmds_tx_data0_TX_RST = 1'b0;
assign tmds_tx_data1_TX_RST = 1'b0;
assign tmds_tx_data2_TX_RST = 1'b0;
assign tmds_tx_clk_TX_RST   = 1'b0;

wire [9:0] tmds_data0;
wire [9:0] tmds_data1;
wire [9:0] tmds_data2;
wire [9:0] tmds_clk ;
wire [7:0] gamma_rdata ;
wire [7:0] gamma_gdata ;
wire [7:0] gamma_bdata ;
wire crosshair_pixel;
wire [15:0] q3_overlay_pixel;
wire q3_overlay_de;
wire q3_overlay_hs;
wire q3_overlay_vs;

// Cold boot is held by the scanout FIFO reset protocol in frame_buffer.
reg [11:0] hdmi_scan_x;
reg [11:0] hdmi_scan_y;
reg quad_mode_hdmi_d0;
reg quad_mode_hdmi_d1;
wire quad_mode_hdmi = quad_mode_hdmi_d1;
assign crosshair_pixel = quad_mode_hdmi &&
                         (((hdmi_scan_x >= 12'd958) && (hdmi_scan_x < 12'd962)) ||
                          ((hdmi_scan_y >= 12'd538) && (hdmi_scan_y < 12'd542)));
always @(posedge hdmi_tx_slow_clk or negedge hdmi_rst_n) begin
    if(!hdmi_rst_n) begin
        hdmi_scan_x <= 12'd0;
        hdmi_scan_y <= 12'd0;
        quad_mode_hdmi_d0 <= 1'b0;
        quad_mode_hdmi_d1 <= 1'b0;
    end else begin
        quad_mode_hdmi_d0 <= (active_display_mode == 2'd1);
        quad_mode_hdmi_d1 <= quad_mode_hdmi_d0;
        if(ch0_vs) begin
            hdmi_scan_x <= 12'd0;
            hdmi_scan_y <= 12'd0;
        end else if(ch0_hs) begin
            hdmi_scan_x <= 12'd0;
        end else if(ch0_de) begin
            if(hdmi_scan_x == 12'd1919) begin
                hdmi_scan_x <= 12'd0;
                if(hdmi_scan_y != 12'd1079)
                    hdmi_scan_y <= hdmi_scan_y + 12'd1;
            end else begin
                hdmi_scan_x <= hdmi_scan_x + 12'd1;
            end
        end
    end
end

// GHD HUD2: top-right game HUD + GAME OVER mixed into the scanout pixel (5 cycles, pixel/sync/scan pass through aligned).
// Enabled only by snapshot flags [13]+[7]; with HUD2 off it is a pure delay line, and the q3 overlay below is unchanged.
wire [15:0] hud2_pixel; wire hud2_de, hud2_hs, hud2_vs; wire [11:0] hud2_x, hud2_y;
game_hud2_overlay u_game_hud2_overlay (
    .clk(hdmi_tx_slow_clk), .rst_n(hdmi_rst_n),
    .pixel_in(frame_buffer_data), .de_in(ch0_de), .hs_in(ch0_hs), .vs_in(ch0_vs), .scan_x(hdmi_scan_x), .scan_y(hdmi_scan_y),
    .snapshot_pix(panel_snapshot_pix), .snapshot_valid_pix(panel_snapshot_valid_pix),
    .pixel_out(hud2_pixel), .de_out(hud2_de), .hs_out(hud2_hs), .vs_out(hud2_vs), .x_out(hud2_x), .y_out(hud2_y));
q3_perf_overlay_pipe u_q3_perf_overlay (
    .clk                  (hdmi_tx_slow_clk),
    .rst_n                (hdmi_rst_n),
    .pixel_in             (hud2_pixel),
    .de_in                (hud2_de),
    .hs_in                (hud2_hs),
    .vs_in                (hud2_vs),
    .quad_enable          (quad_mode_hdmi),
    .scan_x               (hud2_x),
    .scan_y               (hud2_y),
    .snapshot_pix         (panel_snapshot_pix),
    .snapshot_valid_pix   (panel_snapshot_valid_pix),
    .pixel_out            (q3_overlay_pixel),
    .de_out               (q3_overlay_de),
    .hs_out               (q3_overlay_hs),
    .vs_out               (q3_overlay_vs)
);

wire [15:0] scanout_pixel = q3_overlay_pixel;
wire [7:0] fb_r8 = {scanout_pixel[15:11], scanout_pixel[15:13]};
wire [7:0] fb_g8 = {scanout_pixel[10:5], scanout_pixel[10:9]};
wire [7:0] fb_b8 = {scanout_pixel[4:0], scanout_pixel[4:2]};
wire [7:0] scanout_r8 = {scanout_pixel[15:11], scanout_pixel[15:13]};
wire [7:0] scanout_g8 = {scanout_pixel[10:5], scanout_pixel[10:9]};
wire [7:0] scanout_b8 = {scanout_pixel[4:0], scanout_pixel[4:2]};
gamma_correction
#(
	.DATA_WIDTH		(8),
	.GAMMA_CURVE	("./rtl/gamma_conrrection/2P2.mem")
)
inst_gamma_correction
(
	.i_pclk		(hdmi_tx_slow_clk),
	.i_rstn		( hdmi_rst_n ),
	
	.i_red		(fb_r8),//(con_rdata  ),//
	.i_green	(fb_g8),//(con_gdata  ),//
	.i_blue		(fb_b8),//(con_bdata  ),//
  .i_valid	(q3_overlay_de  ),//(con_de     ),//
  .i_hs     (q3_overlay_hs   ),//(con_hs     ),//
  .i_vs     (q3_overlay_vs   ),//(con_vs     ),//
	.o_blue		(gamma_bdata		),
  .o_green	(gamma_gdata	  ),
	.o_red		(gamma_rdata		),
	.o_valid	(gamma_valid	  ),
  .o_hs     (gamma_hs       ),
  .o_vs     (gamma_vs       )
);

dvi_encoder dvi_encoder_m0
(
	.pixelclk      		(hdmi_tx_slow_clk          ),// system clock
	.rst_p         		(~hdmi_rst_n      ),// reset
	.i_bdata      		(gamma_bdata),//(hdmi_tx_rdata ),//(video_b            ),//(bdata_in	),   //
	.i_gdata     		  (gamma_gdata),//(hdmi_tx_gdata ),//(video_g            ),//(gdata_in	),   //   
	.i_rdata       		(gamma_rdata),//(hdmi_tx_bdata ),//(video_r            ),//(rdata_in	),   //   
  .i_de            	(gamma_valid),//(hdmi_tx_de    ),//(video_de           ),//(rx_de		  ),   //
	.i_hs         		(gamma_hs),//(video_hs           ),//(rx_hsync	),   //   
	.i_vs         		(gamma_vs),//(video_vs           ),//(rx_vsync	),   //  
	
  .video_format              (2'b00),
  .video_VIC                 (7'd16),
  .audio_L                   (24'd0),
  .audio_R                   (24'd0),
  .audio_valid               (1'b0),
  .audio_N                   (6144),
  .audio_CTS                 (148500),
  .audio_sample_frequency    (4'd0),
  .audio_word_length         (4'd0),

	.tmds_data0    		(tmds_data0),
  .tmds_data1    		(tmds_data1),
  .tmds_data2    		(tmds_data2),
  .tmds_clk      	  (tmds_clk  )
);
assign tmds_tx_clk_TX_DATA   = ~tmds_clk;
assign tmds_tx_data0_TX_DATA = ~tmds_data0;
assign tmds_tx_data1_TX_DATA = ~tmds_data1;
assign tmds_tx_data2_TX_DATA = ~tmds_data2;


wire [7:0] r_gain;
wire [7:0] g_gain;
wire [7:0] b_gain;
wire [1:0] gamma_sel;
// USER1 TDO is owned by the RISC-V SoC; Efinity auto debug remains on USER2.
wire debug_edb_user1_tdo_unused;
debug_edb_top debug_edb_top_inst (
    .bscan_CAPTURE      ( jtag_inst1_CAPTURE  ),
    .bscan_DRCK         ( jtag_inst1_DRCK     ),
    .bscan_RESET        ( jtag_inst1_RESET    ),
    .bscan_RUNTEST      ( jtag_inst1_RUNTEST  ),
    .bscan_SEL          ( jtag_inst1_SEL      ),
    .bscan_SHIFT        ( jtag_inst1_SHIFT    ),
    .bscan_TCK          ( jtag_inst1_TCK      ),
    .bscan_TDI          ( jtag_inst1_TDI      ),
    .bscan_TMS          ( jtag_inst1_TMS      ),
    .bscan_UPDATE       ( jtag_inst1_UPDATE   ),
    .bscan_TDO          ( debug_edb_user1_tdo_unused ),
    .vio0_clk           ( vid_clk_dvi2        ),
    .vio0_r_gain        ( r_gain              ),
    .vio0_g_gain        ( g_gain              ),
    .vio0_b_gain        ( b_gain              ),

    .vio0_gamma_sel     (gamma_sel),
    .la0_clk            ( vid_clk_dvi2        ),
    .la0_ch0_g          ( ch0_g               ),
    .la0_ch0_b          ( ch0_b               ),
    .la0_ch0_de         ( ch0_de              ),
    .la0_cal_done       ( cal_done            )
);



// END TEMP_BG_PERF_OBSERVER

assign command_fifo_full = 0;
assign command_fifo_empty = 1;
assign command_fifo_level = 0;
assign command_fifo_head_data = 0;
assign renderer_wr_start_offset = 0;
assign renderer_wr_start_addr = 0;
assign renderer_wr_len = 0;
assign renderer_wr_start = 0;
assign renderer_vs = 0;
assign renderer_hs = 0;
assign renderer_de = 0;
assign renderer_vin = 0;
assign rect_renderer_done = 0;
assign block_copy_req = 0;
assign block_ack_test = 0;
assign block_copy_done = 0;
assign block_copy_rd_sw = 0;
assign block_copy_wr_sw = 0;
assign block_copy_ddr_wr_error = 0;
assign block_axi_awid = 0;
assign block_axi_awaddr = 0;
assign block_axi_awlen = 0;
assign block_axi_awsize = 0;
assign block_axi_awburst = 0;
assign block_axi_awcache = 0;
assign block_axi_awprot = 0;
assign block_axi_awlock = 0;
assign block_axi_awvalid = 0;
assign block_axi_awcobuf = 0;
assign block_axi_awapcmd = 0;
assign block_axi_awallstrb = 0;
assign block_axi_awqos = 0;
assign block_axi_wdata = 0;
assign block_axi_wstrb = 0;
assign block_axi_wlast = 0;
assign block_axi_wvalid = 0;
assign block_axi_bready = 1;
assign block_axi_arid = 0;
assign block_axi_araddr = 0;
assign block_axi_arlen = 0;
assign block_axi_arsize = 0;
assign block_axi_arburst = 0;
assign block_axi_arcache = 0;
assign block_axi_arprot = 0;
assign block_axi_arlock = 0;
assign block_axi_arvalid = 0;
assign block_axi_arapcmd = 0;
assign block_axi_arqos = 0;
assign block_axi_rready = 1;
assign block_copy_test_wdata = 0;
assign block_copy_test_rdata = 0;
assign block_copy_error = 0;
assign block_copy_wr_fifo_test = 0;
assign bg_dbg_status = 0;
assign active_command = 0;
assign active_engine = 0;
assign active_target_id = 0;
assign async_gpu_owner_wait_cycles_r = 0;
assign async_gpu_pair_present_seq_r = 0;
assign async_gpu_render_cycles_r = 0;
assign async_q0_present_seq_r = 0;
assign async_write_owner = 0;
assign bg_dbg_aborted = 0;
assign bg_dbg_active = 0;
assign bg_dbg_aw_stall = 0;
assign bg_dbg_b_wait = 0;
assign bg_dbg_burst_count = 0;
assign bg_dbg_cycles = 0;
assign bg_dbg_valid = 0;
assign bg_dbg_w_fire = 0;
assign bg_dbg_w_stall = 0;
assign bg_dbg_wait_fifo_cycles = 0;
assign block_dst_base = 0;
assign block_en_test = 0;
assign block_error_seen = 0;
assign block_height = 0;
assign block_src_base = 0;
assign block_width = 0;
assign color_change = 0;
assign command_prefetch = 0;
assign command_remaining = 0;
assign frame_pipeline_state = 0;
assign frame_target_mask = 0;
assign framebuffer_render_done = 0;
assign perf_block_active = 0;
assign perf_blockcopy_cycles = 0;
assign perf_blockcopy_total_cycles = 0;
assign perf_frame_active = 0;
assign perf_frame_cycles = 0;
assign perf_rect_active = 0;
assign perf_rect_count = 0;
assign perf_rect_last_cycles = 0;
assign perf_rect_total_cycles = 0;

assign pipeline_error_code = 0;
assign pipeline_error_flags = 0;
assign pipeline_error_opcode = 0;
assign probe_back_base = 0;
assign probe_bresp_count = 0;
assign probe_cycles = 0;
assign probe_done_seen = 0;
assign probe_error_flags = 0;
assign probe_front_base = 0;
assign probe_last_araddr = 0;
assign probe_last_awaddr = 0;
assign probe_last_bresp = 0;
assign probe_last_rresp = 0;
assign probe_mismatch = 0;
assign probe_read_beats = 0;
assign probe_read_data = 0;
assign probe_state = 0;
assign probe_write_beats = 0;
assign rect_current_cycles = 0;
assign renderer_start_pulse = 0;
assign wr_error_seen = 0;
endmodule
