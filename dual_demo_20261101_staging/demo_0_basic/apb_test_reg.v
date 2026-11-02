module apb_test_reg (
    input  wire        clk,
    input  wire        resetn,
    input  wire [11:0] PADDR,
    input  wire        PSEL,
    input  wire        PENABLE,
    input  wire        PWRITE,
    input  wire [31:0] PWDATA,
    output reg  [31:0] PRDATA,
    output wire        PREADY,
    output wire        PSLVERROR,

    output reg         pipeline_start_req,
    input  wire        pipeline_busy,
    input  wire        pipeline_done_pulse,
    input  wire        pipeline_error,
    input  wire [7:0]  pipeline_error_code,
    input  wire [3:0]  pipeline_error_opcode,
    input  wire [3:0]  pipeline_error_flags,
    output reg  [7:0]  pipeline_frame_command_count,

    input  wire [31:0] perf_blockcopy_cycles,
    input  wire [31:0] perf_blockcopy_total_cycles,
    input  wire [31:0] perf_rect_last_cycles,
    input  wire [31:0] perf_rect_total_cycles,
    input  wire [31:0] perf_frame_cycles,
    input  wire [31:0] perf_rect_count,

    // TEMP_BG_PERF: read-only diagnosis, outside formal PERF registers.
    input  wire [31:0] bg_dbg_cycles,
    input  wire [31:0] bg_dbg_w_fire,
    input  wire [31:0] bg_dbg_wait_fifo_cycles,
    input  wire [31:0] bg_dbg_aw_stall,
    input  wire [31:0] bg_dbg_w_stall,
    input  wire [31:0] bg_dbg_b_wait,
    input  wire [31:0] bg_dbg_burst_count,
    input  wire [31:0] bg_dbg_status,

    output reg         cmd_push_pulse,
    output reg  [127:0] cmd_push_data,
    output reg  [2:0]   cmd_push_target_id,
    output reg  [2:0]   target_context,
    output reg  [1:0]   display_mode_req,
    output reg  [1:0]   full_source_req,
    input  wire        fifo_full,
    input  wire        fifo_empty,
    input  wire [7:0]  fifo_level
    ,input wire [1:0] active_display_mode
    ,input wire [1:0] active_full_source
    ,input wire [6:0] surface_front_select
    ,input wire [31:0] surface_generation
    ,input wire        cpu_active
    ,input wire [31:0] cpu_status
    ,input wire [31:0] cpu_word_count
    ,output reg        cpu_target_write
    ,output reg [31:0] cpu_target_value
    ,output reg        cpu_xy_write
    ,output reg [31:0] cpu_xy_value
    ,output reg        cpu_row_words_write
    ,output reg [31:0] cpu_row_words_value
    ,output reg        cpu_data_write
    ,output reg [31:0] cpu_data_value
    ,output reg        cpu_control_write
    ,output reg [5:0]  cpu_control_value
    ,output reg        async_enable_request
    ,output reg        async_mode_reject_pulse
    ,input  wire       async_enable_accept
    ,input  wire [31:0] async_q0_present_seq
    ,input  wire [31:0] async_gpu_pair_present_seq
    ,input  wire [31:0] async_gpu_render_cycles
    ,input  wire [31:0] async_gpu_owner_wait_cycles
    ,input  wire [31:0] async_event_status
    ,input wire demo_options_accept
    ,output reg demo_single_beat
    ,output reg demo_single_buffer
    ,output reg [31:0] panel_cpu_row
    ,output reg [31:0] panel_blit_row
    ,output reg [31:0] panel_rect_row
    ,output reg [31:0] panel_blit_time
    ,output reg [31:0] panel_rect_time
    ,output reg [31:0] panel_flags
    ,output reg [31:0] panel_error
    ,output reg        panel_commit_pulse
    ,input  wire       panel_commit_busy
    ,output reg        diag_arm_pulse
    ,output reg        diag_clear_pulse
    ,input wire [31:0] diag_reg0, input wire [31:0] diag_reg1
    ,input wire [31:0] diag_reg2, input wire [31:0] diag_reg3
    ,input wire [31:0] diag_reg4, input wire [31:0] diag_reg5
    ,input wire [31:0] diag_reg6, input wire [31:0] diag_reg7
    ,input wire [31:0] diag_reg8, input wire [31:0] diag_reg9
    ,input wire [31:0] diag_reg10, input wire [31:0] diag_reg11
    ,input wire [31:0] diag_reg12, input wire [31:0] diag_reg13
    ,input wire [31:0] diag_reg14, input wire [31:0] diag_reg15
    ,input wire [31:0] diag_reg16, input wire [31:0] diag_reg17
    ,input wire [31:0] diag_reg18, input wire [31:0] diag_reg19
    ,input wire [31:0] diag_reg20
    ,output wire [11:0] asset_reg_addr
    ,output wire asset_reg_write
    ,output wire [31:0] asset_reg_wdata
    ,input wire [31:0] asset_reg_rdata
    ,input wire asset_owner_active
);

    reg [31:0] test_reg;
    reg        pipeline_done;
    reg [31:0] cmd_word0_shadow;
    reg [31:0] cmd_word1_shadow;
    reg [31:0] cmd_word2_shadow;
    reg [31:0] cmd_word3_shadow;
    reg        push_reject;
    reg        start_empty;

    // This slave has no wait states and never reports an APB error.
    assign PREADY    = 1'b1;
    assign PSLVERROR = 1'b0;
    // Reserved extension pins remain for shared-module interface compatibility.
    // This reserved transport now serves only scope measurement at 0x200..0x263.
    // The removed game/asset register protocol remains unavailable.
    assign asset_reg_addr = PADDR;
    assign asset_reg_wdata = PWDATA;
    assign asset_reg_write = PSEL && PENABLE && PWRITE && (PADDR >= 12'h200) && (PADDR < 12'h264);

    // APB writes occur in the access phase.
    // 0x000: legacy test register.
    // 0x004 bit 0: request one frame-pipeline run.
    // 0x00C-0x018: four 32-bit Command v2 staging words.
    // 0x01C bit 0: atomically push one staged 128-bit command.
    always @(posedge clk) begin
        if (!resetn) begin
            test_reg <= 32'h00000000;
            pipeline_start_req <= 1'b0;
            pipeline_done      <= 1'b0;
            pipeline_frame_command_count <= 8'd0;
            cmd_push_pulse <= 1'b0;
            cmd_push_data  <= 128'd0;
            cmd_push_target_id <= 3'd0;
            target_context <= 3'd0;
            display_mode_req <= 2'd0;
            full_source_req <= 2'd0;
            push_reject    <= 1'b0;
            start_empty    <= 1'b0;
            cmd_word0_shadow <= 32'd0;
            cmd_word1_shadow <= 32'd0;
            cmd_word2_shadow <= 32'd0;
            cmd_word3_shadow <= 32'd0;
            cpu_target_write <= 1'b0;
            cpu_target_value <= 32'd0;
            cpu_xy_write <= 1'b0;
            cpu_xy_value <= 32'd0;
            cpu_row_words_write <= 1'b0;
            cpu_row_words_value <= 32'd0;
            cpu_data_write <= 1'b0;
            cpu_data_value <= 32'd0;
            cpu_control_write <= 1'b0;
            cpu_control_value <= 6'd0;
            async_enable_request <= 1'b0;
            async_mode_reject_pulse <= 1'b0;
            demo_single_beat <= 1'b0;
            demo_single_buffer <= 1'b0;
            panel_cpu_row <= 32'd0;
            panel_blit_row <= 32'd0;
            panel_rect_row <= 32'd0;
            panel_blit_time <= 32'd0;
            panel_rect_time <= 32'd0;
            panel_flags <= 32'd0;
            panel_error <= 32'd0;
            panel_commit_pulse <= 1'b0;
            diag_arm_pulse <= 1'b0;
            diag_clear_pulse <= 1'b0;
        end
        else begin
            cmd_push_pulse <= 1'b0;
            cpu_target_write <= 1'b0;
            cpu_xy_write <= 1'b0;
            cpu_row_words_write <= 1'b0;
            cpu_data_write <= 1'b0;
            cpu_control_write <= 1'b0;
            async_mode_reject_pulse <= 1'b0;
            panel_commit_pulse <= 1'b0;
            diag_arm_pulse <= 1'b0;
            diag_clear_pulse <= 1'b0;

            if (pipeline_done_pulse)
                pipeline_done <= 1'b1;

            // Hold START until the top-level pipeline leaves IDLE.
            if (pipeline_busy)
                pipeline_start_req <= 1'b0;

            // Preserve the external-owner guard on the shared APB interface.
            // The basic Demo top-level permanently ties this owner inactive.
            if (PSEL && PENABLE && PWRITE && !asset_owner_active) begin
                case (PADDR)
                    12'h000: test_reg <= PWDATA;

                    12'h004: begin
                        if (PWDATA[0] && (!cpu_active || async_enable_request) &&
                            !pipeline_busy &&
                            !pipeline_start_req) begin
                            if (fifo_level != 8'd0) begin
                                pipeline_start_req <= 1'b1;
                                pipeline_done      <= 1'b0;
                                pipeline_frame_command_count <= fifo_level;
                                push_reject <= 1'b0;
                                start_empty <= 1'b0;
                            end
                            else begin
                                start_empty <= 1'b1;
                            end
                        end
                    end

                    12'h00C: cmd_word0_shadow <= PWDATA;
                    12'h010: cmd_word1_shadow <= PWDATA;
                    12'h014: cmd_word2_shadow <= PWDATA;
                    12'h018: cmd_word3_shadow <= PWDATA;

                    12'h01C: begin
                        if (PWDATA[0]) begin
                            if ((!cpu_active || async_enable_request) && !pipeline_busy &&
                                !pipeline_start_req &&
                                !fifo_full) begin
                                // Snapshot all four staging words. The FIFO
                                // entry never depends on later staging writes.
                                cmd_push_data <= {
                                    cmd_word3_shadow,
                                    cmd_word2_shadow,
                                    cmd_word1_shadow,
                                    cmd_word0_shadow
                                };
                                cmd_push_target_id <= target_context;
                                cmd_push_pulse <= 1'b1;
                            end
                            else begin
                                push_reject <= 1'b1;
                            end
                        end
                    end

                    12'h060: target_context <= PWDATA[2:0];
                    12'h064: begin
                        if (!cpu_active && !async_enable_request) begin
                            display_mode_req <= PWDATA[1:0];
                            full_source_req <= PWDATA[3:2];
                        end
                    end

                    12'h08C: begin
                        if (PWDATA[31:16] == 16'hA601) begin
                            if (!PWDATA[0] || async_enable_accept)
                                async_enable_request <= PWDATA[0];
                            else
                                async_mode_reject_pulse <= 1'b1;
                        end else begin
                            async_mode_reject_pulse <= 1'b1;
                        end
                    end

                    12'h070: begin
                        cpu_target_write <= 1'b1;
                        cpu_target_value <= PWDATA;
                    end
                    12'h074: begin
                        cpu_xy_write <= 1'b1;
                        cpu_xy_value <= PWDATA;
                    end
                    12'h078: begin
                        cpu_row_words_write <= 1'b1;
                        cpu_row_words_value <= PWDATA;
                    end
                    12'h07C: begin
                        cpu_data_write <= 1'b1;
                        cpu_data_value <= PWDATA;
                    end
                    12'h080: begin
                        cpu_control_write <= 1'b1;
                        cpu_control_value <= PWDATA[5:0];
                    end

                    12'h0C8: begin
                        if (PWDATA[0]) diag_clear_pulse <= 1'b1;
                        if (PWDATA[1]) diag_arm_pulse <= 1'b1;
                    end

                    default: begin
                    end
                endcase
            end

            // The scanout HUD mailbox does not use DDR or the shared writer.
            // Keep its shadow/commit port independent of the writer owner;
            // the resource-changing registers above remain protected.
            // The CDC still freezes each accepted bundle until VSYNC ACK.
            if (PSEL && PENABLE && PWRITE) begin
                case (PADDR)
                    12'h11C: if (PWDATA[31:16] == 16'hB502 && demo_options_accept) begin
                        demo_single_beat <= !PWDATA[0];
                        demo_single_buffer <= !PWDATA[1];
                    end
                    12'h0A4: panel_cpu_row <= PWDATA;
                    12'h0A8: panel_blit_row <= PWDATA;
                    12'h0AC: panel_rect_row <= PWDATA;
                    12'h0B0: panel_blit_time <= PWDATA;
                    12'h0B4: panel_rect_time <= PWDATA;
                    12'h0B8: panel_flags <= PWDATA;
                    12'h0BC: panel_error <= PWDATA;
                    12'h0C0: begin
                        if (PWDATA[0] && !panel_commit_busy)
                            panel_commit_pulse <= 1'b1;
                    end
                    default: begin end
                endcase
            end
        end
    end

    // APB read data is returned in the access phase.
    // 0x008 bit 0: BUSY, bit 1: DONE, bit 2: ERROR.
    // 0x020: FIFO level/status and sticky rejected-operation flags.
    // 0x024: latched pipeline error information.
    // 0x028-0x03C: read-only core_clk performance counters; reads do not clear.
    // 0x0A4-0x0BC: Q3 panel shadow words; 0x0C0 commits when mailbox idle.
    // 0x0C4 bit 0: panel mailbox busy until pixel-domain VSYNC ack.
    always @(*) begin
        PRDATA = 32'h00000000;
        if (PSEL && PENABLE && !PWRITE) begin
            case (PADDR)
                12'h000: PRDATA = test_reg;
                12'h004: PRDATA = {31'd0, pipeline_start_req};
                12'h008: PRDATA = {29'd0, pipeline_error,
                                    pipeline_done, pipeline_busy};
                12'h00C: PRDATA = cmd_word0_shadow;
                12'h010: PRDATA = cmd_word1_shadow;
                12'h014: PRDATA = cmd_word2_shadow;
                12'h018: PRDATA = cmd_word3_shadow;
                12'h020: PRDATA = {20'd0, start_empty, push_reject,
                                    fifo_full, fifo_empty, fifo_level};
                12'h024: PRDATA = {16'd0, pipeline_error_flags,
                                    pipeline_error_opcode,
                                    pipeline_error_code};
                12'h028: PRDATA = perf_blockcopy_cycles;
                12'h03C: PRDATA = perf_blockcopy_total_cycles;
                12'h02C: PRDATA = perf_rect_last_cycles;
                12'h030: PRDATA = perf_rect_total_cycles;
                12'h034: PRDATA = perf_frame_cycles;
                12'h038: PRDATA = perf_rect_count;
                // TEMP_BG_PERF 0x040-0x05C: no read/write side effects.
                12'h040: PRDATA = bg_dbg_cycles;
                12'h044: PRDATA = bg_dbg_w_fire;
                12'h048: PRDATA = bg_dbg_wait_fifo_cycles;
                12'h04C: PRDATA = bg_dbg_aw_stall;
                12'h050: PRDATA = bg_dbg_w_stall;
                12'h054: PRDATA = bg_dbg_b_wait;
                12'h058: PRDATA = bg_dbg_burst_count;
                12'h05C: PRDATA = bg_dbg_status;
                12'h060: PRDATA = {29'd0, target_context};
                12'h064: PRDATA = {28'd0, full_source_req, display_mode_req};
                12'h068: PRDATA = surface_generation;
                12'h06C: PRDATA = {21'd0, surface_front_select,
                                    active_full_source, active_display_mode};
                12'h070: PRDATA = 32'd3;
                12'h074: PRDATA = 32'd0;
                12'h078: PRDATA = 32'd0;
                12'h080: PRDATA = 32'd0;
                12'h084: PRDATA = cpu_status;
                12'h088: PRDATA = cpu_word_count;
                12'h08C: PRDATA = {16'hA601, 15'd0, async_enable_request};
                12'h090: PRDATA = async_q0_present_seq;
                12'h094: PRDATA = async_gpu_pair_present_seq;
                12'h098: PRDATA = async_gpu_render_cycles;
                12'h09C: PRDATA = async_gpu_owner_wait_cycles;
                12'h0A0: PRDATA = async_event_status;
                12'h11C: PRDATA = 32'hB5020004 | {30'd0, !demo_single_buffer, !demo_single_beat} |
                    (demo_options_accept ? 32'd0 : 32'h00000100);
                12'h0A4: PRDATA = panel_cpu_row;
                12'h0A8: PRDATA = panel_blit_row;
                12'h0AC: PRDATA = panel_rect_row;
                12'h0B0: PRDATA = panel_blit_time;
                12'h0B4: PRDATA = panel_rect_time;
                12'h0B8: PRDATA = panel_flags;
                12'h0BC: PRDATA = panel_error;
                12'h0C4: PRDATA = {31'd0, panel_commit_busy};
                12'h0C8: PRDATA = diag_reg0;
                12'h0CC: PRDATA = diag_reg1;
                12'h0D0: PRDATA = diag_reg2;
                12'h0D4: PRDATA = diag_reg3;
                12'h0D8: PRDATA = diag_reg4;
                12'h0DC: PRDATA = diag_reg5;
                12'h0E0: PRDATA = diag_reg6;
                12'h0E4: PRDATA = diag_reg7;
                12'h0E8: PRDATA = diag_reg8;
                12'h0EC: PRDATA = diag_reg9;
                12'h0F0: PRDATA = diag_reg10;
                12'h0F4: PRDATA = diag_reg11;
                12'h0F8: PRDATA = diag_reg12;
                12'h0FC: PRDATA = diag_reg13;
                12'h100: PRDATA = diag_reg14;
                12'h104: PRDATA = diag_reg15;
                12'h108: PRDATA = diag_reg16;
                12'h10C: PRDATA = diag_reg17;
                12'h110: PRDATA = diag_reg18;
                12'h114: PRDATA = diag_reg19;
                12'h118: PRDATA = diag_reg20;
                default: PRDATA = (PADDR >= 12'h200 && PADDR < 12'h264) ? asset_reg_rdata : 32'h00000000;
            endcase
        end
    end

endmodule
