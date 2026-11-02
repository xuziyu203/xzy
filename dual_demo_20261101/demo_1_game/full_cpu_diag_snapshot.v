// FULL CPU stage-1 diagnostic snapshot. Observation only.
// FSD2 foundation patch: timeout, capture gating, terminal freeze and clear.
// Descriptor/ACK/AR/R association is completed below and is covered by the
// local diagnostic regression before any candidate build is started.
module full_cpu_diag_snapshot #(
    // Valid range: 1..2^32-1. Default is 10 seconds at 100 MHz.
    parameter [31:0] FRAME_TIMEOUT_CYCLES = 32'd1_000_000_000
) (
    input wire clk, input wire rst_n,
    input wire arm_pulse, input wire clear_pulse,
    input wire begin_frame_target2,
    input wire [31:0] begin_back_base, input wire [31:0] begin_front_base,
    input wire [31:0] begin_generation, input wire [6:0] begin_pending_mask,
    input wire cpu_frame_written_pulse, input wire cpu_write_done_pulse,
    input wire cpu_swap_done_pulse, input wire [31:0] cpu_status,
    input wire [31:0] cpu_word_count, input wire cpu_ddr_wr_start,
    input wire [32:0] cpu_wr_start_addr, input wire [23:0] cpu_wr_len,
    input wire cpu_write_select, input wire cpu_ddr_wr_done,
    input wire cpu_ddr_wr_error,
    input wire [31:0] awaddr, input wire [7:0] awlen,
    input wire awvalid, input wire awready,
    input wire [127:0] wdata, input wire [15:0] wstrb, input wire wlast,
    input wire wvalid, input wire wready,
    input wire bvalid, input wire bready, input wire [1:0] bresp,
    input wire descriptor_accept, input wire [31:0] descriptor_start_addr,
    input wire [31:0] surface_generation,
    input wire [6:0] surface_present_ack_mask,
    input wire [31:0] araddr, input wire arvalid, input wire arready,
    input wire [3:0] arid, input wire [127:0] rdata,
    input wire rvalid, input wire rready, input wire [3:0] rid,
    input wire [1:0] rresp, input wire rlast,
    output reg [31:0] diag_reg0, output reg [31:0] diag_reg1,
    output reg [31:0] diag_reg2, output reg [31:0] diag_reg3,
    output reg [31:0] diag_reg4, output reg [31:0] diag_reg5,
    output reg [31:0] diag_reg6, output reg [31:0] diag_reg7,
    output reg [31:0] diag_reg8, output reg [31:0] diag_reg9,
    output reg [31:0] diag_reg10, output reg [31:0] diag_reg11,
    output reg [31:0] diag_reg12, output reg [31:0] diag_reg13,
    output reg [31:0] diag_reg14, output reg [31:0] diag_reg15,
    output reg [31:0] diag_reg16, output reg [31:0] diag_reg17,
    output reg [31:0] diag_reg18, output reg [31:0] diag_reg19,
    output reg [31:0] diag_reg20
);
    localparam [3:0] ST_IDLE=4'd0, ST_ARMED=4'd1, ST_WRITE=4'd2,
                     ST_DESC=4'd3, ST_READ=4'd4, ST_FROZEN=4'd5,
                     ST_TIMEOUT=4'd6, ST_AMBIG=4'd7;
    reg [3:0] state;
    reg [31:0] frame_tag_counter, timeout_counter, frame_tag;
    reg [31:0] back_base, front_base, generation_start, generation_end;
    reg [6:0] pending_at_begin;
    reg [31:0] frame_word_count, frame_cpu_status;
    reg [32:0] write_start_addr;
    reg [23:0] write_start_len;
    reg [31:0] first_aw_addr, first_wdata, descriptor_addr, first_ar_addr,
               first_rdata, aw_count, w_count, b_count, ar_count, r_count;
    reg [7:0] first_aw_len;
    reg [15:0] first_wstrb;
    reg [1:0] first_bresp, first_rresp;
    reg [3:0] first_arid, first_rid;
    reg first_wlast, first_rlast, writer_active;
    reg frame_valid, frame_written_valid, descriptor_valid, read_sample_valid,
        present_valid, write_sample_valid, aw_sample_valid, b_sample_valid,
        ar_sample_valid, writer_error, owner_mismatch, timeout_seen,
        write_done_seen, swap_done_seen;
    reg association_error, descriptor_addr_match, ar_addr_match,
        read_id_match, generation_match, descriptor_capture_pending;
    wire capture_active = (state==ST_WRITE) || (state==ST_DESC) ||
                          (state==ST_READ);
    wire aw_fire = awvalid && awready && (writer_active || cpu_ddr_wr_start) &&
                   cpu_write_select;
    wire w_fire  = wvalid && wready && (writer_active || cpu_ddr_wr_start) &&
                   cpu_write_select;
    wire b_fire  = bvalid && bready && (writer_active || cpu_ddr_wr_start) &&
                   cpu_write_select;
    wire ar_fire = arvalid && arready && (descriptor_valid || descriptor_accept);
    wire r_fire  = rvalid && rready && (descriptor_valid || descriptor_accept);
    wire complete_now = frame_valid && frame_written_valid && descriptor_valid &&
                        read_sample_valid && present_valid && write_done_seen &&
                        descriptor_addr_match && ar_addr_match &&
                        read_id_match && generation_match && !writer_error &&
                        !owner_mismatch && !association_error;
    always @(*) begin
        diag_reg0 = 32'h46534432; // FSD2: foundation plus association checks
        diag_reg1 = 0;
        diag_reg1[3:0]=state; diag_reg1[4]=frame_valid;
        diag_reg1[5]=frame_written_valid; diag_reg1[6]=descriptor_valid;
        diag_reg1[7]=read_sample_valid; diag_reg1[8]=present_valid;
        diag_reg1[9]=write_sample_valid; diag_reg1[10]=aw_sample_valid;
        diag_reg1[11]=b_sample_valid; diag_reg1[12]=ar_sample_valid;
        diag_reg1[13]=write_done_seen; diag_reg1[14]=swap_done_seen;
        diag_reg1[15]=writer_error; diag_reg1[16]=owner_mismatch;
        diag_reg1[17]=timeout_seen; diag_reg1[18]=(state==ST_FROZEN);
        diag_reg1[19]=complete_now;
        // FSD2 association status. These bits were unused in FSD1.
        diag_reg1[20]=association_error;
        diag_reg1[21]=descriptor_addr_match;
        diag_reg1[22]=ar_addr_match;
        diag_reg1[23]=read_id_match;
        diag_reg1[24]=generation_match;
        diag_reg2=frame_tag;
        diag_reg3=0; diag_reg3[2:0]=3'd2;
        diag_reg3[11:8]=frame_cpu_status[11:8];
        diag_reg3[12]=frame_valid; diag_reg3[13]=frame_written_valid;
        diag_reg3[14]=write_done_seen; diag_reg3[15]=swap_done_seen;
        diag_reg3[16]=writer_error; diag_reg3[23:17]=pending_at_begin;
        diag_reg3[24]=association_error;
        diag_reg3[25]=descriptor_addr_match;
        diag_reg3[26]=ar_addr_match;
        diag_reg3[27]=read_id_match;
        diag_reg3[28]=generation_match;
        diag_reg4=back_base; diag_reg5=front_base;
        diag_reg6=generation_start; diag_reg7=generation_end;
        diag_reg8=write_start_addr[31:0]; diag_reg9=first_aw_addr;
        diag_reg10=first_wdata; diag_reg11={15'd0,first_wstrb};
        diag_reg11[16]=write_sample_valid; diag_reg11[17]=aw_sample_valid;
        diag_reg11[18]=b_sample_valid; diag_reg11[19]=first_wlast;
        diag_reg11[27:20]=first_aw_len;
        diag_reg12=aw_count; diag_reg13=w_count; diag_reg14=b_count;
        diag_reg14[31:30]=first_bresp;
        diag_reg15=descriptor_addr; diag_reg16=first_ar_addr;
        diag_reg17=first_rdata; diag_reg18=ar_count; diag_reg19=r_count;
        diag_reg19[31:30]=first_rresp; diag_reg20=frame_word_count;
    end
    task automatic clear_capture;
    begin
        frame_tag<=0; back_base<=0; front_base<=0; generation_start<=0;
        generation_end<=0; pending_at_begin<=0; frame_word_count<=0;
        frame_cpu_status<=0; write_start_addr<=0; write_start_len<=0;
        first_aw_addr<=0; first_aw_len<=0; first_wdata<=0;
        first_wstrb<=0; descriptor_addr<=0; first_ar_addr<=0;
        first_rdata<=0; first_bresp<=0; first_rresp<=0;
        first_arid<=0; first_rid<=0; first_wlast<=0; first_rlast<=0;
        timeout_counter<=0; frame_valid<=0; frame_written_valid<=0;
        descriptor_valid<=0; read_sample_valid<=0; present_valid<=0;
        write_sample_valid<=0; aw_sample_valid<=0; b_sample_valid<=0;
        ar_sample_valid<=0; writer_error<=0; owner_mismatch<=0;
        association_error<=0; descriptor_addr_match<=0; ar_addr_match<=0;
        read_id_match<=0; generation_match<=0; descriptor_capture_pending<=0;
        timeout_seen<=0; write_done_seen<=0; swap_done_seen<=0;
        writer_active<=0; aw_count<=0; w_count<=0; b_count<=0;
        ar_count<=0; r_count<=0;
    end
    endtask
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state<=ST_IDLE; frame_tag_counter<=0;
            back_base<=0; front_base<=0; generation_start<=0;
            generation_end<=0; pending_at_begin<=0; frame_word_count<=0;
            frame_cpu_status<=0; write_start_addr<=0; write_start_len<=0;
            first_aw_addr<=0; first_aw_len<=0; first_wdata<=0;
            first_wstrb<=0; descriptor_addr<=0; first_ar_addr<=0;
            first_rdata<=0; first_bresp<=0; first_rresp<=0;
            first_arid<=0; first_rid<=0; first_wlast<=0; first_rlast<=0;
            clear_capture();
        end else if (clear_pulse) begin
            state<=ST_IDLE; clear_capture();
        end else if (arm_pulse) begin
            state<=ST_ARMED; clear_capture();
        end else if ((state==ST_ARMED) || capture_active) begin
            // ARMED waits indefinitely for the next accepted BEGIN. The
            // frame budget starts at that BEGIN edge and only runs while the
            // selected frame is being captured.
            if (complete_now) begin
                state<=ST_FROZEN;
                writer_active<=1'b0;
            end else if (capture_active &&
                         (timeout_counter >= FRAME_TIMEOUT_CYCLES - 32'd1)) begin
                timeout_seen<=1'b1;
                state<=ST_TIMEOUT;
                writer_active<=1'b0;
            end else begin
                if (capture_active) timeout_counter<=timeout_counter+1'b1;
            if ((state==ST_ARMED) && begin_frame_target2) begin
                frame_valid<=1'b1; frame_tag<=frame_tag_counter;
                frame_tag_counter<=frame_tag_counter+1'b1;
                back_base<=begin_back_base; front_base<=begin_front_base;
                generation_start<=begin_generation;
                pending_at_begin<=begin_pending_mask; timeout_counter<=0;
                if (begin_pending_mask[2]) begin
                    owner_mismatch<=1'b1; state<=ST_AMBIG;
                end else state<=ST_WRITE;
            end
            if ((state==ST_WRITE) && cpu_ddr_wr_start) begin
                writer_active<=1'b1; write_start_addr<=cpu_wr_start_addr;
                write_start_len<=cpu_wr_len;
                if (cpu_wr_start_addr[31:0] != back_base)
                    association_error<=1'b1;
            end
            if (writer_active || cpu_ddr_wr_start) begin
                if (!cpu_write_select && (awvalid||wvalid||bvalid))
                    owner_mismatch<=1'b1;
                if (aw_fire) begin
                    aw_count<=aw_count+1'b1;
                    if (!aw_sample_valid) begin
                        first_aw_addr<=awaddr; first_aw_len<=awlen;
                        aw_sample_valid<=1'b1;
                            if (awaddr != back_base)
                                association_error<=1'b1;
                    end
                end
                if (w_fire) begin
                    w_count<=w_count+1'b1;
                    if (!write_sample_valid) begin
                        first_wdata<=wdata[31:0]; first_wstrb<=wstrb;
                        first_wlast<=wlast; write_sample_valid<=1'b1;
                    end
                end
                if (b_fire) begin
                    b_count<=b_count+1'b1;
                    if (!b_sample_valid) begin first_bresp<=bresp; b_sample_valid<=1'b1; end
                    if (bresp!=2'b00) writer_error<=1'b1;
                end
                if (cpu_ddr_wr_done || cpu_ddr_wr_error) writer_active<=1'b0;
                if (cpu_ddr_wr_error) writer_error<=1'b1;
            end
            if (frame_valid && capture_active && cpu_write_done_pulse)
                write_done_seen<=1'b1;
            if (frame_valid && capture_active && cpu_swap_done_pulse)
                swap_done_seen<=1'b1;
            if (cpu_frame_written_pulse && (state==ST_WRITE)) begin
                frame_written_valid<=1'b1; frame_word_count<=cpu_word_count;
                frame_cpu_status<=cpu_status;
                if (cpu_status[6]) writer_error<=1'b1;
                state<=ST_DESC;
            end
            if (frame_valid && descriptor_accept && (state != ST_DESC))
                association_error<=1'b1;
            if ((state==ST_DESC) && descriptor_accept) begin
                descriptor_valid<=1'b1;
                descriptor_capture_pending<=1'b1;
                state<=ST_READ;
            end
            // ddr_rd_buffer registers descriptor_start_addr_debug on the
            // descriptor_take edge. Sample it one core clock later so the
            // diagnostic cannot pair the previous descriptor address with
            // the current acceptance event.
            if (descriptor_capture_pending) begin
                descriptor_addr<=descriptor_start_addr;
                descriptor_addr_match <=
                    (descriptor_start_addr == back_base[31:0]);
                if (descriptor_start_addr != back_base[31:0])
                    association_error<=1'b1;
                descriptor_capture_pending<=1'b0;
            end
            if (ar_fire) begin
                    ar_count<=ar_count+1'b1;
                        if (!ar_sample_valid) begin
                        first_ar_addr<=araddr; first_arid<=arid;
                        ar_sample_valid<=1'b1;
                            ar_addr_match <= (araddr == back_base[31:0]);
                            if (araddr != back_base[31:0])
                                association_error<=1'b1;
                    end
            end
            if (r_fire) begin
                    r_count<=r_count+1'b1;
                        if (!read_sample_valid) begin
                        first_rdata<=rdata[31:0]; first_rresp<=rresp;
                        first_rid<=rid; first_rlast<=rlast;
                        read_sample_valid<=1'b1;
                            read_id_match <= (ar_sample_valid || ar_fire) &&
                                             (rid == (ar_sample_valid ? first_arid : arid)) &&
                                             (rresp == 2'b00);
                            if ((!ar_sample_valid && !ar_fire) ||
                                (rid != (ar_sample_valid ? first_arid : arid)) ||
                                (rresp != 2'b00))
                                association_error<=1'b1;
                    end
            end
            if (frame_valid && surface_present_ack_mask[2] &&
                !descriptor_valid && !descriptor_accept)
                association_error<=1'b1;
            if ((state==ST_DESC || state==ST_READ) && descriptor_valid &&
                surface_present_ack_mask[2] && frame_written_valid) begin
                present_valid<=1'b1; generation_end<=surface_generation;
                generation_match <= (surface_generation != generation_start);
                if ((surface_generation == generation_start) ||
                    (surface_present_ack_mask != 7'b0000100))
                    association_error<=1'b1;
            end
            end
        end
        // IDLE/FROZEN/TIMEOUT/AMBIG hold every captured field. Only reset,
        // CLEAR or ARM may modify a terminal snapshot.
    end
endmodule
