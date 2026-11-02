// Fixed first-generation framebuffer surface map.
// Addresses are byte addresses in the 256 MiB DDR3 address space.
module surface_table (
    input  wire [2:0]  target_id,
    input  wire        front_select,
    output reg  [31:0] front_base,
    output reg  [31:0] back_base,
    output reg  [31:0] stride_bytes,
    output reg  [11:0] width,
    output reg  [11:0] height
);
    always @(*) begin
      front_base=0;back_base=0;stride_bytes=0;width=0;height=0;
      if(target_id==3'd2) begin
        front_base=front_select?32'h01400000:32'h01000000;
        back_base=front_select?32'h01000000:32'h01400000;
        stride_bytes=1920;width=1920;height=1080;
      end
    end
endmodule
