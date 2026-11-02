// One record per real control edge, consumed by PERF one edge later.
// No combinational geometry enters this record; validation is already registered.
reg [7:0] evt_bits;
reg [11:0] evt_tag;
reg evt_valid;
reg [7:0] evt_error_code;
reg [3:0] evt_error_opcode, evt_error_flags;
reg [6:0] evt_bg;
reg block_observer_active;
reg pipeline_done_publish, pipeline_flush_error;
wire evt_frame_start=evt_bits[0], evt_block_start=evt_bits[1], evt_block_end=evt_bits[2];
wire evt_rect_start=evt_bits[3], evt_rect_end=evt_bits[4], evt_abort=evt_bits[5];
wire evt_render_ready=evt_bits[6], evt_present=evt_bits[7];
reg [7:0] abort_code_now;
always @* begin
  abort_code_now=GPU_ERR_COUNT_MISMATCH;
  case(frame_pipeline_state)
    PIPE_CMD_FETCH: abort_code_now=perf_copy_error ? GPU_ERR_BLOCK_COPY :
                      command_fifo_empty ? GPU_ERR_FIFO_UNDERFLOW : GPU_ERR_COUNT_MISMATCH;
    PIPE_CMD_VALIDATE, PIPE_WAIT_REQ, PIPE_WAIT_DONE, PIPE_WAIT_SWAP: abort_code_now=GPU_ERR_BLOCK_COPY;
    PIPE_CMD_DECODE: abort_code_now=perf_copy_error ? GPU_ERR_BLOCK_COPY :
      (command_opcode!=GPU_CMD_OP_RECT && command_opcode!=GPU_CMD_OP_BLIT) ? GPU_ERR_INVALID_OPCODE :
      command_flags!=0 ? GPU_ERR_ILLEGAL_FLAGS :
      command_opcode==GPU_CMD_OP_RECT ? GPU_ERR_MALFORMED_RECT : GPU_ERR_MALFORMED_BLIT;
    PIPE_EXEC_START, PIPE_EXEC_WAIT: abort_code_now=perf_copy_error ? GPU_ERR_BLOCK_COPY : GPU_ERR_EXECUTION_UNIT;
    PIPE_CMD_RETIRE: abort_code_now=perf_copy_error ? GPU_ERR_BLOCK_COPY : GPU_ERR_COUNT_MISMATCH;
    default: abort_code_now=GPU_ERR_COUNT_MISMATCH;
  endcase
  if(framebuffer_read_error) abort_code_now=GPU_ERR_EXECUTION_UNIT;
end
always @(posedge core_clk or negedge test_rst_n) begin
  if(!test_rst_n) begin
    evt_bits<=0; evt_tag<=0; evt_valid<=0; evt_bg<=0;
    evt_error_code<=0; evt_error_opcode<=0; evt_error_flags<=0;
    block_observer_active<=0;
  end else begin
    evt_valid<=1;
    evt_bits<={perf_present_raw,framebuffer_render_done,perf_pipe_abort,perf_rect_end,
               perf_rect_start,perf_block_end,perf_block_start,perf_frame_start};
    evt_tag<={perf_frame_start ? ~frame_epoch : frame_epoch,command_sequence,active_target_id};
    evt_error_code<=abort_code_now;
    evt_error_opcode<=command_opcode; evt_error_flags<=command_flags;
    if(frame_pipeline_state==PIPE_IDLE || frame_pipeline_state==PIPE_PREP_COPY ||
       frame_pipeline_state==PIPE_CMD_FETCH || frame_pipeline_state==PIPE_WAIT_SWAP ||
       ((frame_pipeline_state==PIPE_WAIT_REQ || frame_pipeline_state==PIPE_WAIT_DONE) &&
        active_engine!=GPU_EXEC_BLIT)) begin
      evt_error_opcode<=GPU_CMD_OP_INVALID; evt_error_flags<=0;
    end
    evt_bg<={bg_dbg_target_rect,bg_dbg_burst_event,bg_dbg_b_wait_event,bg_dbg_w_stall_event,
             bg_dbg_aw_stall_event,bg_dbg_w_fire_event,bg_dbg_wait_fifo_event};
    if(perf_frame_start || perf_pipe_abort || perf_block_end) block_observer_active<=0;
    else if(perf_block_start) block_observer_active<=1;
  end
end
