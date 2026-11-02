// HDMI-domain reset release synchronizer.
//
// The DDR calibration indication is produced in core_clk.  It is sampled
// here before it is used to release HDMI-domain state.  Reset asserts
// asynchronously from the board reset and deasserts synchronously after two
// hdmi_tx_slow_clk edges.  The two flops are intentionally kept as a local
// synchronizer; they are not a clock-domain false-path exception.
module hdmi_reset_sync (
    input  wire clk,
    input  wire arst_n,
    input  wire release_in,
    output wire rst_n
);
    (* async_reg = "true" *) reg [1:0] sync_r;

    always @(posedge clk or negedge arst_n) begin
        if (!arst_n)
            sync_r <= 2'b00;
        else
            sync_r <= {sync_r[0], release_in};
    end

    assign rst_n = sync_r[1];
endmodule
