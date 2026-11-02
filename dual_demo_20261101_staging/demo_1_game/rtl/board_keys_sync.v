// Ordinary keys only. KEY0 uses the existing board reset/PLL path.
// Active-low raw inputs, no actions or debounce in this module. The bounded
// CLINT-based firmware debounce consumes only keys_n_sync. Peripheral reset
// asserts asynchronously and is released by the existing core100 reset chain.
module board_keys_sync (
    input wire clk,
    input wire rst_n,
    input wire [2:0] keys_n_async,
    output wire [2:0] keys_n_sync
);
    (* async_reg = "true", syn_srlstyle = "registers" *) reg [2:0] key_meta;
    (* async_reg = "true", syn_srlstyle = "registers" *) reg [2:0] key_sync;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            key_meta <= 3'b111;
            key_sync <= 3'b111;
        end else begin
            key_meta <= keys_n_async;
            key_sync <= key_meta;
        end
    end
    assign keys_n_sync = key_sync;
endmodule
