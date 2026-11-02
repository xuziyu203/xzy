
module par2ser_parse#(
    parameter VID_WIDTH = 24,
    parameter AXI_DATA_WIDTH	= 512
)(
    input clk,
    input rst_n,
 
    input frame_period,
    input rd_fifo_rdvalid,
    input [AXI_DATA_WIDTH-1:0]rd_fifo_rddata ,
    input rd_fifo_rdempty,
    output rd_fifo_rden,
    input [31:0] ddr_frame_len,

    output [VID_WIDTH-1:0] tx_fifo_wrdata,
    output tx_fifo_valid,
    input  tx_fifo_full

);


        // Q8 is little-byte-first in DDR. Existing serializer is MSB-first.
        wire [AXI_DATA_WIDTH-1:0] q8_reversed;
        wire [7:0] q8_code;
        generate for(genvar q=0;q<16;q=q+1)begin
          assign q8_reversed[(15-q)*8+:8]=rd_fifo_rddata[q*8+:8];
        end endgenerate
        // Decode before original 16-bit pixel FIFO; no new pixel/control latency.
        assign tx_fifo_wrdata={q8_code[7:5],q8_code[7:6],q8_code[4:2],q8_code[4:2],q8_code[1:0],q8_code[1:0],q8_code[1]};
        vid_par2ser #(
        .O_VID_WIDTH (8),
        .I_VID_WIDTH	(AXI_DATA_WIDTH) 

    )u_par2ser_128_16(
    /*i*/.clk               (clk              ),
    /*i*/.rst_n             (rst_n            ),
    /*i*/.frame_period      (frame_period     ),

    /*i*/.rd_fifo_rdvalid   (rd_fifo_rdvalid  ),
    /*i*/.rd_fifo_rddata    (q8_reversed      ),
    /*i*/.rd_fifo_rdempty   (rd_fifo_rdempty  ),
    /*o*/.rd_fifo_rden      ( rd_fifo_rden    ),
    /*o*/.tx_fifo_valid     ( tx_fifo_valid   ),
    /*i*/.wr_fifo_full      (tx_fifo_full     ),
    /*i*/.tx_fifo_wrdata    (q8_code          )
);



endmodule

