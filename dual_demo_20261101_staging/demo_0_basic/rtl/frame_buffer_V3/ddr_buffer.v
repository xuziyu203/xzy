

`timescale 1ps/1ps
module ddr_buffer #(
parameter RUNTIME_BURST = 0,
parameter AXI_DATA_WIDTH = 512, //!AXI接口位宽
parameter AXI_ADDR_WIDTH = 33,  //!AXI地址位宽
parameter WR_FIFO_DEPTH	= 1024, //!Write fifo depth   
parameter RD_FIFO_DEPTH = 1024, //!read fifo depth
parameter FB_NUM			= 	1,//2 buffer ,3 buffer   
parameter MAX_VID_WIDTH		=	1920 ,//video width 
parameter MAX_VID_HIGHT		=	1080 ,//wideo height
parameter START_ADDR				=	33'h000201900,
parameter BURST_LEN       = 8'd15,
parameter I_VID_WIDTH = 16,

parameter AXI_STRB_WIDTH = AXI_DATA_WIDTH/8,  //!总共的字节数量，= AXI_DATA_WIDTH/8
parameter WR_PAYLOAD_WIDTH =  AXI_DATA_WIDTH + AXI_STRB_WIDTH
) (
input wire single_beat_mode,

input 											axi_clk,
input                       wr_clk_rst_n,
input                       axi_clk_rst_n,
input                       rd_clk_rst_n,
input 											rd_start,
input											wr_start, // pulse signal 
input	wire		[AXI_ADDR_WIDTH-1:0]    wr_start_addr    , 

// input		[AXI_ADDR_WIDTH-1:0]				start_wr_addr,
// input		[AXI_ADDR_WIDTH-1:0]				start_rd_addr,
input 		[31:0] 								wr_busrt_len,
input 		[31:0] 								rd_burst_len,
output		[7:0] test_cnt ,

input                     wr_fifo_rst_p,
input											wr_fifo_wrclk,
input											wr_fifo_wren,
output											wr_fifo_wrfull,
input	 [WR_PAYLOAD_WIDTH-1:0]					wr_fifo_wrdata, 
input											rd_fifo_rdclk,
input                       rd_fifo_rst_p,
output											rd_fifo_rdvalid,
output	 [AXI_DATA_WIDTH-1:0] 					rd_fifo_rddata,
output wire										rd_fifo_rdempty,
input	 wire									rd_fifo_rden,
input	wire		[AXI_ADDR_WIDTH-1:0]		rd_start_addr       ,
input wire [31:0]                       quad_base_q0,
input wire [31:0]                       quad_base_q1,
input wire [31:0]                       quad_base_q2,
input wire [31:0]                       quad_base_q3,
input wire [31:0]                       quad_stride_bytes,
input wire                              quad_enable,

output 	[3:0] 									awid,
output  [AXI_ADDR_WIDTH-1:0] 					awaddr,
output  [8-1:0] 					awlen,
output  [2:0] 									awsize,
output  [1:0] 									awburst,
output  [3:0] 									awcache,
output [2:0]                    awprot,
output  										awlock,
output  										awvalid,
output  										awcobuf,
output  										awapcmd,
output  										awallstrb,
output  										awqos,
input 											awready,

output [3:0] 									arid,
output  [AXI_ADDR_WIDTH-1:0] 					araddr,
output  [8-1:0] 					arlen,
output  [2:0] 									arsize,
output  [1:0] 									arburst,
output  										arlock,
output  										arvalid,
output  										arapcmd,
output 											arqos,
input 											arready,
output  [3:0] 									arcache,
output [2:0]                    arprot,
output  [AXI_DATA_WIDTH-1:0] 					wdata,  
output	[31:0]									test_wdata,
output	[31:0]									test_rdata,
output  [AXI_STRB_WIDTH-1:0] 					wstrb,
output  										wlast,
output  										wvalid,
input 											wready,

input [3:0] 									rid,
input [AXI_DATA_WIDTH-1:0] 						rdata,
input 											rlast,
input 											rvalid,
output  										rready,
input [1:0] 									rresp,
output[31:0]									wr_fifo_rd_data_test,
input [3:0] 									bid,
input 											bvalid,
output  										bready,
input  [1:0] s_axi_bresp,
output ddr_wr_done,
output ddr_wr_error,
output wire ddr_wr_idle,
output wire ddr_wr_accept,
output wire ddr_wr_fifo_empty,
output rd_quiescent,
output descriptor_accept,
input descriptor_release,
output read_error,
output wire [AXI_ADDR_WIDTH-1:0] descriptor_start_addr_debug,
// TEMP_BG_PERF: passive writer observation passthrough.
output wire bg_dbg_wait_fifo_insufficient,
output [7:0] test_BURST_LEN
);
assign test_BURST_LEN = BURST_LEN;

wire		                        wr_sw_ack           ;
wire		                        wr_sw 		          ;
wire		                        rd_sw			          ;
wire rd_sw_ack;
reg descriptor_release_pending;
always @(posedge axi_clk or negedge axi_clk_rst_n) begin
  if(!axi_clk_rst_n) descriptor_release_pending<=0;
  else if(read_error) descriptor_release_pending<=0;
  else if(descriptor_accept) descriptor_release_pending<=0;
  else if(descriptor_release) descriptor_release_pending<=1;
end
// Grant is independent of final manager present_ack; no circular wait.
assign rd_sw_ack=descriptor_release_pending && rd_quiescent && !read_error;

//=============================================================  
//RTL                                                     
//=============================================================  



ddr_wr_buffer # (
    .RUNTIME_BURST(RUNTIME_BURST),
    .AXI_DATA_WIDTH(AXI_DATA_WIDTH),
    .AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),
    .WR_FIFO_DEPTH(WR_FIFO_DEPTH),
    .BURST_LEN (BURST_LEN        )
  )

  u_ddr_wr_buffer (//_inst
    .single_beat_mode(single_beat_mode),
    .axi_clk(axi_clk),
    .rst_n(axi_clk_rst_n),//(rst_n),
    .wr_start(wr_start),
    .start_addr(wr_start_addr),//(0),//
    .total_burst_len(wr_busrt_len),
/*i*/.bank_sw_ack	(wr_sw_ack   	),
/*o*/.bank_sw 		  (wr_sw  		 	),
    .test_cnt(test_cnt),
    .wr_fifo_rst_p(wr_fifo_rst_p),
    .wr_fifo_wrclk(wr_fifo_wrclk),
    .wr_fifo_wren(wr_fifo_wren),
    .wr_fifo_wrfull(wr_fifo_wrfull),
    .wr_fifo_wrdata(wr_fifo_wrdata),
    .awid(awid),
    .awaddr(awaddr),
    .awlen(awlen),
    .awsize(awsize),
    .awburst(awburst),
    .awcache(awcache),
    .awlock(awlock),
    .awvalid(awvalid),
    .awcobuf(awcobuf),
    .awapcmd(awapcmd),
    .awallstrb(awallstrb),
    .awprot (awprot ),
    .awqos(awqos),
    .awready(awready),
    .wdata(wdata),
    .wstrb(wstrb),
    .wlast(wlast),
    .wvalid(wvalid),
    .wready(wready),
	  .bid	( bid			),
    .bvalid	( bvalid		),
    .bresp   (s_axi_bresp),

    .wr_done (ddr_wr_done),
    .wr_error(ddr_wr_error),
    .wr_idle(ddr_wr_idle),
    .wr_accept(ddr_wr_accept),
    .wr_fifo_empty(ddr_wr_fifo_empty),
    .bg_dbg_wait_fifo_insufficient(bg_dbg_wait_fifo_insufficient),
    .bready	( bready		)
  );


assign test_wdata = wdata[32:0];
assign test_rdata = rdata[32:0];
//=============================================================================================
// FIFO RD_PROCESSING
//=============================================================================================
ddr_rd_buffer # (
    .RUNTIME_BURST(RUNTIME_BURST),
    .SCANOUT_SAFE(1),
    .AXI_DATA_WIDTH(AXI_DATA_WIDTH),
    .AXI_ADDR_WIDTH(AXI_ADDR_WIDTH),
    .RD_FIFO_DEPTH(RD_FIFO_DEPTH),
    .BURST_LEN (BURST_LEN        )
  )
  ddr_rd_buffer_inst (
    .single_beat_mode(1'b0),
    .axi_clk(axi_clk),
    .axi_clk_rst_n(axi_clk_rst_n),
    .start(rd_start),
    .start_addr(rd_start_addr),//(0),//
    .burst_len(rd_burst_len),
    .quad_enable(quad_enable),
    .quad_base_q0(quad_base_q0),
    .quad_base_q1(quad_base_q1),
    .quad_base_q2(quad_base_q2),
    .quad_base_q3(quad_base_q3),
    .quad_stride_bytes(quad_stride_bytes),
    .rd_fifo_rdclk(rd_fifo_rdclk),
    .rd_fifo_rst_p (rd_fifo_rst_p),
    .rd_fifo_rdvalid(rd_fifo_rdvalid),
    .rd_fifo_rddata(rd_fifo_rddata),
    .rd_fifo_rdempty(rd_fifo_rdempty),
    .rd_fifo_rden(rd_fifo_rden),
/*i*/.bank_sw_ack	(rd_sw_ack   	),
/*o*/.bank_sw 		  (rd_sw  		 	),
    .arid(arid),
    .araddr(araddr),
    .arlen(arlen),
    .arsize(arsize),
    .arburst(arburst),
    .arlock(arlock),
    .arvalid(arvalid),
    .arapcmd(arapcmd),
    .arqos(arqos),
    .arready(arready),
    .arprot (arprot ),
    .arcache(arcache),
    .rid(rid),
    .rdata(rdata),
    .rlast(rlast),
    .rvalid(rvalid),
    .rready(rready),
    .rresp(rresp),
    .wr_fifo_rd_data_test(wr_fifo_rd_data_test),
    .rd_quiescent(rd_quiescent),
    .descriptor_accept(descriptor_accept),
    .read_error(read_error),
    .descriptor_start_addr_debug(descriptor_start_addr_debug)
  );
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
