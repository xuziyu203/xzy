// CPU Q0/FULL: APB-fed RGB565 framebuffer writer with target-local geometry.
// APB DATA words are {pixel1,pixel0}; four words form one MSB-first beat.
module cpu_framebuffer_writer (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        target_write,
    input  wire [31:0] target_value,
    input  wire        xy_write,
    input  wire [31:0] xy_value,
    input  wire        row_words_write,
    input  wire [31:0] row_words_value,
    input  wire        data_write,
    input  wire [31:0] data_value,
    input  wire        control_write,
    input  wire [5:0]  control_value,
    input  wire        fifo_full,
    input  wire        owner_grant,
    input  wire        row_owner_grant,
    input  wire        [31:0] table_front_base,
    input  wire        [31:0] table_back_base,
    input  wire        [31:0] table_stride_bytes,
    input  wire        [11:0] table_width,
    input  wire        [11:0] table_height,
    input  wire        table_front_select,
    input  wire        active_full_mode,
    input  wire        active_quad_mode,
    input  wire        active_cpu_source,
    input  wire        current_front_select,
    input  wire        async_mode,
    input  wire        present_ack,
    input  wire        ddr_wr_done,
    input  wire        ddr_wr_error,
    output reg         cpu_active,
    output reg         cpu_frame_active,
    output reg         cpu_row_request,
    output reg         cpu_owner_active,
    output reg         cpu_fifo_rst_p,
    output reg         cpu_fifo_wr_en,
    output reg  [143:0] cpu_fifo_wr_data,
    output reg         cpu_ddr_wr_start,
    output reg  [32:0] cpu_wr_start_addr,
    output reg  [23:0] cpu_wr_len,
    output reg         frame_written_pulse,
    output reg         write_done_pulse,
    output reg         swap_done_pulse,
    output reg  [6:0]  frame_target_mask,
    output reg  [31:0] status_word,
    output reg  [31:0] word_count,
    output reg  [7:0]  error_code
);
    localparam S_IDLE       = 4'd0;
    localparam S_FRAME_OPEN = 4'd1;
    localparam S_ROW_RESET  = 4'd2;
    localparam S_RESET_WAIT = 4'd3;
    localparam S_RECV       = 4'd4;
    localparam S_REQ_WRITE  = 4'd5;
    localparam S_WAIT_WRITE = 4'd6;
    localparam S_FRAME_DONE = 4'd7;
    localparam S_WAIT_SWAP  = 4'd8;
    localparam S_ERROR      = 4'd9;
    localparam S_WAIT_OWNER = 4'd10;

    localparam E_NONE       = 8'h00;
    localparam E_TARGET     = 8'h01;
    localparam E_OWNER      = 8'h02;
    localparam E_ROW        = 8'h03;
    localparam E_DATA       = 8'h04;
    localparam E_DDR        = 8'h05;
    localparam E_FRAME      = 8'h06;

    reg [3:0] state;
    reg [2:0] target_id;
    reg [11:0] row_x, row_y;
    reg [11:0] row_words;
    reg [11:0] accepted_words;
    reg [1:0] pack_index;
    reg [23:0] pushed_beats;
    reg [31:0] word0, word1, word2;
    reg [23:0] row_beats;
    reg [31:0] back_base_latched, stride_latched;
    reg [11:0] width_latched, height_latched;
    reg front_select_latched;
    reg [11:0] background_next_y;
    reg background_complete;
    reg ddr_inflight;
    reg write_done_sticky, swap_done_sticky, error_sticky;
    reg frame_ready;
    reg [2:0] reset_wait;
    reg [31:0] control_count;

    wire begin_frame = control_write && control_value[0];
    wire begin_row   = control_write && control_value[1];
    wire end_row     = control_write && control_value[2];
    wire end_frame   = control_write && control_value[3];
    wire clear_status= control_write && control_value[4];
    wire abort_req   = control_write && control_value[5];
    wire control_valid = (control_value == 0) ||
                         ((control_value & (control_value - 1'b1)) == 0);
    wire control_illegal = control_write && !control_valid;
    wire row_geometry_ok = (row_x[2:0] == 3'd0) &&
                           (row_words != 0) && (row_words[1:0] == 2'd0) &&
                           ({1'b0,row_x} + ({1'b0,row_words} << 1) <= width_latched) &&
                           (row_y < height_latched);
    wire background_row_ok = background_complete ||
                             ((row_x == 0) && (row_words == (width_latched >> 1)) &&
                              (row_y == background_next_y));
    wire data_ready = (state == S_RECV) && !fifo_full &&
                      (accepted_words < row_words);
    // cpu_fifo_wr_en is registered: a packed beat is presented for the
    // following cycle, and is accepted only when the downstream FIFO is not
    // full. Count that actual interface fire, not merely pack completion.
    wire fifo_write_fire = cpu_fifo_wr_en && !fifo_full;

    always @(*) begin
        cpu_active = (state != S_IDLE);
        cpu_frame_active = (state != S_IDLE);
        cpu_row_request = async_mode && (state == S_WAIT_OWNER);
        cpu_owner_active = async_mode ?
                           ((state == S_ROW_RESET) || (state == S_RESET_WAIT) ||
                            (state == S_RECV) || (state == S_REQ_WRITE) ||
                            (state == S_WAIT_WRITE)) :
                           (state != S_IDLE);
        status_word = 32'd0;
        status_word[0] = (state != S_IDLE);
        status_word[1] = (state == S_RECV) || (state == S_ROW_RESET) ||
                         (state == S_RESET_WAIT) || (state == S_REQ_WRITE) ||
                         (state == S_WAIT_WRITE);
        status_word[2] = data_ready;
        status_word[3] = ddr_inflight;
        status_word[4] = write_done_sticky;
        status_word[5] = swap_done_sticky;
        status_word[6] = error_sticky;
        status_word[7] = frame_ready;
        status_word[11:8] = state;
        status_word[15:12] = error_code[3:0];
        status_word[23:16] = error_code;
        status_word[24] = background_complete;
        // IDLE+owner_grant is the software-visible handoff point.
        status_word[25] = (state == S_IDLE) && owner_grant;
        status_word[26] = async_mode && (state == S_WAIT_OWNER);
        status_word[27] = async_mode && row_owner_grant &&
                          ((state == S_WAIT_OWNER) || cpu_owner_active);
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= S_IDLE;
            target_id <= 3'd0;
            row_x <= 0; row_y <= 0; row_words <= 0;
            accepted_words <= 0; pack_index <= 0;
            pushed_beats <= 0;
            word0 <= 0; word1 <= 0; word2 <= 0;
            row_beats <= 0;
            back_base_latched <= 0; stride_latched <= 0;
            width_latched <= 0; height_latched <= 0;
            front_select_latched <= 0;
            background_next_y <= 0; background_complete <= 0;
            ddr_inflight <= 0; write_done_sticky <= 0;
            swap_done_sticky <= 0; error_sticky <= 0; frame_ready <= 0;
            reset_wait <= 0; control_count <= 0;
            error_code <= E_NONE;
            cpu_fifo_rst_p <= 0; cpu_fifo_wr_en <= 0;
            cpu_fifo_wr_data <= 0; cpu_ddr_wr_start <= 0;
            cpu_wr_start_addr <= 0; cpu_wr_len <= 0;
            frame_written_pulse <= 0; write_done_pulse <= 0;
            swap_done_pulse <= 0; frame_target_mask <= 0; word_count <= 0;
        end else begin
            cpu_fifo_rst_p <= 0;
            if (fifo_write_fire)
                cpu_fifo_wr_en <= 1'b0;
            cpu_ddr_wr_start <= 0;
            frame_written_pulse <= 0;
            write_done_pulse <= 0;
            swap_done_pulse <= 0;

            if (clear_status) begin
                write_done_sticky <= 0;
                swap_done_sticky <= 0;
                error_sticky <= 0;
                error_code <= E_NONE;
            end
            if (target_write && (state == S_IDLE))
                target_id <= target_value[2:0];
            if (xy_write) begin
                row_x <= xy_value[11:0];
                row_y <= xy_value[27:16];
            end
            if (row_words_write)
                row_words <= row_words_value[11:0];

            if (abort_req && (state != S_IDLE)) begin
                error_sticky <= 1'b1;
                error_code <= E_FRAME;
                state <= S_ERROR;
            end else begin
                case (state)
                    S_IDLE: begin
                        frame_ready <= 0;
                        if (begin_frame) begin
                            if ((target_id != target_value[2:0]) ||
                                ((target_value[2:0] != 3'd2) &&
                                 (target_value[2:0] != 3'd3))) begin
                                error_sticky <= 1'b1; error_code <= E_TARGET;
                            end else if (!owner_grant) begin
                                error_sticky <= 1'b1; error_code <= E_OWNER;
                            end else begin
                                back_base_latched <= table_back_base;
                                stride_latched <= table_stride_bytes;
                                width_latched <= table_width;
                                height_latched <= table_height;
                                front_select_latched <= table_front_select;
                                background_next_y <= 0;
                                background_complete <= 0;
                                accepted_words <= 0;
                                pushed_beats <= 0;
                                word_count <= 0;
                                write_done_sticky <= 0;
                                swap_done_sticky <= 0;
                                frame_ready <= 0;
                                frame_target_mask <= (target_id == 3'd2) ? 7'b0000100 : 7'b0001000;
                                error_sticky <= 0;
                                error_code <= E_NONE;
                                state <= S_FRAME_OPEN;
                            end
                        end
                    end
                    S_FRAME_OPEN: begin
                        if (begin_row) begin
                            if (!row_geometry_ok || !background_row_ok) begin
                                error_sticky <= 1'b1; error_code <= E_ROW; state <= S_ERROR;
                            end else if (async_mode && !row_owner_grant) begin
                                state <= S_WAIT_OWNER;
                            end else begin
                                accepted_words <= 0;
                                pushed_beats <= 0;
                                word_count <= 0;
                                pack_index <= 0;
                                row_beats <= row_words >> 2;
                                cpu_fifo_rst_p <= 1'b1;
                                cpu_fifo_wr_en <= 1'b0;
                                reset_wait <= 3'd4;
                                state <= S_RESET_WAIT;
                            end
                        end else if (end_frame) begin
                            if (background_complete) begin
                                frame_ready <= 1'b1;
                                frame_written_pulse <= 1'b1;
                                state <= S_FRAME_DONE;
                            end else begin
                                error_sticky <= 1'b1; error_code <= E_FRAME; state <= S_ERROR;
                            end
                        end
                    end
                    S_WAIT_OWNER: begin
                        if (row_owner_grant) begin
                            accepted_words <= 0;
                            pushed_beats <= 0;
                            word_count <= 0;
                            pack_index <= 0;
                            row_beats <= row_words >> 2;
                            cpu_fifo_rst_p <= 1'b1;
                            cpu_fifo_wr_en <= 1'b0;
                            reset_wait <= 3'd4;
                            state <= S_RESET_WAIT;
                        end
                    end
                    S_RESET_WAIT: begin
                        if (reset_wait == 0)
                            state <= S_RECV;
                        else
                            reset_wait <= reset_wait - 1'b1;
                    end
                    S_RECV: begin
                        if (fifo_write_fire)
                            pushed_beats <= pushed_beats + 1'b1;
                        if (data_write) begin
                            if (!data_ready) begin
                                error_sticky <= 1'b1; error_code <= E_DATA; state <= S_ERROR;
                            end else begin
                                accepted_words <= accepted_words + 1'b1;
                                word_count <= word_count + 1'b1;
                                if (pack_index == 0) word0 <= data_value;
                                else if (pack_index == 1) word1 <= data_value;
                                else if (pack_index == 2) word2 <= data_value;
                                else begin
                                    cpu_fifo_wr_data <= {16'hFFFF,
                                        word0[15:0], word0[31:16],
                                        word1[15:0], word1[31:16],
                                        word2[15:0], word2[31:16],
                                        data_value[15:0], data_value[31:16]
                                    };
                                    cpu_fifo_wr_en <= 1'b1;
                                end
                                pack_index <= pack_index + 1'b1;
                            end
                        end
                        if (end_row) begin
                            if ((accepted_words != row_words) ||
                                (pack_index != 0) ||
                                ((pushed_beats + (fifo_write_fire ? 1'b1 : 1'b0)) != row_beats) ||
                                (cpu_fifo_wr_en && fifo_full)) begin
                                error_sticky <= 1'b1; error_code <= E_ROW; state <= S_ERROR;
                            end else begin
                                cpu_wr_start_addr <= back_base_latched +
                                    (row_y * stride_latched) + (row_x << 1);
                                cpu_wr_len <= row_beats;
                                state <= S_REQ_WRITE;
                            end
                        end
                    end
                    S_REQ_WRITE: begin
                        cpu_ddr_wr_start <= 1'b1;
                        ddr_inflight <= 1'b1;
                        state <= S_WAIT_WRITE;
                    end
                    S_WAIT_WRITE: begin
                        if (ddr_wr_error) begin
                            ddr_inflight <= 0;
                            error_sticky <= 1'b1; error_code <= E_DDR; state <= S_ERROR;
                        end else if (ddr_wr_done) begin
                            ddr_inflight <= 0;
                            write_done_sticky <= 1'b1;
                            write_done_pulse <= 1'b1;
                            if (!background_complete && (row_y == (height_latched - 1'b1))) begin
                                background_complete <= 1'b1;
                            end else if (!background_complete) begin
                                background_next_y <= row_y + 1'b1;
                            end
                            state <= S_FRAME_OPEN;
                        end
                    end
                    S_FRAME_DONE: begin
                        state <= S_WAIT_SWAP;
                    end
                    S_WAIT_SWAP: begin
                        if (async_mode ? present_ack :
                            ((current_front_select != front_select_latched) &&
                             ((active_quad_mode || active_full_mode) &&
                              active_cpu_source))) begin
                            swap_done_sticky <= 1'b1;
                            swap_done_pulse <= 1'b1;
                            state <= S_IDLE;
                        end
                    end
                    S_ERROR: begin
                        if (ddr_inflight) begin
                            if (ddr_wr_error || ddr_wr_done) ddr_inflight <= 0;
                        end else begin
                            state <= S_IDLE;
                        end
                    end
                    default: state <= S_IDLE;
                endcase
                if (control_illegal) begin
                    error_sticky <= 1'b1;
                    error_code <= E_FRAME;
                    if (state != S_IDLE)
                        state <= S_ERROR;
                end
                if (data_write && !data_ready) begin
                    error_sticky <= 1'b1;
                    error_code <= E_DATA;
                    if (state != S_IDLE)
                        state <= S_ERROR;
                end
            end
        end
    end
endmodule

