// GHD3: DDR CRC reader + its share of the compositor port of enemy_read_arbiter (replaces the GHD2 inline mux in the top).
// Ownership: the reader may start only when
//   - no compositor owner and no asset-writer owner (owner grants are also gated by busy in the top), and
//   - no compositor read is outstanding on the port: c_outstanding counts compositor AR handshakes minus final R beats
//     (the arbiter allows one transaction; the counter makes the "drained" condition explicit instead of assumed).
// While busy the compositor sees arready=0 / rvalid=0 (it never has a request then, since it cannot own the engine); the reader
// sees the port. R data/resp/last are shared wires (only the selected consumer gets rvalid).
module ddr_crc_port (
 input wire clk, rst_n,
 input wire [11:0] reg_addr, input wire reg_write, input wire [31:0] reg_wdata, output wire [31:0] reg_rdata,
 input wire enemy_owner, input wire asset_owner,
 output wire busy,
 // compositor side
 input wire [31:0] e_araddr, input wire [7:0] e_arlen, input wire e_arvalid, output wire e_arready,
 output wire e_rvalid, input wire e_rready,
 // arbiter c port
 output wire [31:0] c_addr, output wire [7:0] c_len, output wire c_valid, input wire c_ready,
 input wire [127:0] c_data, input wire [1:0] c_resp, input wire c_last, input wire c_rvalid, output wire c_rready
);
 reg c_outstanding;
 wire [31:0] r_araddr; wire [7:0] r_arlen; wire r_arvalid, r_rready;
 always @(posedge clk or negedge rst_n)
  if (!rst_n) c_outstanding <= 1'b0;
  else if (!busy) begin
   if (e_arvalid && e_arready) c_outstanding <= 1'b1;
   else if (e_rvalid && e_rready && c_last) c_outstanding <= 1'b0;
  end
 ddr_crc_reader u_reader (
  .clk(clk), .rst_n(rst_n), .reg_addr(reg_addr), .reg_write(reg_write), .reg_wdata(reg_wdata), .reg_rdata(reg_rdata),
  .idle_ok(!enemy_owner && !asset_owner && !c_outstanding && !e_arvalid), .busy(busy),
  .araddr(r_araddr), .arlen(r_arlen), .arvalid(r_arvalid), .arready(c_ready && busy),
  .rdata(c_data), .rresp(c_resp), .rlast(c_last), .rvalid(c_rvalid && busy), .rready(r_rready));
 assign c_addr = busy ? r_araddr : e_araddr;
 assign c_len = busy ? r_arlen : e_arlen;
 assign c_valid = busy ? r_arvalid : e_arvalid;
 assign c_rready = busy ? r_rready : e_rready;
 assign e_arready = c_ready && !busy;
 assign e_rvalid = c_rvalid && !busy;
endmodule
