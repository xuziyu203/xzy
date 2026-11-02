// GHD: read-only DDR CRC-32 reader (core_clk). The CPU has no DDR read path, so the dual-ELF handoff verifies the asset bytes in
// DDR through this block: CRC-32 (zlib/IEEE, reflected 0xEDB88320, init/xorout 0xFFFFFFFF) over [base, base+length) in address
// order, equal to zlib.crc32(file) for an uploaded asset (AXI byte lane k = address+k; the asset writer packs file bytes LE).
// It uses the compositor port of enemy_read_arbiter (the HDMI port keeps its priority) and only while the PL is quiet:
//  - START is accepted only when idle_ok (no compositor owner, no asset writer owner); otherwise STATUS.refused is set.
//  - busy is exported so the top gates the compositor and asset owner grants for the whole run (no new owner can start).
//  - single outstanding burst of <= 16 beats, never crossing a 4 KiB boundary; rready is held low while a captured beat is
//    folded (4 x 32-bit per beat, 5 cycles per beat), which AXI allows; no write channel at all.
// APB (asset_reg bus, 32-bit, word aligned): 0x3E0 ABI (RO) 0x44435201, 0x3E4 BASE, 0x3E8 LENGTH (bytes; both multiples of 16,
// 1 <= length, base+length <= 0x04000000), 0x3EC CONTROL (write 1 = start), 0x3F0 STATUS {.., refused[3], error[2], done[1],
// busy[0]}, 0x3F4 CRC (valid when done), 0x3F8 bytes folded, 0x3FC completed runs. error = parameter error or RRESP != OKAY.
module ddr_crc_reader (
 input wire clk, rst_n,
 input wire [11:0] reg_addr, input wire reg_write, input wire [31:0] reg_wdata, output reg [31:0] reg_rdata,
 input wire idle_ok,
 output reg busy,
 output reg [31:0] araddr, output reg [7:0] arlen, output reg arvalid, input wire arready,
 input wire [127:0] rdata, input wire [1:0] rresp, input wire rlast, input wire rvalid, output wire rready
);
 localparam [31:0] ABI = 32'h44435201;
 localparam S_IDLE = 2'd0, S_AR = 2'd1, S_R = 2'd2;
 reg [1:0] state;
 reg [31:0] base, length, cur, left, crc, folded, runs;
 reg done, error, refused;
 reg [127:0] beat; reg beat_valid; reg [1:0] lane; reg [4:0] burst_left;
 function [31:0] crc32_word; input [31:0] c; input [31:0] d; integer i; reg [31:0] x; begin
  x = c ^ d;
  for (i = 0; i < 32; i = i + 1) x = x[0] ? (x >> 1) ^ 32'hEDB88320 : (x >> 1);
  crc32_word = x;
 end endfunction
 wire [31:0] lane_word = lane == 2'd0 ? beat[31:0] : lane == 2'd1 ? beat[63:32] : lane == 2'd2 ? beat[95:64] : beat[127:96];
 // burst length: min(16 beats, beats left, beats to the next 4 KiB boundary)
 wire [8:0] to_page = 9'd256 - {1'b0, cur[11:4]};
 wire [31:0] beats_left = {4'd0, left[31:4]};
 wire [4:0] burst = (beats_left < 32'd16 && beats_left[8:0] < to_page) ? beats_left[4:0] : (to_page < 9'd16 ? to_page[4:0] : 5'd16);
 wire start = reg_write && reg_addr == 12'h3EC && reg_wdata == 32'd1;
 wire params_ok = base[3:0] == 4'd0 && length[3:0] == 4'd0 && length != 32'd0 && base < 32'h04000000 && length <= 32'h04000000 - base;
 assign rready = state == S_R && !beat_valid;
 always @* begin
  case (reg_addr)
   12'h3E0: reg_rdata = ABI;
   12'h3E4: reg_rdata = base;
   12'h3E8: reg_rdata = length;
   12'h3F0: reg_rdata = {28'd0, refused, error, done, busy};
   12'h3F4: reg_rdata = done ? ~crc : 32'd0;
   12'h3F8: reg_rdata = folded;
   12'h3FC: reg_rdata = runs;
   default: reg_rdata = 32'd0;
  endcase
 end
 always @(posedge clk or negedge rst_n) begin
  if (!rst_n) begin
   state <= S_IDLE; busy <= 0; base <= 0; length <= 0; cur <= 0; left <= 0; crc <= 32'hFFFFFFFF; folded <= 0; runs <= 0;
   done <= 0; error <= 0; refused <= 0; beat <= 0; beat_valid <= 0; lane <= 0; burst_left <= 0;
   araddr <= 0; arlen <= 0; arvalid <= 0;
  end else begin
   if (reg_write && !busy && reg_addr == 12'h3E4) base <= reg_wdata;
   if (reg_write && !busy && reg_addr == 12'h3E8) length <= reg_wdata;
   case (state)
    S_IDLE: if (start) begin
      done <= 0; error <= 0; refused <= 0; folded <= 0; crc <= 32'hFFFFFFFF;
      if (!params_ok) begin error <= 1; done <= 1; end
      else if (!idle_ok) refused <= 1;
      else begin busy <= 1; cur <= base; left <= length; state <= S_AR; end
     end
    S_AR: if (!arvalid) begin araddr <= cur; arlen <= {3'd0, burst} - 8'd1; burst_left <= burst; arvalid <= 1; end
     else if (arready) begin arvalid <= 0; state <= S_R; end
    S_R: begin
      if (rvalid && rready) begin
       beat <= rdata; beat_valid <= 1; lane <= 0;
       if (rresp != 2'b00) error <= 1;
       burst_left <= burst_left - 5'd1;
      end
      if (beat_valid) begin
       crc <= crc32_word(crc, lane_word); lane <= lane + 2'd1; folded <= folded + 32'd4;
       if (lane == 2'd3) begin
        beat_valid <= 0; cur <= cur + 32'd16; left <= left - 32'd16;
        if (burst_left == 5'd0) begin
         if (left == 32'd16) begin busy <= 0; done <= 1; runs <= runs + 32'd1; state <= S_IDLE; end
         else state <= S_AR;
        end
       end
      end
     end
    default: state <= S_IDLE;
   endcase
  end
 end
endmodule
