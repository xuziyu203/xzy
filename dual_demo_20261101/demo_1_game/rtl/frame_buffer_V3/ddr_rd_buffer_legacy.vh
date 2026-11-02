// Exact increment2 reader body, retained for the existing BLIT instance.

assign test_BURST_LEN = BURST_LEN;
localparam AXSIZE_WTH = $clog2(AXI_DATA_WIDTH/8);//!内部数据
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

always @( posedge axi_clk or negedge axi_clk_rst_n )
begin
	if( !axi_clk_rst_n ) begin
			// cfg_alen_r   <= cfg_alen;
			burst_len_r<= burst_len;
			start_addr_r <= start_addr;
			quad_enable_r <= quad_enable;
			quad_base_q0_r <= quad_base_q0;
			quad_base_q1_r <= quad_base_q1;
			quad_base_q2_r <= quad_base_q2;
			quad_base_q3_r <= quad_base_q3;
			quad_stride_r <= quad_stride_bytes;
	end else begin
			burst_len_r  <= pos_start ? burst_len   : burst_len_r ;
			// cfg_alen_r   <= pos_start ? cfg_alen    :cfg_alen_r;
			start_addr_r <= pos_start ? start_addr  :start_addr_r;
			quad_enable_r <= pos_start ? quad_enable : quad_enable_r;
			quad_base_q0_r <= pos_start ? quad_base_q0 : quad_base_q0_r;
			quad_base_q1_r <= pos_start ? quad_base_q1 : quad_base_q1_r;
			quad_base_q2_r <= pos_start ? quad_base_q2 :quad_base_q2_r;
			quad_base_q3_r <= pos_start ? quad_base_q3 :quad_base_q3_r;
			quad_stride_r <= pos_start ? quad_stride_bytes :quad_stride_r;
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
	else ddr_rd_valid <= rvalid && rready; // Enqueue only accepted AXI R beats.
end 			

always @( posedge axi_clk )
begin
    ddr_rd_data <= rdata;
end

//==========================================================================================
reg [1:0] state = 2'd0;
reg rd_req = 0;
reg ddr_read_stop = 'd0;
reg descriptor_pending = 1'b0;
assign bank_sw = ddr_read_stop;

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
    end else if(rd_fifo_rst_p) begin
        state <= 2'd0;
        arvalid <= 1'b0;
        rready <= 1'b0;
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
        rready <= 1'b1;
        if (sync_r2)
            descriptor_pending <= 1'b1;
        descriptor_accept <= (state == 2'd0) &&
                             (sync_r2 || descriptor_pending) &&
                             (!ddr_read_stop || bank_sw_ack);
        rd_quiescent <= (state == 2'd0) && !arvalid && !last_burst_r &&
                        !ddr_read_stop && !descriptor_pending &&
                        !ddr_rd_valid && (rd_fifo_wrusedw == 0);
        case(state )
        2'd0 : begin
            arvalid <= 1'b0;

            if((sync_r2 || descriptor_pending) &&
               (!ddr_read_stop || bank_sw_ack)) begin
                descriptor_pending <= 1'b0;
                ddr_addr <= start_addr_r;
                remaining_beats <= burst_len_r;
                burst_beats_r <= 9'd0;
                ddr_arlen <= 8'd0;
                last_burst_r <= 1'b0;
                ddr_read_stop <= 1'b0;
                quad_row_r <= 11'd0;
                quad_side_r <= 1'b0;
                quad_segment_remaining <= QUAD_SEGMENT_BEATS;
				if (quad_enable_r)
					ddr_addr <= quad_base_q0_r;

                if(burst_len_r == 25'd0) begin
                    ddr_read_stop <= 1'b1;
                    state <= 2'd0;
                end else begin
                    state <= 2'd1;
                end
            end
        end
        2'd1 : begin
            if(
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
            if(rvalid && rready && rlast) begin
                if(last_burst_r) begin
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
  /*i*/.Reset   (	(~axi_clk_rst_n)	|| rd_fifo_rst_p		), 
  /*i*/.WrClk   (axi_clk			), 
  /*i*/.WrEn    (ddr_rd_valid		), 
  /*o*/.WrDNum  (rd_fifo_wrusedw	), 
  /*o*/.WrFull  (rd_fifo_wr_full 	), 
  /*i*/.WrData  (ddr_rd_data 		), 
  /*i*/.RdClk   (rd_fifo_rdclk		), 
  /*i*/.RdEn    (rd_fifo_rden		), 
  /*o*/.RdDNum  (rd_fifo_rdusedw	), 
  /*o*/.RdEmpty (rd_fifo_rdempty	), 
  /*o*/.RdData  (rd_fifo_rddata		)  
);

always @( posedge axi_clk )
begin
    rd_fifo_rdvalid <=   rd_fifo_rden;
end



