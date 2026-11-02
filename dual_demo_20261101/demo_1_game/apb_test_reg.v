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
    assign asset_reg_addr = PADDR;
    assign asset_reg_wdata = PWDATA;
    assign asset_reg_write = PSEL && PENABLE && PWRITE &&
                             (((PADDR >= 12'h120) && (PADDR <= 12'h180)) ||
                              PADDR == 12'h1F0 || PADDR == 12'h304 || PADDR == 12'h324 ||
                              (PADDR>=12'h200 && PADDR<=12'h2fc && PADDR[1:0]==0) ||
                              (PADDR>=12'h340 && PADDR<=12'h374 && PADDR[1:0]==0) ||
                              (PADDR>=12'h380 && PADDR<=12'h3ac && PADDR[1:0]==0) || // BGT-A decor
                              (PADDR>=12'h3b0 && PADDR<=12'h3d0 && PADDR[1:0]==0) || // CMD decor slots 8..15
                              (PADDR>=12'h3e0 && PADDR<=12'h3fc && PADDR[1:0]==0)); // GHD DDR CRC reader

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
            full_source_req <= 2'd2;
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

            // An asset lease owns the shared DDR writer.  Keep all legacy
            // command and CPU pulses quiescent until it releases; the
            // dedicated 0x120..0x150 sideband remains live below.
            if (PSEL && PENABLE && PWRITE && !asset_owner_active) begin
                case (PADDR)
                    12'h000: test_reg <= PWDATA;

                    // No demo producer exists. Old MMIO writes are rejected.
                    12'h004,12'h00C,12'h010,12'h014,12'h018,12'h01C,
                    12'h060,12'h064: push_reject<=1;
                    12'h08C: begin push_reject<=1;async_mode_reject_pulse<=1;end
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
            // Keep its shadow/commit port live while an asset/enemy lease is
            // held; the resource-changing registers above remain protected.
            // The CDC still freezes each accepted bundle until VSYNC ACK.
            if (PSEL && PENABLE && PWRITE) begin
                case (PADDR)
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
                12'h310: PRDATA = 32'h51380101;
                12'h314: PRDATA = 32'd1920;
                12'h318: PRDATA = 32'd2073600;
                12'h31c: PRDATA = 32'h00010332;
                12'h320: PRDATA = 32'h10040100;
                12'h060: PRDATA = 32'h474f0200;
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
                12'h08C: PRDATA = 32'd0; // unsupported, no A601 capability
                12'h090: PRDATA = async_q0_present_seq;
                12'h094: PRDATA = async_gpu_pair_present_seq;
                12'h098: PRDATA = async_gpu_render_cycles;
                12'h09C: PRDATA = async_gpu_owner_wait_cycles;
                12'h0A0: PRDATA = async_event_status;
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
                12'h120,12'h124,12'h128,12'h12C,12'h130,12'h134,
                12'h138,12'h13C,12'h140,12'h144,12'h148,12'h14C,12'h150,12'h154,12'h158,12'h15C,12'h160,12'h164,12'h168,12'h16C,
                12'h170,12'h174,12'h178,12'h17C,12'h180,
                12'h184,12'h188,12'h18C,12'h190,12'h194,12'h198,12'h19C,
                12'h1A0,12'h1A4,12'h1A8,12'h1AC,12'h1B0,12'h1B4,12'h1B8,12'h1BC,
                12'h1C0,12'h1C4,12'h1C8,12'h1CC,12'h1D0,12'h1D4,12'h1D8,
                12'h1DC,12'h1E0,12'h1E4,12'h1E8,12'h1F0,12'h1F4,12'h1F8:
                    PRDATA = asset_reg_rdata;
                12'h200,12'h204,12'h208,12'h20c,12'h210,12'h214,12'h218,12'h21c,
                12'h220,12'h224,12'h228,12'h22c,12'h230,12'h234,12'h238,12'h23c,
                12'h240,12'h244,12'h248,12'h24c,12'h250,12'h254,12'h258,12'h25c,
                12'h260,12'h264,12'h268,12'h26c,12'h270,12'h274,12'h278,12'h27c,
                12'h280,12'h284,12'h288,12'h28c,
                12'h290,12'h294,12'h298,12'h29c,12'h2a0,12'h2a4,12'h2a8,
                12'h2ac,12'h2b0,12'h2b4,12'h2b8,12'h2bc,12'h2c0,12'h2c4,
                12'h2c8,12'h2cc,12'h2d0,12'h2d4,12'h2d8,12'h2dc,12'h2e0,12'h2e4,
                12'h2e8,12'h2ec,12'h2f0,12'h2f4,12'h2f8,12'h2fc,
                12'h340,12'h344,12'h348,12'h34c,12'h350,12'h354,12'h358,12'h35c,
                12'h360,12'h364,12'h368,12'h36c,12'h370,12'h374,
                12'h300,12'h304,12'h308,12'h30c,12'h324,12'h328,
                12'h380,12'h384,12'h388,12'h38c,12'h390,12'h394,12'h398,12'h39c,12'h3a0,12'h3a4,12'h3a8,12'h3ac,
                12'h3b0,12'h3b4,12'h3b8,12'h3bc,12'h3c0,12'h3c4,12'h3c8,12'h3cc,12'h3d0,
                12'h3e0,12'h3e4,12'h3e8,12'h3ec,12'h3f0,12'h3f4,12'h3f8,12'h3fc: // GHD DDR CRC reader
                    PRDATA = asset_reg_rdata;
                12'h1EC: PRDATA = 32'h48440900; // GHDA: HUD2, ENEMY + BULLETS show the display count w4[31:16] (real + 400 in STRESS, no rounding), w4[1:0] = MODE; // GHD HUD2 (rtl/game_hud2_overlay.v); the q3 game HUD (0x48440402 rows) stays available
                default: PRDATA = 32'h00000000;
            endcase
        end
    end

endmodule
