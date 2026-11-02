//2026.8.17：V3: 
//（1）使用了通用串并转换模块，理论上可以支持任意位宽的串并，并串转换。

`timescale 1ps/1ps
module frame_buffer #(
parameter RUNTIME_BURST = 0,
parameter I_VID_WIDTH = 16,
parameter O_VID_WIDTH = 16,
parameter AXI_DATA_WIDTH	= 512,
parameter AXI_ADDR_WIDTH 	= 33,
parameter	WR_FIFO_DEPTH		= 1024,    
parameter	RD_FIFO_DEPTH 	= 1024,
parameter START_ADDR			=	33'h000201900,
parameter BURST_LEN       = 8'd15,
parameter FB_NUM			    = 	3,//2 buffer ,3 buffer 
parameter MAX_VID_WIDTH		=	1920 ,//video width 
parameter MAX_VID_HIGHT		=	1080 ,//wideo height
parameter AXI_STRB_WIDTH 	= AXI_DATA_WIDTH/8
) (
input wire single_beat_mode,

input 												axi_clk,
input 												rst_n,
input	wire	wr_start,
input	wire								i_clk	,
input	wire								pixel_valid, //active hgih
output	wire								pixel_ready, //active high
input	wire	[I_VID_WIDTH-1:0] 			vin ,
input wire [32:0] wr_start_addr,
input wire frame_start_in,
input wire [23:0] wr_len,
input wire solid_fill_enable,
input wire [15:0] solid_fill_color,
input	wire								  o_clk	,
output	wire								o_hs , //active high
output	wire								o_vs , //active hgih
output	wire								o_de , //active high
output	wire	[O_VID_WIDTH-1:0] 			vout ,
input  wire [2:0]      start_pixel_offset,
input	wire 	[12:0]									H_FRONT_PORCH 	,
input	wire 	[12:0]									H_SYNC 			,
input	wire 	[12:0]									H_VALID 		,
input	wire 	[12:0]									H_BACK_PORCH 	,
input	wire 	[12:0]									V_FRONT_PORCH 	,
input	wire 	[12:0]									V_SYNC 			,
input	wire 	[12:0]									V_VALID 		,
input	wire 	[12:0]									V_BACK_PORCH 	,
output reg tx_underflow_seen,
input wire render_done,
input wire use_external_buffers,
input wire [31:0] external_front_buffer_base,
input wire [31:0] external_back_buffer_base,
input wire external_swap_pulse,
input wire quad_scanout_enable,
input wire [31:0] quad_front_q0,
input wire [31:0] quad_front_q1,
input wire [31:0] quad_front_q2,
input wire [31:0] quad_front_q3,
output wire swap_pulse,
input wire init_bank_sel,
input wire init_write_complete,
output 	[3:0] 								awid,
output  [AXI_ADDR_WIDTH-1:0] 	awaddr,
output  [7:0] 								awlen,
output  [2:0] 								awsize,
output  [1:0] 								awburst,
output  [3:0] 								awcache,
output [2:0]                    awprot,
output  									    awlock,
output  									    awvalid,
output  									    awcobuf,
output  									    awapcmd,
output  									    awallstrb,
output  									    awqos,
input 										    awready,

output [3:0] 									arid,
output  [AXI_ADDR_WIDTH-1:0] 	                araddr,
output  [7:0] 								    arlen,
output  [2:0] 								    arsize,
output  [1:0] 								    arburst,
output  									    arlock,
output  									    arvalid,
output  									    arapcmd,
output 										    arqos,
input 										    arready,
output  [3:0] 									arcache,
output [2:0]                    arprot,

output  [AXI_DATA_WIDTH-1:0] 	wdata,  
output  [AXI_STRB_WIDTH-1:0] 	wstrb,
output  											wlast,
output  											wvalid,
input 												wready,

input [3:0] 									    rid,
input [AXI_DATA_WIDTH-1:0] 		rdata,
input 												rlast,
input 												rvalid,
output  											rready,
input [1:0] 									    rresp,

input [3:0] 									    bid,
input 												bvalid,
output  											bready,
output			[31:0]								test_rd_fifo_rddata,   
output	[31:0]								        test_wdata,
output	[31:0]								        test_rdata,

input  [1:0] s_axi_bresp,
output ddr_wr_done,
output ddr_wr_error,
input wire cpu_write_select,
input wire cpu_fifo_rst_p,
input wire cpu_fifo_wr_en,
input wire [143:0] cpu_fifo_wr_data,
input wire cpu_ddr_wr_start,
input wire [32:0] cpu_wr_start_addr_absolute,
input wire [23:0] cpu_wr_len_beats,
output wire cpu_fifo_full,
output wire cpu_ddr_wr_done,
output wire cpu_ddr_wr_error,

// Fixed asset uploader shares this writer and FIFO while the display reader
// remains untouched.  Asset writes never assert render_done/present.
input wire asset_write_select,
input wire asset_fifo_rst_p,
input wire asset_fifo_wr_en,
input wire [143:0] asset_fifo_wr_data,
input wire asset_ddr_wr_start,
input wire [32:0] asset_wr_start_addr,
input wire [23:0] asset_wr_len_beats,
output wire asset_fifo_full,
output wire asset_ddr_wr_done,
output wire asset_ddr_wr_error,
output wire asset_ddr_wr_idle,
output wire asset_ddr_wr_accept,
output wire asset_ddr_fifo_empty,

// Scanout acceptance contract for asynchronous surface commits.
output wire rd_quiescent,
output wire descriptor_accept,
input wire descriptor_release,
output wire read_error,
output wire [AXI_ADDR_WIDTH-1:0] descriptor_start_addr_debug,

// TEMP_BG_PERF: passive writer observation passthrough.
output wire bg_dbg_wait_fifo_insufficient,
output [7:0] test_BURST_LEN
);
assign test_BURST_LEN = BURST_LEN;

wire [31:0] manager_front_buffer_base;
wire [31:0] manager_back_buffer_base;
wire [31:0] front_buffer_base;
wire [31:0] back_buffer_base;
wire [31:0] scanout_start_addr;

assign front_buffer_base = use_external_buffers ?
    external_front_buffer_base : manager_front_buffer_base;

assign back_buffer_base = use_external_buffers ?
    external_back_buffer_base : manager_back_buffer_base;

assign scanout_start_addr = quad_scanout_enable ?
    32'd0 : front_buffer_base;
//=============================================================
//parameter define                                                  
//============================================================= 
wire [31:0]               ddr_frame_len ;
wire [AXI_DATA_WIDTH-1:0] wr_fifo_wrdata;
wire ddr_wr_idle;
wire ddr_wr_accept;
wire ddr_fifo_empty;

wire                      rd_fifo_rdvalid	;
wire [AXI_DATA_WIDTH-1:0] rd_fifo_rddata	 ; 
wire                      rd_fifo_rdempty	;
wire                      rd_fifo_rden     ;

wire                      fifo_rd_period;

reg [12:0] h_front_porch  = 13'd100; 
reg [12:0] h_sync 	 		= 13'd100; 
reg [12:0] h_valid 	 	 	= 13'd100; 
reg [12:0] h_back_porch = 13'd100; 
reg [12:0] v_front_porch  = 13'd100; 
reg [12:0] v_sync 	 		= 13'd100; 
reg [12:0] v_valid 	 	  = 13'd100; 
reg [12:0] v_back_porch = 13'd100; 

wire		wr_sw_ack ;
wire		wr_sw 		;
wire		rd_sw			;
wire		rd_sw_ack ;

//=============================================================  
//RTL                                                     
//=============================================================  
wire wr_clk_rst_n;
wire axi_clk_rst_n;
wire rd_clk_rst_n;
rst_n_piple #(                       
	.DLY ( 3 )                        
)	u_i_clk_rst_pip(                                          
/*i*/.clk			(i_clk),                        
/*i*/.rst_n_i	(rst_n),                    
/*o*/.rst_n_o (wr_clk_rst_n)            
);

  rst_n_piple #(                       
	.DLY ( 3 )                        
)	u_axi_rst_pip(                                          
/*i*/.clk			(axi_clk),                        
/*i*/.rst_n_i	(rst_n),                    
/*o*/.rst_n_o (axi_clk_rst_n)            
);

  rst_n_piple #(                       
	.DLY ( 3 )                        
)	u_o_clk_rst_pip(                                          
/*i*/.clk			(o_clk),                        
/*i*/.rst_n_i	(rst_n),                    
/*o*/.rst_n_o (rd_clk_rst_n)            
);


wire                      wr_fifo_wren;
wire                      frame_start;
wire                      frame_stable;

/*============================================================
    ddr_write_packer signals
============================================================*/

wire                    packer_ready;
wire                    packer_vs_ready;

wire                    packer_fifo_wr_en;
wire [127:0]            packer_fifo_wr_data;

wire                    packer_frame_start;
wire                    packer_fifo_rst_p;
wire [23:0]             packer_ddr_len;
wire [143:0]            fifo;
wire                    wr_fifo_wrfull;
assign pixel_ready = packer_ready;
assign cpu_fifo_full = wr_fifo_wrfull;
/*============================================================
    ddr_write_packer
============================================================*/

ddr_write_packer
#(
    .I_VID_WIDTH       (16),
    .AXI_DATA_WIDTH    (128)
)
u_ddr_write_packer
(
    .clk               (i_clk),
    .rst_n             (wr_clk_rst_n),

    // 工作使能
    .start             (wr_start),
    .solid_fill_enable (solid_fill_enable),
    .solid_fill_color  (solid_fill_color),
    .start_pixel_offset(start_pixel_offset),
    // 16bit RGB565输入
    .wr_fifo_rddata    (vin),
    .i_de              (pixel_valid),
    .valid             (pixel_valid),
    .ready             (packer_ready),
    .fifo_full          (wr_fifo_wrfull),
    // 本次需要写入的pixel数量
    .wr_len            (wr_len),

    // 帧开始握手
    .vs_valid          (frame_start_in),
    .vs_ready          (packer_vs_ready),

    // 输出到DDR写FIFO
    .fifo_wr_en        (packer_fifo_wr_en),
    .fifo_wr_data      (packer_fifo_wr_data),
    
    .fifo              (fifo),
    // DDR buffer控制
    .frame_start       (packer_frame_start),
    .fifo_rst_p        (packer_fifo_rst_p),
    .ddr_len           (packer_ddr_len)
);


wire [7:0] test_cnt;
`include "rtl/frame_read_boundary.vh"

