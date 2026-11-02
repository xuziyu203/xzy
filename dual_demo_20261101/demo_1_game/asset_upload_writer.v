`include "enemy_config.vh"
// Fixed byte-addressed assets. No surface/present interface; one 128-byte
// transaction at a time. Shares the framebuffer write FIFO under a held lease.
module asset_upload_writer (
    input wire clk, rst_n,
    input wire [11:0] reg_addr,
    input wire reg_write,
    input wire [31:0] reg_wdata,
    output reg [31:0] reg_rdata,
    input wire owner_grant,
    output reg owner_active,
    input wire fifo_full, fifo_empty, ddr_idle, ddr_accept,
    input wire ddr_done, ddr_error,
    output reg fifo_reset, fifo_valid,
    output reg [143:0] fifo_data,
    output reg wr_start,
    output reg [32:0] wr_addr,
    output wire [23:0] wr_beats
);
    `include "enemy_catalog.vh"
    localparam [31:0] ABI = 32'h41530100;
    localparam IDLE=0, READY=1, RESET=2, RECEIVE=3, START=4,
               ACCEPT=5, RESPONSE=6, DRAIN=7, CLEAN=8;
    reg [3:0] state;
    reg [31:0] id_shadow, offset_shadow, asset_id, base, length;
    reg [31:0] committed, block_offset, done_offset, error_code;
    reg [5:0] words;
    reg [3:0] beats, reset_wait;
    reg [1:0] pack_index;
    reg [31:0] word0, word1, word2;
    reg done, abort_pending;
    wire control = reg_write && reg_addr == 12'h130;
    wire abort_now = control && reg_wdata == 32'd8;
    wire data_ready = state == RECEIVE && !fifo_valid && !fifo_full && words < 32;
    wire commit_ready = state == RECEIVE && words == 32 && beats == 8 && !fifo_valid;
    wire inflight = state == START || state == ACCEPT || state == RESPONSE;
    wire fifo_fire = fifo_valid && !fifo_full;
    wire [32:0] block_end = {1'b0,base} + {1'b0,offset_shadow} + 33'd128;
    assign wr_beats = 24'd8;

    always @* begin
        reg_rdata = 0;
        case (reg_addr)
            12'h120: reg_rdata = ABI;
            12'h124: reg_rdata = owner_active ? asset_id : id_shadow;
            12'h128: reg_rdata = offset_shadow;
            12'h134: reg_rdata = {24'd0, (state == IDLE && ddr_idle),
                (abort_pending || state == DRAIN || state == CLEAN),
                (error_code != 0), done, inflight, commit_ready, data_ready, owner_active};
            12'h138: reg_rdata = {26'd0,words};
            12'h13C: reg_rdata = {28'd0,beats};
            12'h140: reg_rdata = committed;
            12'h144: reg_rdata = done_offset;
            12'h148: reg_rdata = error_code;
            12'h14C: reg_rdata = base;
            12'h150: reg_rdata = length;
            12'h154: reg_rdata = 32'h41530204;
            12'h158: reg_rdata = `ASSET_SUPPORTED_MASK;
            12'h15c: reg_rdata = `ASSET_SUPPORTED_MASK_HI; // CMD: IDs 32..63
        endcase
    end

    task fail;
        input [31:0] code;
        begin
            if (error_code == 0) error_code <= code;
            if (owner_active) abort_pending <= 1;
        end
    endtask

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state<=IDLE; owner_active<=0; id_shadow<=0; offset_shadow<=0;
            asset_id<=0; base<=0; length<=0; committed<=0; block_offset<=0;
            done_offset<=32'hFFFFFFFF; error_code<=0; words<=0; beats<=0;
            reset_wait<=0; pack_index<=0; word0<=0; word1<=0; word2<=0;
            done<=0; abort_pending<=0; fifo_reset<=0; fifo_valid<=0;
            fifo_data<=0; wr_start<=0; wr_addr<=0;
        end else begin
            fifo_reset<=0; wr_start<=0;
            if (fifo_fire) begin fifo_valid<=0; beats<=beats+1'b1; end

            case (state)
                RESET: if (reset_wait != 0) begin
                           // Keep the shared FIFO in reset for the complete
                           // synchronizer interval; a one-cycle pulse leaves
                           // the vendor dual-clock FIFO's count bus unknown.
                           fifo_reset<=1;
                           reset_wait<=reset_wait-1'b1;
                       end
                       else if (fifo_empty && ddr_idle) state<=RECEIVE;
                START: begin wr_start<=1; state<=ACCEPT; end
                ACCEPT: if (ddr_accept) state<=RESPONSE;
                RESPONSE: if (ddr_error || ddr_done) begin
                    if (ddr_error) begin error_code<=7; abort_pending<=1; state<=DRAIN; end
                    else if (abort_pending || abort_now) state<=DRAIN;
                    else begin committed<=committed+128; done_offset<=block_offset;
                        done<=1; state<=READY; end
                end
                DRAIN: if (ddr_idle) begin
                    // No issued command remains. Drop pack residue/FIFO data only now.
                    fifo_valid<=0; pack_index<=0; fifo_reset<=1;
                    reset_wait<=15; state<=CLEAN;
                end
                CLEAN: if (reset_wait != 0) begin fifo_reset<=1; reset_wait<=reset_wait-1'b1; end
                       else if (ddr_idle && fifo_empty) begin
                           owner_active<=0; abort_pending<=0; state<=IDLE;
                       end
            endcase

            if (reg_write) begin
                case (reg_addr)
                    12'h124: if (!owner_active) id_shadow<=reg_wdata; else fail(1);
                    12'h128: if (state == READY) offset_shadow<=reg_wdata; else fail(2);
                    12'h12C: if (!data_ready || abort_pending) fail(3);
                    else begin
                        words<=words+1'b1;
                        case (pack_index)
                            0: word0<=reg_wdata;
                            1: word1<=reg_wdata;
                            2: word2<=reg_wdata;
                            3: begin
                                // Byte 0 is WDATA[7:0], NOT the frame writer's pixel order.
                                fifo_data<={16'hFFFF,reg_wdata,word2,word1,word0};
                                fifo_valid<=1;
                            end
                        endcase
                        pack_index<=pack_index+1'b1;
                    end
                    12'h130: case (reg_wdata)
                        1: if (state != IDLE || !owner_grant || !ddr_idle || !fifo_empty) fail(4);
                           else if (id_shadow < 1 || id_shadow > `ASSET_ID_MAX) fail(1);
                           else begin
                               asset_id<=id_shadow; owner_active<=1; state<=READY;
                               base <= fixed_asset_base(id_shadow);
                               length <= fixed_asset_length(id_shadow);
                               committed<=0; done<=0; error_code<=0; abort_pending<=0;
                               words<=0; beats<=0; done_offset<=32'hFFFFFFFF;
                           end
                        2: if (state != READY || !ddr_idle || abort_pending) fail(4);
                           else if (offset_shadow != committed || offset_shadow[6:0]!=0 ||
                                    offset_shadow > length-128 || block_end[32] ||
                                    block_end > ({1'b0,base}+{1'b0,length}) ||
                                    block_end > 33'h010000000) fail(5);
                           else begin
                               block_offset<=offset_shadow; wr_addr<={1'b0,base}+offset_shadow;
                               done<=0; words<=0; beats<=0; pack_index<=0; fifo_valid<=0;
                               fifo_reset<=1; reset_wait<=15; state<=RESET;
                           end
                        4: if (!commit_ready || abort_pending) fail(6);
                           else state<=START;
                        8: if (owner_active) abort_pending<=1;
                        16: if (state == READY && ddr_idle && !fifo_valid) begin
                                abort_pending<=1; state<=DRAIN;
                            end else fail(4);
                        32: if (state == IDLE) begin error_code<=0; done<=0; end else fail(4);
                        default: fail(6);
                    endcase
                    default: ;
                endcase
            end
            // An issued start, even before its delayed acceptance, must retire.
            // Cancellation wins over a not-yet-issued START in the same cycle.
            if ((abort_pending || abort_now) && owner_active &&
                state != ACCEPT && state != RESPONSE && state != CLEAN && state != DRAIN) begin
                wr_start<=0; fifo_valid<=0; state<=DRAIN;
            end
        end
    end
endmodule
