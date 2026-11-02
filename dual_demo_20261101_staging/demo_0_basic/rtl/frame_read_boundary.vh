// Original frame_buffer read-period edge detector, shared by finite integration.
reg fifo_rd_period_r0=0, fifo_rd_period_r1=0, fifo_rd_period_r2=0;
always @(posedge axi_clk) begin
  fifo_rd_period_r0<=fifo_rd_period;
  fifo_rd_period_r1<=fifo_rd_period_r0;
  fifo_rd_period_r2<=fifo_rd_period_r1;
end
wire neg_rd_period = {fifo_rd_period_r2,fifo_rd_period_r1}==2'b10;
