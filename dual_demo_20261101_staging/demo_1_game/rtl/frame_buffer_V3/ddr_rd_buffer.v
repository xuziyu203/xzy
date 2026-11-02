
`timescale 1ps/1ps
module ddr_rd_buffer #(
parameter AXI_DATA_WIDTH = 512, //!AXI鎺ュ彛浣嶅
parameter AXI_ADDR_WIDTH = 33,  //!AXI鍦板潃浣嶅
parameter RD_FIFO_DEPTH = 1024, //!read fifo depth
// Only the scanout wrapper opts in; BLIT keeps the increment2 reader.
parameter SCANOUT_SAFE = 0,
parameter [7:0] BURST_LEN = 8'd15

) (

input 								            axi_clk,
input 								            axi_clk_rst_n,
input 								            start,//!鐢靛钩鏈夋晥銆?

input		[AXI_ADDR_WIDTH-1:0]	            start_addr,
input		[24:0]								burst_len,
input wire                                     quad_enable,
input wire                                     boot_read_enable, // core domain, cold boot only
input wire                                     pixel_rst_n,
output wire                                    pixel_scanout_started,
input wire [31:0]                              quad_base_q0,
input wire [31:0]                              quad_base_q1,
input wire [31:0]                              quad_base_q2,
input wire [31:0]                              quad_base_q3,
input wire [31:0]                              quad_stride_bytes,
input wire                                     bank_sw_ack	,
output  wire                                     bank_sw 		  ,

input											rd_fifo_rdclk,
input                                           rd_fifo_rst_p,
output	reg										rd_fifo_rdvalid ='d0,
output	 [AXI_DATA_WIDTH-1:0] 				    rd_fifo_rddata,
output wire										rd_fifo_rdempty,
input	 wire									rd_fifo_rden,

output [3:0] 									    arid,
output  [AXI_ADDR_WIDTH-1:0] 	                    araddr,
output  [8-1:0] 						            arlen,
output  [2:0] 										arsize,
output  [1:0] 										arburst,
output  											arlock,
output reg 											arvalid,
output  											arapcmd,
output 												arqos,
input 												arready,
output  [3:0] 								        arcache,
output [2:0]                                        arprot,

input [3:0] 									rid,
input [AXI_DATA_WIDTH-1:0] 						rdata,
input 											rlast,
input 											rvalid,
output  reg										rready,
input [1:0] 									rresp,
output[31:0]									wr_fifo_rd_data_test,
output reg                                      rd_quiescent,
output reg                                      descriptor_accept,
output reg                                      read_error,
output reg [AXI_ADDR_WIDTH-1:0]              descriptor_start_addr_debug,
output [7:0] test_BURST_LEN
);
generate if(SCANOUT_SAFE) begin: safe_reader
assign test_BURST_LEN = BURST_LEN;
localparam AXSIZE_WTH = $clog2(AXI_DATA_WIDTH/8);//!鍐呴儴鏁版嵁
localparam RD_USEDW_WITH = $clog2(RD_FIFO_DEPTH) ;
localparam ADDR_SHIFT_BITS = $clog2(AXI_DATA_WIDTH/8);
localparam [8:0] MAX_BURST_BEATS =
        {1'b0, BURST_LEN} + 9'd1;
reg 										ddr_rd_valid;                      
reg [AXI_DATA_WIDTH-1:0] 					ddr_rd_data;     
wire [RD_USEDW_WITH   :0]					rd_fifo_wrusedw;   
wire [RD_USEDW_WITH   :0] 					rd_fifo_rdusedw; 
reg	 [AXI_ADDR_WIDTH-1:0] 					ddr_addr	= 'd0;  
wire										rd_fifo_wr_full;
reg	[8-1:0]					                ddr_arlen = 'd0; 
reg			[2:0]							start_sync = 'd0;
reg	[24:0]									burst_len_r = 'd0;
reg [AXI_ADDR_WIDTH-1:0] 					start_addr_r = 'd0;
reg                                         sync_r0 = 'd0;
reg                                         sync_r1 = 'd0;
reg                                         sync_r2 = 'd0;
reg [24:0]                                  remaining_beats = 'd0;
reg [8:0]                                   burst_beats_r = 'd0;
reg                                         last_burst_r = 'd0;
reg                                         quad_enable_r = 1'b0;
reg [31:0]                                  quad_base_q0_r = 32'd0;
reg [31:0]                                  quad_base_q1_r = 32'd0;
reg [31:0]                                  quad_base_q2_r = 32'd0;
reg [31:0]                                  quad_base_q3_r = 32'd0;
reg [31:0]                                  quad_stride_r = 32'd1920;
reg [10:0]                                  quad_row_r = 11'd0;
reg                                         quad_side_r = 1'b0;
reg [24:0]                                  quad_segment_remaining = 25'd0;
localparam [24:0] QUAD_SEGMENT_BEATS = 25'd120;
reg [1:0] state = 2'd0;
reg descriptor_pending = 1'b0;
reg drain_pending, read_outstanding;
wire descriptor_take = boot_read_enable && (state==0) && (sync_r2 || descriptor_pending) &&
                        bank_sw_ack && rd_quiescent && !read_error && !rd_fifo_rst_p;

wire pos_start;
always @( posedge axi_clk or negedge axi_clk_rst_n )
begin
	if( !axi_clk_rst_n )			start_sync <= 'd0;
	else 				    start_sync <= {start_sync[1:0],start};
end

assign pos_start = start_sync[1:0] == 2'b01;
always @( posedge axi_clk or negedge axi_clk_rst_n )
begin
	if( !axi_clk_rst_n ) begin
		sync_r0 <= 1'b0;
		sync_r1 <= 1'b0;
		sync_r2 <= 1'b0;
	end else begin 
		sync_r0 <= pos_start;
		sync_r1 <= sync_r0;
		sync_r2 <= sync_r1;
	end 
end

// Snapshot all descriptor fields at the acceptance edge, after release.
always @(posedge axi_clk or negedge axi_clk_rst_n) begin
  if(!axi_clk_rst_n) begin
    burst_len_r<=0; start_addr_r<=0; quad_enable_r<=0;
    descriptor_start_addr_debug<=0;
    quad_base_q0_r<=0;quad_base_q1_r<=0;quad_base_q2_r<=0;quad_base_q3_r<=0;quad_stride_r<=1920;
  end else if(descriptor_take) begin
    burst_len_r<=burst_len;start_addr_r<=start_addr;
    descriptor_start_addr_debug<=start_addr; quad_enable_r<=quad_enable;
    quad_base_q0_r<=quad_base_q0;quad_base_q1_r<=quad_base_q1;
    quad_base_q2_r<=quad_base_q2;quad_base_q3_r<=quad_base_q3;quad_stride_r<=quad_stride_bytes;
  end
end

// Current address capacity before the next 4 KiB boundary, in AXI beats.
wire [12:0] bytes_to_4k =
        13'd4096 - {1'b0, ddr_addr[11:0]};

wire [12:0] beats_to_4k =
        bytes_to_4k >> ADDR_SHIFT_BITS;

wire [24:0] max_burst_beats_ext =
        {{16{1'b0}}, MAX_BURST_BEATS};

wire [24:0] beats_to_4k_ext =
        {{12{1'b0}}, beats_to_4k};

wire [24:0] burst_limit_0 =
        (remaining_beats < max_burst_beats_ext) ?
            remaining_beats : max_burst_beats_ext;

wire [24:0] burst_limit_quad =
        (quad_segment_remaining < burst_limit_0) ?
            quad_segment_remaining : burst_limit_0;

// Actual beats for this burst: remaining task, AXI maximum and 4 KiB limit.
wire [24:0] burst_beats_calc =
        ((quad_enable_r ? burst_limit_quad : burst_limit_0) < beats_to_4k_ext) ?
            (quad_enable_r ? burst_limit_quad : burst_limit_0) : beats_to_4k_ext;

wire [AXI_ADDR_WIDTH-1:0] burst_addr_inc =
        ({{(AXI_ADDR_WIDTH-9){1'b0}}, burst_beats_r})
        << ADDR_SHIFT_BITS;


assign arapcmd  = 1'b0;  
assign arlock   = 1'b0;  
assign arqos    = 1'b0; 
assign arid     = 4'h0;
assign arsize   = AXSIZE_WTH;
assign arlen	= ddr_arlen;
assign arburst  = 2'b01;
assign araddr   = ddr_addr;    
assign arprot       = 3'd2;
assign arcache 		= 4'd3; 
// assign rready   = ~rd_fifo_wr_full & start ;


always @(posedge axi_clk or negedge axi_clk_rst_n )
begin
	if( !axi_clk_rst_n )									ddr_rd_valid <= 1'b0;
	else ddr_rd_valid <= rvalid && rready && read_outstanding && !drain_pending && !rd_fifo_rst_p && !read_error; // Enqueue only accepted AXI R beats.
end 			

always @( posedge axi_clk )
begin
    ddr_rd_data <= rdata;
end

//==========================================================================================
reg rd_req = 0;
reg ddr_read_stop = 'd0;
assign bank_sw = ddr_read_stop;
reg [8:0] response_remaining;
reg fifo_clear, fifo_clear_done;
reg [3:0] fifo_clear_count;
reg [3:0] fifo_rd_settle;
reg fifo_rd_ready_local;
// Moved from frame_buffer.rd_frame_en_r0, with the identical pixel reset.
reg pixel_scanout_started_r = 1'b0;
always @(posedge rd_fifo_rdclk or negedge pixel_rst_n) begin
  if(!pixel_rst_n) pixel_scanout_started_r <= 1'b0;
  else pixel_scanout_started_r <= pixel_scanout_started_r || fifo_rd_ready_local;
end
assign pixel_scanout_started = pixel_scanout_started_r;
reg fifo_rd_ready0, fifo_rd_ready1;
wire r_fire = rvalid && rready;
wire bad_r_beat = r_fire && (!read_outstanding || rid!=4'd0 || rresp!=2'b00 ||
                           (rlast != (response_remaining==9'd1)));
// A frame boundary is a drain request, never an AXI transaction reset.
// The real FIFO's asynchronous reset is released before waiting >=8 edges in
// BOTH domains (its internal cross-domain reset handshake takes fewer edges).
always @(posedge rd_fifo_rdclk or posedge fifo_clear or negedge axi_clk_rst_n) begin
  if(!axi_clk_rst_n || fifo_clear) begin
    fifo_rd_settle<=0;
    fifo_rd_ready_local<=1'b0;
  end else if(fifo_rd_settle!=8) begin
    fifo_rd_settle<=fifo_rd_settle+1'b1;
    if (fifo_rd_settle==7)
      fifo_rd_ready_local<=1'b1;
  end
end
always @(posedge axi_clk or negedge axi_clk_rst_n) begin
  if(!axi_clk_rst_n) begin
    drain_pending<=1; fifo_clear<=1; fifo_clear_done<=0; fifo_clear_count<=0;
    fifo_rd_ready0<=0; fifo_rd_ready1<=0;
    read_outstanding<=0;response_remaining<=0;read_error<=0;
  end else begin
    fifo_rd_ready0 <= fifo_rd_ready_local; fifo_rd_ready1<=fifo_rd_ready0;
    if(rd_fifo_rst_p) drain_pending<=1;
    if(descriptor_take) begin drain_pending<=0;fifo_clear_done<=0;end
    if(bad_r_beat || (rvalid && !read_outstanding && state!=2'd2)) read_error<=1;
    if(arvalid && arready) begin read_outstanding<=1;response_remaining<=burst_beats_r;end
    if(r_fire && read_outstanding) begin
      if(response_remaining!=0) response_remaining<=response_remaining-1'b1;
      // Early RLAST cannot erase the remaining beats promised by accepted AR.
      // Missing final RLAST waits for a terminator; there is no stall timeout.
      if(rlast && response_remaining<=1) read_outstanding<=0;
    end
    if(!boot_read_enable) begin
      fifo_clear<=1;fifo_clear_count<=0;fifo_clear_done<=0;
    end else if(fifo_clear) begin
      if(fifo_clear_count==3) begin fifo_clear<=0;fifo_clear_count<=0;end
      else fifo_clear_count<=fifo_clear_count+1'b1;
    end else if(drain_pending && !arvalid && !read_outstanding && !ddr_rd_valid) begin
      if(!fifo_clear_done) begin
        if(fifo_clear_count==0 && !fifo_rd_ready1) fifo_clear_count<=1;
        else if(fifo_clear_count!=0 && fifo_clear_count<8) fifo_clear_count<=fifo_clear_count+1'b1;
        else if(fifo_clear_count==8 && fifo_rd_ready1) begin
          fifo_clear_done<=1;fifo_clear_count<=0;
        end else if(fifo_clear_count==0 && fifo_rd_ready1) begin
          fifo_clear<=1;fifo_clear_count<=0;
        end
      end
    end
  end
end

always @( posedge axi_clk or negedge axi_clk_rst_n )
begin
    if( !axi_clk_rst_n )
        rd_req <= 1'b0;
    else if(RD_FIFO_DEPTH < rd_fifo_wrusedw + MAX_BURST_BEATS + 9'd5)
        rd_req <= 1'b0;
    else 
        rd_req <= 1'b1;
end


always @( posedge axi_clk or negedge axi_clk_rst_n )
begin
    if( !axi_clk_rst_n ) begin
        arvalid <= 1'b0;
        rready <= 1'b0;
        state <= 'd0;
        ddr_addr <= {AXI_ADDR_WIDTH{1'b0}};
        remaining_beats <= 25'd0;
        burst_beats_r <= 9'd0;
        ddr_arlen <= 8'd0;
        last_burst_r <= 1'b0;
        ddr_read_stop <= 1'b0;
        descriptor_pending <= 1'b0;
		rd_quiescent <= 1'b0;
		descriptor_accept <= 1'b0;
        quad_row_r <= 11'd0;
        quad_side_r <= 1'b0;
        quad_segment_remaining <= 25'd0;
    end else begin
        rready <= read_outstanding || read_error;
        if (sync_r2)
            descriptor_pending <= 1'b1;
        descriptor_accept <= descriptor_take;
        rd_quiescent <= (state==0) && !arvalid && !read_outstanding && !ddr_rd_valid &&
                         drain_pending && fifo_clear_done && !fifo_clear && !read_error && !descriptor_take;
        case(state )
        2'd0 : begin
            arvalid <= 1'b0;

            if(descriptor_take) begin
                descriptor_pending <= 1'b0;
                ddr_addr <= start_addr;
                remaining_beats <= burst_len;
                burst_beats_r <= 9'd0;
                ddr_arlen <= 8'd0;
                last_burst_r <= 1'b0;
                ddr_read_stop <= 1'b0;
                quad_row_r <= 11'd0;
                quad_side_r <= 1'b0;
                quad_segment_remaining <= QUAD_SEGMENT_BEATS;
				if (quad_enable)
					ddr_addr <= quad_base_q0;

                if(burst_len == 25'd0) begin
                    ddr_read_stop <= 1'b1;
                    state <= 2'd0;
                end else begin
                    state <= 2'd1;
                end
            end
        end
        2'd1 : begin
            if(drain_pending || rd_fifo_rst_p || read_error) begin
                state<=0;ddr_read_stop<=1;
            end else if(
                rd_req &&
                (remaining_beats != 25'd0) &&
                (burst_beats_calc != 25'd0)
              ) begin
                burst_beats_r <= burst_beats_calc[8:0];
                ddr_arlen <= burst_beats_calc[7:0] - 8'd1;
                last_burst_r <=
                        (remaining_beats == burst_beats_calc);
                state <= 2'd2;
                arvalid <= 1'b1;
            end
        end
        2'd2 : begin
            if(arvalid && arready) begin
                state <= 2'd3;
                arvalid <= 1'b0;
                remaining_beats <=
                        remaining_beats - burst_beats_r;

				if (quad_enable_r) begin
					if (quad_segment_remaining > burst_beats_r) begin
						quad_segment_remaining <= quad_segment_remaining - burst_beats_r;
						ddr_addr <= ddr_addr + burst_addr_inc;
					end else if (!quad_side_r) begin
						quad_segment_remaining <= QUAD_SEGMENT_BEATS;
						quad_side_r <= 1'b1;
						if (quad_row_r < 11'd540)
							ddr_addr <= quad_base_q1_r + quad_row_r * quad_stride_r;
						else
							ddr_addr <= quad_base_q3_r + (quad_row_r - 11'd540) * quad_stride_r;
					end else if (quad_row_r < 11'd1079) begin
						quad_segment_remaining <= QUAD_SEGMENT_BEATS;
						quad_side_r <= 1'b0;
						quad_row_r <= quad_row_r + 1'b1;
						if (quad_row_r < 11'd539)
							ddr_addr <= quad_base_q0_r + (quad_row_r + 1'b1) * quad_stride_r;
						else
							ddr_addr <= quad_base_q2_r + (quad_row_r - 11'd539) * quad_stride_r;
					end else begin
						quad_segment_remaining <= 25'd0;
					end
				end else begin
					ddr_addr <= ddr_addr + burst_addr_inc;
				end

            end
        end
        2'd3 : begin
            if(rvalid && rready && rlast && response_remaining<=1) begin
                if(last_burst_r || drain_pending || rd_fifo_rst_p || read_error || bad_r_beat) begin
                    last_burst_r <= 1'b0;
                    // The scanout bank may switch only after the final R
                    // beat is accepted; the final AR handshake is too early.
                    ddr_read_stop <= 1'b1;
                    state <= 2'd0;
                end else begin
                    state <= 2'd1;
                end
            end
        end
        default : begin
            state <= 2'd0;
            arvalid <= 1'b0;
            ddr_read_stop <= 1'b0;
        end
    
        endcase
end
end



	DC_FIFO
# (
  	.FIFO_MODE  ( "Normal"    	 ), //"Normal"; //"ShowAhead"
    .DATA_WIDTH ( AXI_DATA_WIDTH ),
    .FIFO_DEPTH ( RD_FIFO_DEPTH  )
  ) u_rd_fifo(   
  //System Signal
  /*i*/.Reset   (	(~axi_clk_rst_n)	|| fifo_clear		), 
  /*i*/.WrClk   (axi_clk			), 
  /*i*/.WrEn    (ddr_rd_valid && !drain_pending && !rd_fifo_rst_p && !read_error), 
  /*o*/.WrDNum  (rd_fifo_wrusedw	), 
  /*o*/.WrFull  (rd_fifo_wr_full 	), 
  /*i*/.WrData  (ddr_rd_data 		), 
  /*i*/.RdClk   (rd_fifo_rdclk		), 
  /*i*/.RdEn    (rd_fifo_rden		), 
  /*o*/.RdDNum  (rd_fifo_rdusedw	), 
  /*o*/.RdEmpty (rd_fifo_rdempty	), 
  /*o*/.RdData  (rd_fifo_rddata		)  
);

always @(posedge rd_fifo_rdclk or negedge axi_clk_rst_n) begin
    if(!axi_clk_rst_n) rd_fifo_rdvalid<=0;
    else rd_fifo_rdvalid<=rd_fifo_rden && !rd_fifo_rdempty;
end



end else begin: legacy_reader
  assign read_error=1'b0; // Unused by legacy BLIT; scanout always selects safe_reader.
  assign pixel_scanout_started = 1'b0; // legacy reader has no boot-ready protocol
  `include "rtl/frame_buffer_V3/ddr_rd_buffer_legacy.vh"
end endgenerate
endmodule
