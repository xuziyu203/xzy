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
    localparam [31:0] FULL_SIZE   = 32'h003F_4800;
    localparam [31:0] QUAD_SIZE   = 32'h000F_D200;

    function [31:0] target_a;
        input [2:0] id;
        begin
            case (id)
                3'd0: target_a = 32'h0000_0000; // FULL_BLIT_A
                3'd1: target_a = 32'h0080_0000; // FULL_RECT_A
                3'd2: target_a = 32'h0100_0000; // FULL_CPU_A
                3'd3: target_a = 32'h0180_0000; // Q0_A
                3'd4: target_a = 32'h01A0_0000; // Q1_A
                3'd5: target_a = 32'h01C0_0000; // Q2_A
                3'd6: target_a = 32'h01E0_0000; // Q3_A
                default: target_a = 32'h0000_0000;
            endcase
        end
    endfunction

    function [31:0] target_b;
        input [2:0] id;
        begin
            case (id)
                3'd0: target_b = 32'h0040_0000; // FULL_BLIT_B
                3'd1: target_b = 32'h00C0_0000; // FULL_RECT_B
                3'd2: target_b = 32'h0140_0000; // FULL_CPU_B
                3'd3: target_b = 32'h0190_0000; // Q0_B
                3'd4: target_b = 32'h01B0_0000; // Q1_B
                3'd5: target_b = 32'h01D0_0000; // Q2_B
                3'd6: target_b = 32'h01F0_0000; // Q3_B
                default: target_b = 32'h0040_0000;
            endcase
        end
    endfunction

    always @(*) begin
        if (front_select == 1'b0) begin
            front_base = target_a(target_id);
            back_base  = target_b(target_id);
        end else begin
            front_base = target_b(target_id);
            back_base  = target_a(target_id);
        end

        if (target_id >= 3'd3) begin
            stride_bytes = 32'd1920;
            width        = 12'd960;
            height       = 12'd540;
        end else begin
            stride_bytes = 32'd3840;
            width        = 12'd1920;
            height       = 12'd1080;
        end
    end
endmodule
