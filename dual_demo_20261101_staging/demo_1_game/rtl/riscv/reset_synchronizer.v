module reset_synchronizer #(
    parameter NUM_STAGE  = 10,
    parameter ACTIVE_LOW = 1
)(
    input  reset_in,
    output reset_out,
    input  clk
);

    reg [NUM_STAGE-1:0] data;

    generate
        if (ACTIVE_LOW == 1)
            always @(posedge clk or negedge reset_in) begin
                if (~reset_in)
                    data <= {NUM_STAGE{1'b0}};
                else
                    data <= {1'b1, data[NUM_STAGE-1:1]};
            end
        else
            always @(posedge clk or posedge reset_in) begin
                if (reset_in)
                    data <= {NUM_STAGE{1'b1}};
                else
                    data <= {1'b0, data[NUM_STAGE-1:1]};
            end
    endgenerate

    assign reset_out = data[0];

endmodule

// CPU200 SoC ingress only: retain five physical reset synchronizer FFs.
module cpu200_reset_synchronizer #(
    parameter NUM_STAGE  = 10,
    parameter ACTIVE_LOW = 1
)(
    input  reset_in,
    output reset_out,
    input  clk
);

    (* async_reg = "true", syn_srlstyle = "registers" *) reg [NUM_STAGE-1:0] data;

    generate
        if (ACTIVE_LOW == 1)
            always @(posedge clk or negedge reset_in) begin
                if (~reset_in)
                    data <= {NUM_STAGE{1'b0}};
                else
                    data <= {1'b1, data[NUM_STAGE-1:1]};
            end
        else
            always @(posedge clk or posedge reset_in) begin
                if (reset_in)
                    data <= {NUM_STAGE{1'b1}};
                else
                    data <= {1'b0, data[NUM_STAGE-1:1]};
            end
    endgenerate

    assign reset_out = data[0];

endmodule
