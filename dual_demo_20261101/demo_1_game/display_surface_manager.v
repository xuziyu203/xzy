// Single owner for front/back roles and VSYNC display commits.
module display_surface_manager (
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
    surface_table u_query_table(.target_id(query_target_id),.front_select(front_select[2]),
      .front_base(query_front_base),.back_base(query_back_base),.stride_bytes(query_stride_bytes),
      .width(query_width),.height(query_height));
    surface_table u_game_table(.target_id(3'd2),.front_select(front_select[2]),
      .front_base(full_front_base),.back_base(full_back_base),.stride_bytes(),.width(),.height());
    assign quad_front_q0=0;assign quad_front_q1=0;assign quad_front_q2=0;assign quad_front_q3=0;
    wire ready=pending_mask[2] || (render_done && render_target_mask==7'b0000100);
    always @(posedge clk or negedge rst_n) begin
      if(!rst_n) begin
        active_mode<=0;active_full_source<=2;front_select<=0;
        swap_pulse<=0;present_ack_mask<=0;pending_mask<=0;await_mask<=0;generation<=0;
      end else begin
        swap_pulse<=0;present_ack_mask<=0;pending_mask<={4'd0,ready,2'd0};
        // Back stays unavailable until the complete descriptor is accepted.
        if(descriptor_accept && await_mask[2] && !read_error) begin
          present_ack_mask<=7'b0000100;swap_pulse<=1;pending_mask<=0;await_mask<=0;generation<=generation+1'b1;
        end
        if(frame_boundary && !read_error && await_mask==0 && ready) begin
          front_select[2]<=!front_select[2];await_mask<=7'b0000100;
        end
      end
    end
endmodule
