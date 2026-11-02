module q3_perf_snapshot_cdc #(
    parameter DATA_WIDTH = 224
) (
    input  wire                  core_clk,
    input  wire                  pix_clk,
    input  wire                  rst_n,
    input  wire [DATA_WIDTH-1:0] shadow_data_core,
    input  wire                  commit_pulse_core,
    output reg                   busy_core,
    input  wire                  vsync_pix,
    output reg  [DATA_WIDTH-1:0] snapshot_pix,
    output reg                   snapshot_valid_pix
);

    /* Bundled-data mailbox: publish_data_core is written before req_toggle_core
     * changes and remains unchanged until the pixel-domain acknowledgement. */
    reg [DATA_WIDTH-1:0] publish_data_core;
    reg req_toggle_core;
    reg ack_toggle_pix;
    reg ack_sync0_core;
    reg ack_sync1_core;

    reg req_sync0_pix;
    reg req_sync1_pix;
    reg req_seen_pix;
    reg vsync_pix_d;

    /* rst_n includes core-domain DDR calibration. Both mailbox domains must
     * see even a short assertion, while pixel state may leave reset only on
     * its own clock. Do not substitute a synchronously sampled cal_done:
     * missing its low pulse would leave stale request/ack toggle state. */
    (* async_reg = "true" *) reg [1:0] pix_reset_sync;
    wire pix_rst_n = pix_reset_sync[1];
    always @(posedge pix_clk or negedge rst_n) begin
        if (!rst_n)
            pix_reset_sync <= 2'b00;
        else
            pix_reset_sync <= {pix_reset_sync[0], 1'b1};
    end

    always @(posedge core_clk or negedge rst_n) begin
        if (!rst_n) begin
            publish_data_core <= {DATA_WIDTH{1'b0}};
            req_toggle_core   <= 1'b0;
            ack_sync0_core    <= 1'b0;
            ack_sync1_core    <= 1'b0;
            busy_core         <= 1'b0;
        end else begin
            ack_sync0_core <= ack_toggle_pix;
            ack_sync1_core <= ack_sync0_core;

            if (busy_core && (ack_sync1_core == req_toggle_core))
                busy_core <= 1'b0;

            /* A busy commit is intentionally ignored.  The software starts a
             * fresh one-second window and tries again on its next publication. */
            if (commit_pulse_core && !busy_core) begin
                publish_data_core <= shadow_data_core;
                req_toggle_core   <= ~req_toggle_core;
                busy_core         <= 1'b1;
            end
        end
    end

    always @(posedge pix_clk or negedge pix_rst_n) begin
        if (!pix_rst_n) begin
            req_sync0_pix       <= 1'b0;
            req_sync1_pix       <= 1'b0;
            req_seen_pix        <= 1'b0;
            ack_toggle_pix      <= 1'b0;
            vsync_pix_d        <= 1'b0;
            snapshot_pix        <= {DATA_WIDTH{1'b0}};
            snapshot_valid_pix  <= 1'b0;
        end else begin
            req_sync0_pix <= req_toggle_core;
            req_sync1_pix <= req_sync0_pix;
            vsync_pix_d   <= vsync_pix;

            /* The complete bundle is accepted only at the start of a frame. */
            if (vsync_pix && !vsync_pix_d &&
                (req_sync1_pix != req_seen_pix)) begin
                snapshot_pix       <= publish_data_core;
                snapshot_valid_pix <= 1'b1;
                req_seen_pix       <= req_sync1_pix;
                ack_toggle_pix     <= req_sync1_pix;
            end
        end
    end
endmodule
