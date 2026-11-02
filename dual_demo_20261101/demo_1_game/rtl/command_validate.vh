// Shared production C0-C6 command validation. Included by the real TOP.
// Arithmetic is unsigned; width/range checks precede all narrowed values.
reg [2:0] command_validate_phase;
reg command_context_valid, command_is_full;
reg [31:0] command_front_r, command_back_r, command_stride_r;
reg [11:0] command_surface_width_r, command_surface_height_r;
reg [21:0] command_surface_size_r;
reg [11:0] command_tag_r;
reg [7:0] command_sequence;
reg frame_epoch;
reg rect_descriptor_valid, blit_descriptor_valid, blit_basic_valid;
reg [12:0] cv_right, cv_bottom, cv_width_bytes, cv_x_bytes;
reg [11:0] cv_hm1, cv_y;
reg [4:0] cv_pair0, cv_pair1, cv_reduce30;
reg [5:0] cv_sum;
reg [7:0] cv_low;
reg [15:0] cv_h15, cv_y15;
reg [23:0] cv_span, cv_dst_offset;
reg [32:0] cv_src_end, cv_src_absolute, cv_dst_absolute;
reg [3:0] cv_remainder15;
reg [13:0] cv_row_sum;
reg cv_end_ok, cv_dst_ok;
wire [14:0] cv_upper = command_is_full ? {1'b0,command_src_address[21:8]} : command_src_address[21:7];
wire [11:0] cv_row_byte = command_is_full ? {cv_remainder15,cv_low} : {1'b0,cv_remainder15,cv_low[6:0]};
wire [23:0] cv_hstride = command_is_full ? {cv_h15,8'd0} : {1'b0,cv_h15,7'd0};
wire [23:0] cv_ystride = command_is_full ? {cv_y15,8'd0} : {1'b0,cv_y15,7'd0};
wire command_validation_done = (command_validate_phase == 3'd1 && command_opcode != GPU_CMD_OP_BLIT) || command_validate_phase == 3'd6;
// Target7 had no ack bit in increment2; it must still reach the original decode.
wire [7:0] command_snapshot_ack_mask = {1'b0,surface_present_ack_mask};
wire command_snapshot_wait = surface_commit_boundary || command_snapshot_ack_mask[active_target_id];
always @(posedge core_clk or negedge test_rst_n) begin
  if(!test_rst_n) begin
    frame_epoch<=0; command_sequence<=0;
  end else begin
    if(perf_frame_start) frame_epoch<=~frame_epoch;
    if(command_fifo_pop) command_sequence<=command_sequence+8'd1;
  end
end
always @(posedge core_clk or negedge test_rst_n) begin
  if(!test_rst_n) begin
    command_validate_phase<=0; command_context_valid<=0;
    command_front_r<=0; command_back_r<=0; command_stride_r<=0;
    command_surface_width_r<=0; command_surface_height_r<=0; command_surface_size_r<=0;
    command_is_full<=0; command_tag_r<=0;
    rect_descriptor_valid<=0; blit_descriptor_valid<=0; blit_basic_valid<=0;
    cv_right<=0; cv_bottom<=0; cv_width_bytes<=0; cv_x_bytes<=0; cv_hm1<=0; cv_y<=0;
    cv_pair0<=0; cv_pair1<=0; cv_reduce30<=0; cv_sum<=0; cv_low<=0;
    cv_h15<=0; cv_y15<=0; cv_span<=0; cv_dst_offset<=0;
    cv_src_end<=0; cv_src_absolute<=0; cv_dst_absolute<=0;
    cv_remainder15<=0; cv_row_sum<=0; cv_end_ok<=0; cv_dst_ok<=0;
  end else if(frame_pipeline_state == PIPE_CMD_FETCH) begin
    command_validate_phase<=0; command_context_valid<=0;
    rect_descriptor_valid<=0; blit_descriptor_valid<=0;
  end else if(perf_pipe_abort || frame_pipeline_state == PIPE_CMD_RETIRE) begin
    command_context_valid<=0;
  end else if(frame_pipeline_state == PIPE_CMD_VALIDATE) begin
    case(command_validate_phase)
      0: if(!command_snapshot_wait) begin
        command_front_r<=front_buffer_base; command_back_r<=back_buffer_base;
        command_stride_r<=target_stride_bytes;
        command_surface_width_r<=target_surface_width; command_surface_height_r<=target_surface_height;
        command_surface_size_r<=(active_target_id < 3'd3) ? 22'd4147200 : 22'd1036800;
        command_is_full<=(active_target_id < 3'd3);
        command_tag_r<={frame_epoch,command_sequence,active_target_id};
        command_context_valid<=1; command_validate_phase<=1;
      end
      1: begin
        rect_descriptor_valid <= command_x < command_surface_width_r && command_y < command_surface_height_r &&
          command_width!=0 && command_width<=command_surface_width_r && command_height!=0 && command_height<=command_surface_height_r &&
          command_alpha==0 && command_src_address==0 && command_src_stride==0;
        blit_basic_valid <= command_width!=0 && command_width<=command_surface_width_r &&
          command_height!=0 && command_height<=command_surface_height_r && command_width[2:0]==0 && command_x[2:0]==0 &&
          command_src_address[3:0]==0 && command_src_address < {10'd0,command_surface_size_r} &&
          command_src_stride==command_stride_r[15:0] && command_alpha==0 && command_color_or_key==0;
        cv_right<={1'b0,command_x}+{1'b0,command_width};
        cv_bottom<={1'b0,command_y}+{1'b0,command_height};
        cv_width_bytes<={command_width,1'b0}; cv_x_bytes<={command_x,1'b0};
        cv_hm1<=command_height==0 ? 12'd0 : command_height-12'd1; cv_y<=command_y;
        cv_pair0<={1'b0,cv_upper[3:0]}+{1'b0,cv_upper[7:4]};
        cv_pair1<={1'b0,cv_upper[11:8]}+{2'b0,cv_upper[14:12]};
        cv_low<=command_is_full ? command_src_address[7:0] : {1'b0,command_src_address[6:0]};
        command_validate_phase<=2;
      end
      2: begin
        cv_sum<={1'b0,cv_pair0}+{1'b0,cv_pair1};
        cv_h15<={cv_hm1,4'd0}-{4'd0,cv_hm1}; cv_y15<={cv_y,4'd0}-{4'd0,cv_y};
        command_validate_phase<=3;
      end
      3: begin
        cv_reduce30<=cv_sum>=6'd30 ? cv_sum-6'd30 : cv_sum;
        cv_span<=cv_hstride+{11'd0,cv_width_bytes}; cv_dst_offset<=cv_ystride+{11'd0,cv_x_bytes};
        command_validate_phase<=4;
      end
      4: begin
        cv_remainder15<=cv_reduce30>=5'd15 ? cv_reduce30-5'd15 : cv_reduce30;
        cv_src_end<={1'b0,command_src_address}+{9'd0,cv_span};
        cv_src_absolute<={1'b0,command_front_r}+{1'b0,command_src_address};
        cv_dst_absolute<={1'b0,command_back_r}+{9'd0,cv_dst_offset};
        command_validate_phase<=5;
      end
      5: begin
        cv_row_sum<={2'd0,cv_row_byte}+{1'b0,cv_width_bytes};
        cv_end_ok<=cv_src_end<={11'd0,command_surface_size_r};
        cv_dst_ok<=cv_right<={1'b0,command_surface_width_r} && cv_bottom<={1'b0,command_surface_height_r};
        command_validate_phase<=6;
      end
      6: blit_descriptor_valid<=blit_basic_valid && cv_end_ok && cv_dst_ok &&
           {18'd0,cv_row_sum}<=command_stride_r;
      default: command_context_valid<=0;
    endcase
  end
end