/*  @brief 计算真实的写地址 */
wire [31:0] wr_addr_real;
wire fb_wr_addr_absolute;
assign fb_wr_addr_absolute = ~init_write_complete;

assign wr_addr_real =
        fb_wr_addr_absolute ?
        wr_start_addr[31:0] :
        (back_buffer_base + wr_start_addr[31:0]);

wire selected_asset = asset_write_select;
wire selected_cpu = !selected_asset && cpu_write_select;
wire selected_wr_start = selected_asset ? asset_ddr_wr_start :
                          (selected_cpu ? cpu_ddr_wr_start : wr_start);
wire [32:0] selected_wr_addr = selected_asset ? asset_wr_start_addr :
        (selected_cpu ? cpu_wr_start_addr_absolute : {1'b0,wr_addr_real});
wire [23:0] selected_wr_len = selected_asset ? asset_wr_len_beats :
                              (selected_cpu ? cpu_wr_len_beats : packer_ddr_len);
wire selected_fifo_rst = selected_asset ? asset_fifo_rst_p :
                         (selected_cpu ? cpu_fifo_rst_p : packer_fifo_rst_p);
wire selected_fifo_wren = selected_asset ? asset_fifo_wr_en :
                          (selected_cpu ? cpu_fifo_wr_en : packer_fifo_wr_en);
wire [143:0] selected_fifo_data = selected_asset ? asset_fifo_wr_data :
                                  (selected_cpu ? cpu_fifo_wr_data : fifo);
assign cpu_ddr_wr_done = selected_cpu ? ddr_wr_done : 1'b0;
assign cpu_ddr_wr_error = selected_cpu ? ddr_wr_error : 1'b0;
assign asset_ddr_wr_done = selected_asset ? ddr_wr_done : 1'b0;
assign asset_ddr_wr_error = selected_asset ? ddr_wr_error : 1'b0;
assign asset_ddr_wr_idle = ddr_wr_idle;
assign asset_ddr_wr_accept = ddr_wr_accept;
assign asset_ddr_fifo_empty = ddr_fifo_empty;
assign asset_fifo_full = wr_fifo_wrfull;
// Capture the command geometry at the same edge as wr_start.  The DDR
// writer synchronizes wr_start for two cycles, so keeping these values in
// registers removes the long renderer/CPU mux path from burst arithmetic.
reg [31:0] selected_wr_addr_r;
reg [23:0] selected_wr_len_r;
always @(posedge axi_clk or negedge axi_clk_rst_n) begin
    if (!axi_clk_rst_n) begin
        selected_wr_addr_r <= 32'd0;
        selected_wr_len_r <= 24'd0;
    end else if (selected_wr_start) begin
        selected_wr_addr_r <= selected_wr_addr[31:0];
        selected_wr_len_r <= selected_wr_len;
    end
end
ddr_buffer #(
    .RUNTIME_BURST(RUNTIME_BURST),
.AXI_DATA_WIDTH ( AXI_DATA_WIDTH	),
.AXI_ADDR_WIDTH ( AXI_ADDR_WIDTH	),
.WR_FIFO_DEPTH	( WR_FIFO_DEPTH		),    
.RD_FIFO_DEPTH 	( RD_FIFO_DEPTH 	),
.START_ADDR			( START_ADDR      ),
.I_VID_WIDTH    ( I_VID_WIDTH     ),
.BURST_LEN      (BURST_LEN        ),
.WR_PAYLOAD_WIDTH (AXI_DATA_WIDTH + AXI_DATA_WIDTH/8)
)u_ddr_buffer(
    .single_beat_mode(single_beat_mode),
    .axi_clk		    (axi_clk	),
    .wr_clk_rst_n (wr_clk_rst_n),
    .axi_clk_rst_n(axi_clk_rst_n),
    .rd_clk_rst_n (rd_clk_rst_n),
    
/*i*/.rd_start	      (fifo_rd_period),
/*i*/.wr_start	      (selected_wr_start),
    .wr_busrt_len     ({8'd0, selected_wr_len_r}),// AXI beat count
    .rd_burst_len     (ddr_frame_len ),//ddr address scope for one frame
    
	   .test_cnt				(test_cnt),
/*i*/.wr_fifo_wrclk		(i_clk					),
/*i*/.wr_fifo_rst_p   ( selected_fifo_rst  ),
/*i*/.wr_fifo_wren		(selected_fifo_wren	  ),
/*o*/.wr_fifo_wrfull	(wr_fifo_wrfull	  ),
/*i*/.wr_fifo_wrdata	( selected_fifo_data ),

/*i*/.rd_fifo_rdclk		(o_clk					  ),
/*i*/.rd_fifo_rst_p   (neg_rd_period    ),
/*o*/.rd_fifo_rdvalid	(rd_fifo_rdvalid	),
/*o*/.rd_fifo_rddata	(rd_fifo_rddata	  ),
/*o*/.rd_fifo_rdempty	(rd_fifo_rdempty	),
/*i*/.rd_fifo_rden		(rd_fifo_rden     ),
     
    .rd_start_addr(scanout_start_addr),
     .wr_start_addr(selected_wr_addr_r),
    .quad_enable(quad_scanout_enable),
    .quad_base_q0(quad_front_q0),
    .quad_base_q1(quad_front_q1),
    .quad_base_q2(quad_front_q2),
    .quad_base_q3(quad_front_q3),
    .quad_stride_bytes(32'd1920),

/*o*/.test_wdata(test_wdata ),
/*o*/.test_rdata(test_rdata ),    
    .awid			  ( awid			),
    .awaddr			( awaddr		),
    .awlen			( awlen			),
    .awsize			( awsize		),
    .awburst		( awburst		),
    .awvalid		( awvalid		),
    .awready		( awready		),
    .awqos			( awqos			),
    .awcache    ( awcache   ),
    .awprot     ( awprot    ),

    .arid			  ( arid			),
    .araddr			( araddr		),
    .arlen			( arlen			),
    .arsize			( arsize		),
    .arburst		( arburst		),
    .arlock			( arlock		),
    .arvalid		( arvalid		),
    .arapcmd		( arapcmd		),
    .arready		( arready		),
    .arqos			( arqos			),
    .arprot     (arprot     ),
    .arcache    (arcache    ),
    //
    .wdata			( wdata			),
    .wstrb			( wstrb			),
    .wlast			( wlast			),
    .wvalid			( wvalid		),
    .wready			( wready		),
    //
    .rid			  ( rid			  ),
    .rdata			( rdata			),
    .rlast			( rlast			),
    .rvalid			( rvalid		),
    .rready			( rready		),
    .rresp			( rresp			),
    .bid			  ( bid			  ),
    .bvalid			( bvalid		),
    .s_axi_bresp(s_axi_bresp),

    .ddr_wr_done(ddr_wr_done),
    .ddr_wr_error(ddr_wr_error),
    .ddr_wr_idle(ddr_wr_idle),
    .ddr_wr_accept(ddr_wr_accept),
    .ddr_wr_fifo_empty(ddr_fifo_empty),
    .rd_quiescent(rd_quiescent),
    .descriptor_accept(descriptor_accept),
    .descriptor_release(descriptor_release),
    .read_error(read_error),
    .descriptor_start_addr_debug(descriptor_start_addr_debug),
    .bg_dbg_wait_fifo_insufficient(bg_dbg_wait_fifo_insufficient),
    .bready			( bready		));

//===================================================================================
//
//===================================================================================
assign frame_stable = 1'b1;
assign ddr_frame_len = 32'd259200;
wire tx_valid;
wire [23:0] tx_vin;
    par2ser_parse#(
      .VID_WIDTH 				( O_VID_WIDTH  ),
      .AXI_DATA_WIDTH   (AXI_DATA_WIDTH)
      
    )u_par2ser_parse(
    /*i*/.clk               (o_clk),
    /*i*/.rst_n             (rd_clk_rst_n),
    /*i*/.frame_period      (	fifo_rd_period),
    /*i*/.ddr_frame_len     (ddr_frame_len),
    /*i*/.rd_fifo_rdvalid   (rd_fifo_rdvalid  ),
    /*i*/.rd_fifo_rddata    (rd_fifo_rddata		),
    /*i*/.rd_fifo_rdempty   (rd_fifo_rdempty ),
    /*o*/.rd_fifo_rden      (rd_fifo_rden     ),

    /*o*/.tx_fifo_wrdata     (tx_vin),
    /*o*/.tx_fifo_valid    (tx_valid),
    /*I*/.tx_fifo_full    (tx_almost_full)
);

    



always @( posedge o_clk )
begin
    h_front_porch   <=  H_FRONT_PORCH ;
    h_sync 	 		    <=  H_SYNC 			  ;
    h_valid 	 	    <=  H_VALID 		  ;
    h_back_porch    <=  H_BACK_PORCH 	;
    v_front_porch   <=  V_FRONT_PORCH ;
    v_sync 	 		    <=  V_SYNC 			  ;
    v_valid 	 	    <=  V_VALID 		  ;
    v_back_porch    <=  V_BACK_PORCH 	;
end

reg rd_frame_en_r0 = 'd0;
reg rd_frame_en_r1 = 'd0;
always @( posedge o_clk or negedge rd_clk_rst_n )
begin
    if( !rd_clk_rst_n ) begin
      rd_frame_en_r0 <= 1'b0;
      rd_frame_en_r1 <= 1'b0;
    end else begin
      rd_frame_en_r0 <= frame_stable;
      rd_frame_en_r1 <= rd_frame_en_r0;
    end
end
wire tx_fifo_underflow;
data_tx #(
	.VID_WIDTH 				( O_VID_WIDTH  ),
	.FIFO_DIPTH  			( 1024 				 ),
	.FIFO_ALMOST_FULL	( 960          )
	
	
	)u_data_tx(
	/*i*/.clk					  (o_clk				),
	/*i*/.rst_n				  (rd_clk_rst_n	),
	/*i*/.H_FRONT_PORCH (h_front_porch),//( 50 			),
	/*i*/.H_SYNC 	 		  (h_sync 	 		),//( 50 			),
	/*i*/.H_VALID 	 	  (h_valid 	 	  ),//( 48 			),
	/*i*/.H_BACK_PORCH  (h_back_porch ),//( 50 			),
	/*i*/.V_FRONT_PORCH (v_front_porch),//( 5 			),
	/*i*/.V_SYNC 	 		  (v_sync 	 		),//( 5 			),
	/*i*/.V_VALID 	 	  (v_valid 	 	  ),//( 20 			),
	/*i*/.V_BACK_PORCH  (v_back_porch ),//( 5 			),
	/*i*/.frame_en		  (rd_frame_en_r1),//(  	frame_stable		),//(tx_frame_en	),
	/*i*/.fifo_wr_data  (tx_vin				),
	/*i*/.fifo_wr_en	  (tx_valid			),
	/*o*/.fifo_wr_almost_full	(tx_almost_full	),
	/*o*/.fifo_rd_period(fifo_rd_period),
    .fifo_rd_underflow(tx_fifo_underflow),
	/*o*/.vout		(vout		),
	/*o*/.o_hs		(o_hs		),
	/*o*/.o_vs		(o_vs		),
	/*o*/.o_de		(o_de		)
	
	);

////bank_switch


wire manager_swap_pulse;
wire double_buffer_en = 1'b1;

/*  @brief 两级信号同步 */
reg vs_sync1;
reg vs_sync2;
reg vs_sync2_d;
always @(posedge axi_clk or negedge rst_n)
begin
    if(!rst_n)
    begin
        vs_sync1   <= 1'b0;
        vs_sync2   <= 1'b0;
        vs_sync2_d <= 1'b0;
    end
    else
    begin
        vs_sync1   <= o_vs;
        vs_sync2   <= vs_sync1;
        vs_sync2_d <= vs_sync2;
    end
end

wire frame_boundary;

assign frame_boundary = vs_sync2 & ~vs_sync2_d;
framebuffer_manager #(
  .FB0_BASE(32'h0000_0000),
  .FB1_BASE(32'h0040_0000)
) framebuffer_manager_inst(
  .clk(axi_clk),
  .rst_n(rst_n),
  .double_buffer_en(double_buffer_en),
  .render_done(render_done),
  .frame_boundary(frame_boundary),
  .front_buffer_base(manager_front_buffer_base),
  .back_buffer_base(manager_back_buffer_base),
  .swap_pulse(manager_swap_pulse)
);

assign swap_pulse = use_external_buffers ? external_swap_pulse : manager_swap_pulse;


always @(posedge o_clk or negedge rd_clk_rst_n)
begin
    if(!rd_clk_rst_n)
        tx_underflow_seen <= 1'b0;
    else if(tx_fifo_underflow)
        tx_underflow_seen <= 1'b1;
end

/*----------------------------------------------------------------------------------*\
                                 The function code
\*----------------------------------------------------------------------------------*/
function integer clogb2;
input [31:0] value;
begin
value = value - 1;
for (clogb2 = 0; value > 0; clogb2 = clogb2 + 1)
value = value >> 1;
end
endfunction


endmodule
