// Single owner for front/back roles and VSYNC display commits.
module display_surface_manager #(parameter RUNTIME_DBUF = 0) (
    input wire single_buffer_mode,
    input  wire        clk,
    input  wire        rst_n,
    input  wire [1:0]  requested_mode,
    input  wire [1:0]  requested_full_source,
    input  wire        frame_boundary,
    input  wire        render_done,
    input  wire [6:0]  render_target_mask,
    input  wire        async_enable,
    input  wire [2:0]  query_target_id,
    input wire descriptor_accept,
    input wire read_error,
    output reg [6:0] await_mask,

    output reg  [1:0]  active_mode,
    output reg  [1:0]  active_full_source,
    output reg  [6:0]  front_select,
    output reg         swap_pulse,
    output reg  [6:0]  present_ack_mask,
    output reg  [6:0]  pending_mask,
    output reg  [31:0] generation,

    output wire [31:0] query_front_base,
    output wire [31:0] query_back_base,
    output wire [31:0] query_stride_bytes,
    output wire [11:0] query_width,
    output wire [11:0] query_height,
    output wire [31:0] full_front_base,
    output wire [31:0] full_back_base,
    output wire [31:0] quad_front_q0,
    output wire [31:0] quad_front_q1,
    output wire [31:0] quad_front_q2,
    output wire [31:0] quad_front_q3
);
    reg [1:0] last_requested_mode;
    reg [1:0] last_requested_full_source;

    wire [31:0] table_front_base;
    wire [31:0] table_back_base;
    wire [31:0] table_stride;
    wire [11:0] table_width;
    wire [11:0] table_height;

    surface_table u_query_table (
        .target_id   (query_target_id),
        .front_select(front_select[query_target_id]),
        .front_base  (table_front_base),
        .back_base   (table_back_base),
        .stride_bytes(table_stride),
        .width       (table_width),
        .height      (table_height)
    );

    assign query_front_base  = table_front_base;
    assign query_back_base   = (RUNTIME_DBUF && single_buffer_mode) ? table_front_base : table_back_base;
    assign query_stride_bytes = table_stride;
    assign query_width       = table_width;
    assign query_height      = table_height;

    wire [31:0] full0_front;
    wire [31:0] full0_back;
    wire [31:0] full1_front;
    wire [31:0] full1_back;
    wire [31:0] full2_front;
    wire [31:0] full2_back;

    surface_table u_full0 (.target_id(3'd0), .front_select(front_select[0]),
        .front_base(full0_front), .back_base(full0_back), .stride_bytes(), .width(), .height());
    surface_table u_full1 (.target_id(3'd1), .front_select(front_select[1]),
        .front_base(full1_front), .back_base(full1_back), .stride_bytes(), .width(), .height());
    surface_table u_full2 (.target_id(3'd2), .front_select(front_select[2]),
        .front_base(full2_front), .back_base(full2_back), .stride_bytes(), .width(), .height());

    surface_table u_q0 (.target_id(3'd3), .front_select(front_select[3]),
        .front_base(quad_front_q0), .back_base(), .stride_bytes(), .width(), .height());
    surface_table u_q1 (.target_id(3'd4), .front_select(front_select[4]),
        .front_base(quad_front_q1), .back_base(), .stride_bytes(), .width(), .height());
    surface_table u_q2 (.target_id(3'd5), .front_select(front_select[5]),
        .front_base(quad_front_q2), .back_base(), .stride_bytes(), .width(), .height());
    surface_table u_q3 (.target_id(3'd6), .front_select(front_select[6]),
        .front_base(quad_front_q3), .back_base(), .stride_bytes(), .width(), .height());

    assign full_front_base = (active_full_source == 2'd0) ? full0_front :
                             (active_full_source == 2'd1) ? full1_front : full2_front;
    assign full_back_base  = (RUNTIME_DBUF && single_buffer_mode) ? full_front_base : (active_full_source == 2'd0) ? full0_back :
                             (active_full_source == 2'd1) ? full1_back : full2_back;

    // A mode/source request starts a new generation exactly once. The request
    // history advances every clock, so a waiting transition cannot repeatedly
    // erase the pending mask produced by the current frame.
    wire request_generation_change = (requested_mode != last_requested_mode) ||
                                      ((requested_mode == 2'd0) &&
                                       (requested_full_source !=
                                        last_requested_full_source));
    // Include completion on the boundary itself. Partial QUAD readiness must
    // survive boundaries until all four panels can commit together.
    wire [6:0] generation_pending = request_generation_change ? await_mask :
                                    pending_mask;
    wire [6:0] ready_mask = generation_pending |
                           (render_done ? render_target_mask : 7'd0);
    wire quad_ready = &ready_mask[6:3];
    wire [6:0] legacy_commit_mask = {quad_ready ? 4'b1111 : 4'b0000,
                                     ready_mask[2:0]};
    // In ASYNC_QUAD Q0 is independent and Q1/Q2 are one atomic group. Q3
    // remains the already committed Front after initialization. A FULL
    // request is a single-target transaction; it must still be able to
    // commit its own target if an asynchronous request is left asserted
    // while the mode switch is draining.
    wire [6:0] async_full_commit_mask =
        (requested_mode == 2'd0) && (requested_full_source <= 2'd2) &&
        ready_mask[requested_full_source] ?
        (7'b0000001 << requested_full_source) : 7'd0;
    wire [6:0] async_quad_commit_mask = {1'b0,
                                          (ready_mask[5] && ready_mask[4]) ? 2'b11 : 2'b00,
                                          ready_mask[3], 3'b000};
    wire [6:0] async_commit_mask = (requested_mode == 2'd0) ?
                                    async_full_commit_mask :
                                    async_quad_commit_mask;
    wire [6:0] commit_mask = async_enable ? async_commit_mask :
                              legacy_commit_mask;
    wire requested_full_ready = (requested_full_source <= 2'd2) &&
                                 ready_mask[requested_full_source];
    wire requested_full_commit_ready = requested_full_ready &&
                                       commit_mask[requested_full_source];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            active_mode       <= 2'd0;
            active_full_source<= 2'd0;
            last_requested_mode <= 2'd0;
            last_requested_full_source <= 2'd0;
            front_select      <= 7'd0;
            swap_pulse        <= 1'b0;
            present_ack_mask  <= 7'd0;
            pending_mask      <= 7'd0;
            generation        <= 32'd0;
            await_mask <= 7'd0;
        end else begin
            swap_pulse <= 1'b0;
            present_ack_mask <= 7'd0;
            last_requested_mode <= requested_mode;
            last_requested_full_source <= requested_full_source;

            pending_mask <= ready_mask;

            // Present means the complete committed descriptor was accepted.
            // Pending retains await bits so Back cannot be recycled early.
            if(descriptor_accept && await_mask!=0 && !read_error) begin
                present_ack_mask<=await_mask;
                swap_pulse<=1;
                pending_mask<=ready_mask & ~await_mask;
                await_mask<=0;
                generation<=generation+1'b1;
            end
            if (frame_boundary && !read_error && await_mask==0 && (commit_mask != 7'd0)) begin
                if (!(RUNTIME_DBUF && single_buffer_mode))
                    front_select <= front_select ^ commit_mask;
                await_mask <= commit_mask;
                pending_mask <= ready_mask;

                // active_* describes scanout, never the type of the newly
                // completed render. Only a ready requested display may win.
                if ((requested_mode == 2'd0) && requested_full_commit_ready) begin
                    active_mode        <= 2'd0;
                    active_full_source <= requested_full_source;
                end else if ((requested_mode == 2'd1) && quad_ready) begin
                    active_mode <= 2'd1;
                end
            end
        end
    end
endmodule
