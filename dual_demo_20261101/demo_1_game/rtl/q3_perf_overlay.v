// Q3 performance overlay: pipelined raster and pixel mix.
// Snapshot fields remain sampled once per pixel transaction.
module q3_perf_overlay (
    input wire clk,
    input wire rst_n,
    input  wire [15:0] pixel_in,
    input  wire        de_in,
    input  wire        hs_in,
    input  wire        vs_in,
    input  wire        quad_enable,
    input  wire [11:0] scan_x,
    input  wire [11:0] scan_y,
    input  wire [223:0] snapshot_pix,
    input  wire        snapshot_valid_pix,
    // Registered game-HUD y bands of this pixel's line: [0] 32..55 FPS, [1] 64..87 OBJECTS,
    // [2] 96..119 LIVES, [3] 128..151 SCORE, [4] 520..543 GAME OVER.
    input  wire [4:0]  game_band,
    output wire [15:0] pixel_out,
    output wire        de_out,
    output wire        hs_out,
    output wire        vs_out
);
    localparam [15:0] PANEL_BG        = 16'h1082;
    localparam [15:0] PANEL_TEXT      = 16'hEF7D;
    localparam [15:0] PANEL_TITLE     = 16'hEA69;
    localparam [15:0] PANEL_GRID      = 16'hB187;
    localparam [15:0] PANEL_CROSSHAIR = 16'hF800;

    wire q3_active = quad_enable && de_in &&
                     (scan_x >= 12'd960) && (scan_x < 12'd1920) &&
                     (scan_y >= 12'd540) && (scan_y < 12'd1080);
    wire [11:0] local_x = scan_x - 12'd960;
    wire [11:0] local_y = scan_y - 12'd540;
    // Existing four-pixel central crosshair. It is deliberately applied last.
    wire crosshair = quad_enable && de_in &&
                     (((scan_x >= 12'd958) && (scan_x < 12'd962)) ||
                      ((scan_y >= 12'd538) && (scan_y < 12'd542)));

    wire [31:0] cpu_row_word    = snapshot_pix[31:0];
    wire [31:0] blit_row_word   = snapshot_pix[63:32];
    wire [31:0] rect_row_word   = snapshot_pix[95:64];
    wire [31:0] blit_time_word  = snapshot_pix[127:96];
    wire [31:0] rect_time_word  = snapshot_pix[159:128];
    wire [31:0] panel_flags     = snapshot_pix[191:160];
    wire [31:0] panel_error     = snapshot_pix[223:192];

    function [15:0] body_font_row;
        input [7:0] ch;
        input [5:0] row;
        begin
            body_font_row = 16'd0;
            case (ch)
                8'h20: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0000;
                        6'd4: body_font_row = 16'h0000;
                        6'd5: body_font_row = 16'h0000;
                        6'd6: body_font_row = 16'h0000;
                        6'd7: body_font_row = 16'h0000;
                        6'd8: body_font_row = 16'h0000;
                        6'd9: body_font_row = 16'h0000;
                        6'd10: body_font_row = 16'h0000;
                        6'd11: body_font_row = 16'h0000;
                        6'd12: body_font_row = 16'h0000;
                        6'd13: body_font_row = 16'h0000;
                        6'd14: body_font_row = 16'h0000;
                        6'd15: body_font_row = 16'h0000;
                        6'd16: body_font_row = 16'h0000;
                        6'd17: body_font_row = 16'h0000;
                        6'd18: body_font_row = 16'h0000;
                        6'd19: body_font_row = 16'h0000;
                        6'd20: body_font_row = 16'h0000;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h2D: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0000;
                        6'd4: body_font_row = 16'h0000;
                        6'd5: body_font_row = 16'h0000;
                        6'd6: body_font_row = 16'h0000;
                        6'd7: body_font_row = 16'h0000;
                        6'd8: body_font_row = 16'h0000;
                        6'd9: body_font_row = 16'h0000;
                        6'd10: body_font_row = 16'h0000;
                        6'd11: body_font_row = 16'h0000;
                        6'd12: body_font_row = 16'h0000;
                        6'd13: body_font_row = 16'h07E0;
                        6'd14: body_font_row = 16'h07E0;
                        6'd15: body_font_row = 16'h0000;
                        6'd16: body_font_row = 16'h0000;
                        6'd17: body_font_row = 16'h0000;
                        6'd18: body_font_row = 16'h0000;
                        6'd19: body_font_row = 16'h0000;
                        6'd20: body_font_row = 16'h0000;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h2E: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0000;
                        6'd4: body_font_row = 16'h0000;
                        6'd5: body_font_row = 16'h0000;
                        6'd6: body_font_row = 16'h0000;
                        6'd7: body_font_row = 16'h0000;
                        6'd8: body_font_row = 16'h0000;
                        6'd9: body_font_row = 16'h0000;
                        6'd10: body_font_row = 16'h0000;
                        6'd11: body_font_row = 16'h0000;
                        6'd12: body_font_row = 16'h0000;
                        6'd13: body_font_row = 16'h0000;
                        6'd14: body_font_row = 16'h0000;
                        6'd15: body_font_row = 16'h0000;
                        6'd16: body_font_row = 16'h0000;
                        6'd17: body_font_row = 16'h01C0;
                        6'd18: body_font_row = 16'h01C0;
                        6'd19: body_font_row = 16'h01C0;
                        6'd20: body_font_row = 16'h01C0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h30: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h03E0;
                        6'd4: body_font_row = 16'h07F0;
                        6'd5: body_font_row = 16'h0E38;
                        6'd6: body_font_row = 16'h1C18;
                        6'd7: body_font_row = 16'h1C18;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h181C;
                        6'd10: body_font_row = 16'h181C;
                        6'd11: body_font_row = 16'h199C;
                        6'd12: body_font_row = 16'h19DC;
                        6'd13: body_font_row = 16'h181C;
                        6'd14: body_font_row = 16'h181C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h1C18;
                        6'd17: body_font_row = 16'h1C18;
                        6'd18: body_font_row = 16'h0E38;
                        6'd19: body_font_row = 16'h07F0;
                        6'd20: body_font_row = 16'h03E0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h31: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h03C0;
                        6'd4: body_font_row = 16'h0FC0;
                        6'd5: body_font_row = 16'h0CC0;
                        6'd6: body_font_row = 16'h00C0;
                        6'd7: body_font_row = 16'h00C0;
                        6'd8: body_font_row = 16'h00C0;
                        6'd9: body_font_row = 16'h00C0;
                        6'd10: body_font_row = 16'h00C0;
                        6'd11: body_font_row = 16'h00C0;
                        6'd12: body_font_row = 16'h00C0;
                        6'd13: body_font_row = 16'h00C0;
                        6'd14: body_font_row = 16'h00C0;
                        6'd15: body_font_row = 16'h00C0;
                        6'd16: body_font_row = 16'h00C0;
                        6'd17: body_font_row = 16'h00C0;
                        6'd18: body_font_row = 16'h00C0;
                        6'd19: body_font_row = 16'h0FFC;
                        6'd20: body_font_row = 16'h0FFC;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h32: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0FC0;
                        6'd4: body_font_row = 16'h1FF0;
                        6'd5: body_font_row = 16'h1878;
                        6'd6: body_font_row = 16'h0038;
                        6'd7: body_font_row = 16'h0018;
                        6'd8: body_font_row = 16'h0018;
                        6'd9: body_font_row = 16'h0018;
                        6'd10: body_font_row = 16'h0038;
                        6'd11: body_font_row = 16'h0070;
                        6'd12: body_font_row = 16'h0060;
                        6'd13: body_font_row = 16'h00C0;
                        6'd14: body_font_row = 16'h01C0;
                        6'd15: body_font_row = 16'h0380;
                        6'd16: body_font_row = 16'h0700;
                        6'd17: body_font_row = 16'h0E00;
                        6'd18: body_font_row = 16'h1C00;
                        6'd19: body_font_row = 16'h1FF8;
                        6'd20: body_font_row = 16'h1FF8;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h33: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h07C0;
                        6'd4: body_font_row = 16'h1FF0;
                        6'd5: body_font_row = 16'h1878;
                        6'd6: body_font_row = 16'h0038;
                        6'd7: body_font_row = 16'h0018;
                        6'd8: body_font_row = 16'h0018;
                        6'd9: body_font_row = 16'h0038;
                        6'd10: body_font_row = 16'h0070;
                        6'd11: body_font_row = 16'h03E0;
                        6'd12: body_font_row = 16'h03E0;
                        6'd13: body_font_row = 16'h0038;
                        6'd14: body_font_row = 16'h0018;
                        6'd15: body_font_row = 16'h001C;
                        6'd16: body_font_row = 16'h001C;
                        6'd17: body_font_row = 16'h0018;
                        6'd18: body_font_row = 16'h1038;
                        6'd19: body_font_row = 16'h1FF0;
                        6'd20: body_font_row = 16'h0FE0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h34: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0070;
                        6'd4: body_font_row = 16'h00F0;
                        6'd5: body_font_row = 16'h00F0;
                        6'd6: body_font_row = 16'h01B0;
                        6'd7: body_font_row = 16'h0330;
                        6'd8: body_font_row = 16'h0330;
                        6'd9: body_font_row = 16'h0630;
                        6'd10: body_font_row = 16'h0630;
                        6'd11: body_font_row = 16'h0C30;
                        6'd12: body_font_row = 16'h1C30;
                        6'd13: body_font_row = 16'h1830;
                        6'd14: body_font_row = 16'h3030;
                        6'd15: body_font_row = 16'h3FFC;
                        6'd16: body_font_row = 16'h3FFC;
                        6'd17: body_font_row = 16'h0030;
                        6'd18: body_font_row = 16'h0030;
                        6'd19: body_font_row = 16'h0030;
                        6'd20: body_font_row = 16'h0030;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h35: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FF0;
                        6'd4: body_font_row = 16'h1FF0;
                        6'd5: body_font_row = 16'h1C00;
                        6'd6: body_font_row = 16'h1C00;
                        6'd7: body_font_row = 16'h1C00;
                        6'd8: body_font_row = 16'h1C00;
                        6'd9: body_font_row = 16'h1FC0;
                        6'd10: body_font_row = 16'h1FF0;
                        6'd11: body_font_row = 16'h1878;
                        6'd12: body_font_row = 16'h0038;
                        6'd13: body_font_row = 16'h0018;
                        6'd14: body_font_row = 16'h001C;
                        6'd15: body_font_row = 16'h001C;
                        6'd16: body_font_row = 16'h0018;
                        6'd17: body_font_row = 16'h0038;
                        6'd18: body_font_row = 16'h1078;
                        6'd19: body_font_row = 16'h1FF0;
                        6'd20: body_font_row = 16'h0FC0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h36: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h01F0;
                        6'd4: body_font_row = 16'h07F8;
                        6'd5: body_font_row = 16'h0E18;
                        6'd6: body_font_row = 16'h0C00;
                        6'd7: body_font_row = 16'h1800;
                        6'd8: body_font_row = 16'h1800;
                        6'd9: body_font_row = 16'h19E0;
                        6'd10: body_font_row = 16'h1FF0;
                        6'd11: body_font_row = 16'h1E38;
                        6'd12: body_font_row = 16'h1C1C;
                        6'd13: body_font_row = 16'h181C;
                        6'd14: body_font_row = 16'h181C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h181C;
                        6'd17: body_font_row = 16'h1C1C;
                        6'd18: body_font_row = 16'h0E38;
                        6'd19: body_font_row = 16'h07F0;
                        6'd20: body_font_row = 16'h03E0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h37: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FFC;
                        6'd4: body_font_row = 16'h1FF8;
                        6'd5: body_font_row = 16'h0018;
                        6'd6: body_font_row = 16'h0038;
                        6'd7: body_font_row = 16'h0030;
                        6'd8: body_font_row = 16'h0030;
                        6'd9: body_font_row = 16'h0070;
                        6'd10: body_font_row = 16'h0060;
                        6'd11: body_font_row = 16'h0060;
                        6'd12: body_font_row = 16'h00E0;
                        6'd13: body_font_row = 16'h00C0;
                        6'd14: body_font_row = 16'h01C0;
                        6'd15: body_font_row = 16'h0180;
                        6'd16: body_font_row = 16'h0180;
                        6'd17: body_font_row = 16'h0380;
                        6'd18: body_font_row = 16'h0300;
                        6'd19: body_font_row = 16'h0700;
                        6'd20: body_font_row = 16'h0700;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h38: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h03E0;
                        6'd4: body_font_row = 16'h0FF0;
                        6'd5: body_font_row = 16'h1E38;
                        6'd6: body_font_row = 16'h1C18;
                        6'd7: body_font_row = 16'h181C;
                        6'd8: body_font_row = 16'h1818;
                        6'd9: body_font_row = 16'h1C18;
                        6'd10: body_font_row = 16'h0E38;
                        6'd11: body_font_row = 16'h07E0;
                        6'd12: body_font_row = 16'h07F0;
                        6'd13: body_font_row = 16'h1C38;
                        6'd14: body_font_row = 16'h181C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h181C;
                        6'd17: body_font_row = 16'h181C;
                        6'd18: body_font_row = 16'h1C38;
                        6'd19: body_font_row = 16'h0FF8;
                        6'd20: body_font_row = 16'h07E0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h39: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h07C0;
                        6'd4: body_font_row = 16'h0FF0;
                        6'd5: body_font_row = 16'h1C38;
                        6'd6: body_font_row = 16'h1818;
                        6'd7: body_font_row = 16'h1818;
                        6'd8: body_font_row = 16'h381C;
                        6'd9: body_font_row = 16'h381C;
                        6'd10: body_font_row = 16'h181C;
                        6'd11: body_font_row = 16'h181C;
                        6'd12: body_font_row = 16'h1C3C;
                        6'd13: body_font_row = 16'h0FFC;
                        6'd14: body_font_row = 16'h07DC;
                        6'd15: body_font_row = 16'h0018;
                        6'd16: body_font_row = 16'h0018;
                        6'd17: body_font_row = 16'h0038;
                        6'd18: body_font_row = 16'h0870;
                        6'd19: body_font_row = 16'h0FE0;
                        6'd20: body_font_row = 16'h07C0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h41: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h01C0;
                        6'd4: body_font_row = 16'h03C0;
                        6'd5: body_font_row = 16'h03C0;
                        6'd6: body_font_row = 16'h0360;
                        6'd7: body_font_row = 16'h0760;
                        6'd8: body_font_row = 16'h0660;
                        6'd9: body_font_row = 16'h0670;
                        6'd10: body_font_row = 16'h0630;
                        6'd11: body_font_row = 16'h0E30;
                        6'd12: body_font_row = 16'h0C38;
                        6'd13: body_font_row = 16'h0C38;
                        6'd14: body_font_row = 16'h1C18;
                        6'd15: body_font_row = 16'h1FF8;
                        6'd16: body_font_row = 16'h1FFC;
                        6'd17: body_font_row = 16'h381C;
                        6'd18: body_font_row = 16'h380C;
                        6'd19: body_font_row = 16'h300E;
                        6'd20: body_font_row = 16'h300E;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h42: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FE0;
                        6'd4: body_font_row = 16'h1FF0;
                        6'd5: body_font_row = 16'h1838;
                        6'd6: body_font_row = 16'h181C;
                        6'd7: body_font_row = 16'h181C;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h1818;
                        6'd10: body_font_row = 16'h1838;
                        6'd11: body_font_row = 16'h1FF0;
                        6'd12: body_font_row = 16'h1FF0;
                        6'd13: body_font_row = 16'h1818;
                        6'd14: body_font_row = 16'h180C;
                        6'd15: body_font_row = 16'h180C;
                        6'd16: body_font_row = 16'h180C;
                        6'd17: body_font_row = 16'h180C;
                        6'd18: body_font_row = 16'h181C;
                        6'd19: body_font_row = 16'h1FF8;
                        6'd20: body_font_row = 16'h1FF0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h43: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h01F0;
                        6'd4: body_font_row = 16'h07FC;
                        6'd5: body_font_row = 16'h0F0C;
                        6'd6: body_font_row = 16'h0E00;
                        6'd7: body_font_row = 16'h1C00;
                        6'd8: body_font_row = 16'h1C00;
                        6'd9: body_font_row = 16'h1800;
                        6'd10: body_font_row = 16'h1800;
                        6'd11: body_font_row = 16'h1800;
                        6'd12: body_font_row = 16'h1800;
                        6'd13: body_font_row = 16'h1800;
                        6'd14: body_font_row = 16'h1800;
                        6'd15: body_font_row = 16'h1C00;
                        6'd16: body_font_row = 16'h1C00;
                        6'd17: body_font_row = 16'h0C00;
                        6'd18: body_font_row = 16'h0F0C;
                        6'd19: body_font_row = 16'h07FC;
                        6'd20: body_font_row = 16'h01F0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h44: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1F80;
                        6'd4: body_font_row = 16'h1FE0;
                        6'd5: body_font_row = 16'h1870;
                        6'd6: body_font_row = 16'h1838;
                        6'd7: body_font_row = 16'h1818;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h181C;
                        6'd10: body_font_row = 16'h181C;
                        6'd11: body_font_row = 16'h181C;
                        6'd12: body_font_row = 16'h181C;
                        6'd13: body_font_row = 16'h181C;
                        6'd14: body_font_row = 16'h181C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h1818;
                        6'd17: body_font_row = 16'h1838;
                        6'd18: body_font_row = 16'h1870;
                        6'd19: body_font_row = 16'h1FE0;
                        6'd20: body_font_row = 16'h1FC0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h45: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FFC;
                        6'd4: body_font_row = 16'h1FFC;
                        6'd5: body_font_row = 16'h1C00;
                        6'd6: body_font_row = 16'h1C00;
                        6'd7: body_font_row = 16'h1C00;
                        6'd8: body_font_row = 16'h1C00;
                        6'd9: body_font_row = 16'h1C00;
                        6'd10: body_font_row = 16'h1C00;
                        6'd11: body_font_row = 16'h1FF8;
                        6'd12: body_font_row = 16'h1FF8;
                        6'd13: body_font_row = 16'h1C00;
                        6'd14: body_font_row = 16'h1C00;
                        6'd15: body_font_row = 16'h1C00;
                        6'd16: body_font_row = 16'h1C00;
                        6'd17: body_font_row = 16'h1C00;
                        6'd18: body_font_row = 16'h1C00;
                        6'd19: body_font_row = 16'h1FFC;
                        6'd20: body_font_row = 16'h1FFC;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h46: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0FFC;
                        6'd4: body_font_row = 16'h0FFC;
                        6'd5: body_font_row = 16'h0C00;
                        6'd6: body_font_row = 16'h0C00;
                        6'd7: body_font_row = 16'h0C00;
                        6'd8: body_font_row = 16'h0C00;
                        6'd9: body_font_row = 16'h0C00;
                        6'd10: body_font_row = 16'h0C00;
                        6'd11: body_font_row = 16'h0FF8;
                        6'd12: body_font_row = 16'h0FF8;
                        6'd13: body_font_row = 16'h0C00;
                        6'd14: body_font_row = 16'h0C00;
                        6'd15: body_font_row = 16'h0C00;
                        6'd16: body_font_row = 16'h0C00;
                        6'd17: body_font_row = 16'h0C00;
                        6'd18: body_font_row = 16'h0C00;
                        6'd19: body_font_row = 16'h0C00;
                        6'd20: body_font_row = 16'h0C00;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h47: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h01F0;
                        6'd4: body_font_row = 16'h07F8;
                        6'd5: body_font_row = 16'h0E08;
                        6'd6: body_font_row = 16'h1C00;
                        6'd7: body_font_row = 16'h1800;
                        6'd8: body_font_row = 16'h1800;
                        6'd9: body_font_row = 16'h3800;
                        6'd10: body_font_row = 16'h3800;
                        6'd11: body_font_row = 16'h3800;
                        6'd12: body_font_row = 16'h387C;
                        6'd13: body_font_row = 16'h387C;
                        6'd14: body_font_row = 16'h380C;
                        6'd15: body_font_row = 16'h180C;
                        6'd16: body_font_row = 16'h180C;
                        6'd17: body_font_row = 16'h1C0C;
                        6'd18: body_font_row = 16'h0E1C;
                        6'd19: body_font_row = 16'h07FC;
                        6'd20: body_font_row = 16'h03F0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h48: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h181C;
                        6'd4: body_font_row = 16'h181C;
                        6'd5: body_font_row = 16'h181C;
                        6'd6: body_font_row = 16'h181C;
                        6'd7: body_font_row = 16'h181C;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h181C;
                        6'd10: body_font_row = 16'h181C;
                        6'd11: body_font_row = 16'h1FFC;
                        6'd12: body_font_row = 16'h1FFC;
                        6'd13: body_font_row = 16'h181C;
                        6'd14: body_font_row = 16'h181C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h181C;
                        6'd17: body_font_row = 16'h181C;
                        6'd18: body_font_row = 16'h181C;
                        6'd19: body_font_row = 16'h181C;
                        6'd20: body_font_row = 16'h181C;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h49: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FF8;
                        6'd4: body_font_row = 16'h1FF8;
                        6'd5: body_font_row = 16'h0180;
                        6'd6: body_font_row = 16'h0180;
                        6'd7: body_font_row = 16'h0180;
                        6'd8: body_font_row = 16'h0180;
                        6'd9: body_font_row = 16'h0180;
                        6'd10: body_font_row = 16'h0180;
                        6'd11: body_font_row = 16'h0180;
                        6'd12: body_font_row = 16'h0180;
                        6'd13: body_font_row = 16'h0180;
                        6'd14: body_font_row = 16'h0180;
                        6'd15: body_font_row = 16'h0180;
                        6'd16: body_font_row = 16'h0180;
                        6'd17: body_font_row = 16'h0180;
                        6'd18: body_font_row = 16'h0180;
                        6'd19: body_font_row = 16'h1FF8;
                        6'd20: body_font_row = 16'h1FF8;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h4A: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h07F0;
                        6'd4: body_font_row = 16'h07F0;
                        6'd5: body_font_row = 16'h0030;
                        6'd6: body_font_row = 16'h0030;
                        6'd7: body_font_row = 16'h0030;
                        6'd8: body_font_row = 16'h0030;
                        6'd9: body_font_row = 16'h0030;
                        6'd10: body_font_row = 16'h0030;
                        6'd11: body_font_row = 16'h0030;
                        6'd12: body_font_row = 16'h0030;
                        6'd13: body_font_row = 16'h0030;
                        6'd14: body_font_row = 16'h0030;
                        6'd15: body_font_row = 16'h0030;
                        6'd16: body_font_row = 16'h0030;
                        6'd17: body_font_row = 16'h2070;
                        6'd18: body_font_row = 16'h3870;
                        6'd19: body_font_row = 16'h3FE0;
                        6'd20: body_font_row = 16'h0FC0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h4B: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h180E;
                        6'd4: body_font_row = 16'h181C;
                        6'd5: body_font_row = 16'h1838;
                        6'd6: body_font_row = 16'h1870;
                        6'd7: body_font_row = 16'h18E0;
                        6'd8: body_font_row = 16'h19C0;
                        6'd9: body_font_row = 16'h1B80;
                        6'd10: body_font_row = 16'h1F80;
                        6'd11: body_font_row = 16'h1F80;
                        6'd12: body_font_row = 16'h1DC0;
                        6'd13: body_font_row = 16'h18C0;
                        6'd14: body_font_row = 16'h18E0;
                        6'd15: body_font_row = 16'h1870;
                        6'd16: body_font_row = 16'h1830;
                        6'd17: body_font_row = 16'h1838;
                        6'd18: body_font_row = 16'h181C;
                        6'd19: body_font_row = 16'h180C;
                        6'd20: body_font_row = 16'h180E;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h4C: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1C00;
                        6'd4: body_font_row = 16'h1C00;
                        6'd5: body_font_row = 16'h1C00;
                        6'd6: body_font_row = 16'h1C00;
                        6'd7: body_font_row = 16'h1C00;
                        6'd8: body_font_row = 16'h1C00;
                        6'd9: body_font_row = 16'h1C00;
                        6'd10: body_font_row = 16'h1C00;
                        6'd11: body_font_row = 16'h1C00;
                        6'd12: body_font_row = 16'h1C00;
                        6'd13: body_font_row = 16'h1C00;
                        6'd14: body_font_row = 16'h1C00;
                        6'd15: body_font_row = 16'h1C00;
                        6'd16: body_font_row = 16'h1C00;
                        6'd17: body_font_row = 16'h1C00;
                        6'd18: body_font_row = 16'h1C00;
                        6'd19: body_font_row = 16'h1FFC;
                        6'd20: body_font_row = 16'h1FFC;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h4D: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h381C;
                        6'd4: body_font_row = 16'h3C1C;
                        6'd5: body_font_row = 16'h3C3C;
                        6'd6: body_font_row = 16'h3C3C;
                        6'd7: body_font_row = 16'h363C;
                        6'd8: body_font_row = 16'h366C;
                        6'd9: body_font_row = 16'h326C;
                        6'd10: body_font_row = 16'h336C;
                        6'd11: body_font_row = 16'h33CC;
                        6'd12: body_font_row = 16'h33CC;
                        6'd13: body_font_row = 16'h31CC;
                        6'd14: body_font_row = 16'h318C;
                        6'd15: body_font_row = 16'h300C;
                        6'd16: body_font_row = 16'h300C;
                        6'd17: body_font_row = 16'h300C;
                        6'd18: body_font_row = 16'h300C;
                        6'd19: body_font_row = 16'h300C;
                        6'd20: body_font_row = 16'h300C;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h4E: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1C1C;
                        6'd4: body_font_row = 16'h1C1C;
                        6'd5: body_font_row = 16'h1E1C;
                        6'd6: body_font_row = 16'h1E1C;
                        6'd7: body_font_row = 16'h1E1C;
                        6'd8: body_font_row = 16'h1B1C;
                        6'd9: body_font_row = 16'h1B1C;
                        6'd10: body_font_row = 16'h199C;
                        6'd11: body_font_row = 16'h199C;
                        6'd12: body_font_row = 16'h199C;
                        6'd13: body_font_row = 16'h18DC;
                        6'd14: body_font_row = 16'h18DC;
                        6'd15: body_font_row = 16'h187C;
                        6'd16: body_font_row = 16'h187C;
                        6'd17: body_font_row = 16'h187C;
                        6'd18: body_font_row = 16'h183C;
                        6'd19: body_font_row = 16'h183C;
                        6'd20: body_font_row = 16'h181C;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h4F: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h03E0;
                        6'd4: body_font_row = 16'h0FF0;
                        6'd5: body_font_row = 16'h0E38;
                        6'd6: body_font_row = 16'h1C18;
                        6'd7: body_font_row = 16'h181C;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h381C;
                        6'd10: body_font_row = 16'h380C;
                        6'd11: body_font_row = 16'h380C;
                        6'd12: body_font_row = 16'h380C;
                        6'd13: body_font_row = 16'h380C;
                        6'd14: body_font_row = 16'h381C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h181C;
                        6'd17: body_font_row = 16'h1C18;
                        6'd18: body_font_row = 16'h0E38;
                        6'd19: body_font_row = 16'h0FF0;
                        6'd20: body_font_row = 16'h03E0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h50: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FF0;
                        6'd4: body_font_row = 16'h1FF8;
                        6'd5: body_font_row = 16'h1C3C;
                        6'd6: body_font_row = 16'h1C1C;
                        6'd7: body_font_row = 16'h1C0C;
                        6'd8: body_font_row = 16'h1C0C;
                        6'd9: body_font_row = 16'h1C1C;
                        6'd10: body_font_row = 16'h1C3C;
                        6'd11: body_font_row = 16'h1FF8;
                        6'd12: body_font_row = 16'h1FF0;
                        6'd13: body_font_row = 16'h1C00;
                        6'd14: body_font_row = 16'h1C00;
                        6'd15: body_font_row = 16'h1C00;
                        6'd16: body_font_row = 16'h1C00;
                        6'd17: body_font_row = 16'h1C00;
                        6'd18: body_font_row = 16'h1C00;
                        6'd19: body_font_row = 16'h1C00;
                        6'd20: body_font_row = 16'h1C00;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h51: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h03E0;
                        6'd4: body_font_row = 16'h0FF0;
                        6'd5: body_font_row = 16'h0E38;
                        6'd6: body_font_row = 16'h1C18;
                        6'd7: body_font_row = 16'h181C;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h381C;
                        6'd10: body_font_row = 16'h380C;
                        6'd11: body_font_row = 16'h380C;
                        6'd12: body_font_row = 16'h380C;
                        6'd13: body_font_row = 16'h380C;
                        6'd14: body_font_row = 16'h381C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h181C;
                        6'd17: body_font_row = 16'h1C18;
                        6'd18: body_font_row = 16'h0E38;
                        6'd19: body_font_row = 16'h0FF0;
                        6'd20: body_font_row = 16'h03E0;
                        6'd21: body_font_row = 16'h0070;
                        6'd22: body_font_row = 16'h0038;
                        6'd23: body_font_row = 16'h0010;
                        default: ;
                    endcase
                end
                8'h52: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FE0;
                        6'd4: body_font_row = 16'h1FF0;
                        6'd5: body_font_row = 16'h1838;
                        6'd6: body_font_row = 16'h1818;
                        6'd7: body_font_row = 16'h1818;
                        6'd8: body_font_row = 16'h1818;
                        6'd9: body_font_row = 16'h1818;
                        6'd10: body_font_row = 16'h1838;
                        6'd11: body_font_row = 16'h1FF0;
                        6'd12: body_font_row = 16'h1FE0;
                        6'd13: body_font_row = 16'h1870;
                        6'd14: body_font_row = 16'h1830;
                        6'd15: body_font_row = 16'h1838;
                        6'd16: body_font_row = 16'h1818;
                        6'd17: body_font_row = 16'h181C;
                        6'd18: body_font_row = 16'h180C;
                        6'd19: body_font_row = 16'h180E;
                        6'd20: body_font_row = 16'h1806;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h53: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h03E0;
                        6'd4: body_font_row = 16'h0FF8;
                        6'd5: body_font_row = 16'h1C18;
                        6'd6: body_font_row = 16'h1800;
                        6'd7: body_font_row = 16'h1800;
                        6'd8: body_font_row = 16'h1800;
                        6'd9: body_font_row = 16'h1C00;
                        6'd10: body_font_row = 16'h1F00;
                        6'd11: body_font_row = 16'h0FE0;
                        6'd12: body_font_row = 16'h03F8;
                        6'd13: body_font_row = 16'h0078;
                        6'd14: body_font_row = 16'h001C;
                        6'd15: body_font_row = 16'h001C;
                        6'd16: body_font_row = 16'h001C;
                        6'd17: body_font_row = 16'h001C;
                        6'd18: body_font_row = 16'h1038;
                        6'd19: body_font_row = 16'h1FF8;
                        6'd20: body_font_row = 16'h0FE0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h54: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h3FFE;
                        6'd4: body_font_row = 16'h3FFE;
                        6'd5: body_font_row = 16'h0180;
                        6'd6: body_font_row = 16'h0180;
                        6'd7: body_font_row = 16'h0180;
                        6'd8: body_font_row = 16'h0180;
                        6'd9: body_font_row = 16'h0180;
                        6'd10: body_font_row = 16'h0180;
                        6'd11: body_font_row = 16'h0180;
                        6'd12: body_font_row = 16'h0180;
                        6'd13: body_font_row = 16'h0180;
                        6'd14: body_font_row = 16'h0180;
                        6'd15: body_font_row = 16'h0180;
                        6'd16: body_font_row = 16'h0180;
                        6'd17: body_font_row = 16'h0180;
                        6'd18: body_font_row = 16'h0180;
                        6'd19: body_font_row = 16'h0180;
                        6'd20: body_font_row = 16'h0180;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h55: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h181C;
                        6'd4: body_font_row = 16'h181C;
                        6'd5: body_font_row = 16'h181C;
                        6'd6: body_font_row = 16'h181C;
                        6'd7: body_font_row = 16'h181C;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h181C;
                        6'd10: body_font_row = 16'h181C;
                        6'd11: body_font_row = 16'h181C;
                        6'd12: body_font_row = 16'h181C;
                        6'd13: body_font_row = 16'h181C;
                        6'd14: body_font_row = 16'h181C;
                        6'd15: body_font_row = 16'h181C;
                        6'd16: body_font_row = 16'h181C;
                        6'd17: body_font_row = 16'h1818;
                        6'd18: body_font_row = 16'h1C38;
                        6'd19: body_font_row = 16'h0FF0;
                        6'd20: body_font_row = 16'h03E0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h56: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h300E;
                        6'd4: body_font_row = 16'h380C;
                        6'd5: body_font_row = 16'h380C;
                        6'd6: body_font_row = 16'h181C;
                        6'd7: body_font_row = 16'h181C;
                        6'd8: body_font_row = 16'h1C18;
                        6'd9: body_font_row = 16'h1C18;
                        6'd10: body_font_row = 16'h0C38;
                        6'd11: body_font_row = 16'h0C30;
                        6'd12: body_font_row = 16'h0E30;
                        6'd13: body_font_row = 16'h0630;
                        6'd14: body_font_row = 16'h0670;
                        6'd15: body_font_row = 16'h0660;
                        6'd16: body_font_row = 16'h0760;
                        6'd17: body_font_row = 16'h0360;
                        6'd18: body_font_row = 16'h03C0;
                        6'd19: body_font_row = 16'h03C0;
                        6'd20: body_font_row = 16'h01C0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h57: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h6006;
                        6'd4: body_font_row = 16'h7006;
                        6'd5: body_font_row = 16'h7006;
                        6'd6: body_font_row = 16'h7006;
                        6'd7: body_font_row = 16'h3006;
                        6'd8: body_font_row = 16'h31CE;
                        6'd9: body_font_row = 16'h31CE;
                        6'd10: body_font_row = 16'h33CC;
                        6'd11: body_font_row = 16'h33CC;
                        6'd12: body_font_row = 16'h334C;
                        6'd13: body_font_row = 16'h3B6C;
                        6'd14: body_font_row = 16'h1A6C;
                        6'd15: body_font_row = 16'h1E6C;
                        6'd16: body_font_row = 16'h1E3C;
                        6'd17: body_font_row = 16'h1E38;
                        6'd18: body_font_row = 16'h1C38;
                        6'd19: body_font_row = 16'h1C38;
                        6'd20: body_font_row = 16'h0C38;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h58: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h380E;
                        6'd4: body_font_row = 16'h181C;
                        6'd5: body_font_row = 16'h1C18;
                        6'd6: body_font_row = 16'h0E38;
                        6'd7: body_font_row = 16'h0630;
                        6'd8: body_font_row = 16'h0760;
                        6'd9: body_font_row = 16'h03E0;
                        6'd10: body_font_row = 16'h03C0;
                        6'd11: body_font_row = 16'h01C0;
                        6'd12: body_font_row = 16'h03C0;
                        6'd13: body_font_row = 16'h03E0;
                        6'd14: body_font_row = 16'h0760;
                        6'd15: body_font_row = 16'h0630;
                        6'd16: body_font_row = 16'h0C38;
                        6'd17: body_font_row = 16'h1C18;
                        6'd18: body_font_row = 16'h181C;
                        6'd19: body_font_row = 16'h380C;
                        6'd20: body_font_row = 16'h700E;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h59: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h300E;
                        6'd4: body_font_row = 16'h380C;
                        6'd5: body_font_row = 16'h181C;
                        6'd6: body_font_row = 16'h1C18;
                        6'd7: body_font_row = 16'h0C38;
                        6'd8: body_font_row = 16'h0670;
                        6'd9: body_font_row = 16'h0660;
                        6'd10: body_font_row = 16'h03E0;
                        6'd11: body_font_row = 16'h03C0;
                        6'd12: body_font_row = 16'h01C0;
                        6'd13: body_font_row = 16'h0180;
                        6'd14: body_font_row = 16'h0180;
                        6'd15: body_font_row = 16'h0180;
                        6'd16: body_font_row = 16'h0180;
                        6'd17: body_font_row = 16'h0180;
                        6'd18: body_font_row = 16'h0180;
                        6'd19: body_font_row = 16'h0180;
                        6'd20: body_font_row = 16'h0180;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h5A: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h1FFC;
                        6'd4: body_font_row = 16'h1FFC;
                        6'd5: body_font_row = 16'h001C;
                        6'd6: body_font_row = 16'h0018;
                        6'd7: body_font_row = 16'h0038;
                        6'd8: body_font_row = 16'h0070;
                        6'd9: body_font_row = 16'h0060;
                        6'd10: body_font_row = 16'h00E0;
                        6'd11: body_font_row = 16'h00C0;
                        6'd12: body_font_row = 16'h01C0;
                        6'd13: body_font_row = 16'h0380;
                        6'd14: body_font_row = 16'h0300;
                        6'd15: body_font_row = 16'h0700;
                        6'd16: body_font_row = 16'h0E00;
                        6'd17: body_font_row = 16'h0C00;
                        6'd18: body_font_row = 16'h1C00;
                        6'd19: body_font_row = 16'h1FFE;
                        6'd20: body_font_row = 16'h1FFE;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h6D: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0000;
                        6'd4: body_font_row = 16'h0000;
                        6'd5: body_font_row = 16'h0000;
                        6'd6: body_font_row = 16'h0000;
                        6'd7: body_font_row = 16'h0000;
                        6'd8: body_font_row = 16'h3738;
                        6'd9: body_font_row = 16'h3FFC;
                        6'd10: body_font_row = 16'h39CC;
                        6'd11: body_font_row = 16'h318C;
                        6'd12: body_font_row = 16'h318C;
                        6'd13: body_font_row = 16'h318C;
                        6'd14: body_font_row = 16'h318C;
                        6'd15: body_font_row = 16'h318C;
                        6'd16: body_font_row = 16'h318C;
                        6'd17: body_font_row = 16'h318C;
                        6'd18: body_font_row = 16'h318C;
                        6'd19: body_font_row = 16'h318C;
                        6'd20: body_font_row = 16'h318C;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h73: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0000;
                        6'd4: body_font_row = 16'h0000;
                        6'd5: body_font_row = 16'h0000;
                        6'd6: body_font_row = 16'h0000;
                        6'd7: body_font_row = 16'h0000;
                        6'd8: body_font_row = 16'h03E0;
                        6'd9: body_font_row = 16'h0FF0;
                        6'd10: body_font_row = 16'h0E10;
                        6'd11: body_font_row = 16'h0C00;
                        6'd12: body_font_row = 16'h0C00;
                        6'd13: body_font_row = 16'h0F00;
                        6'd14: body_font_row = 16'h07F0;
                        6'd15: body_font_row = 16'h0078;
                        6'd16: body_font_row = 16'h0018;
                        6'd17: body_font_row = 16'h0018;
                        6'd18: body_font_row = 16'h0838;
                        6'd19: body_font_row = 16'h1FF0;
                        6'd20: body_font_row = 16'h07E0;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                8'h78: begin
                    case (row)
                        6'd0: body_font_row = 16'h0000;
                        6'd1: body_font_row = 16'h0000;
                        6'd2: body_font_row = 16'h0000;
                        6'd3: body_font_row = 16'h0000;
                        6'd4: body_font_row = 16'h0000;
                        6'd5: body_font_row = 16'h0000;
                        6'd6: body_font_row = 16'h0000;
                        6'd7: body_font_row = 16'h0000;
                        6'd8: body_font_row = 16'h181C;
                        6'd9: body_font_row = 16'h1C18;
                        6'd10: body_font_row = 16'h0E30;
                        6'd11: body_font_row = 16'h0660;
                        6'd12: body_font_row = 16'h03E0;
                        6'd13: body_font_row = 16'h03C0;
                        6'd14: body_font_row = 16'h01C0;
                        6'd15: body_font_row = 16'h03C0;
                        6'd16: body_font_row = 16'h0760;
                        6'd17: body_font_row = 16'h0670;
                        6'd18: body_font_row = 16'h0C38;
                        6'd19: body_font_row = 16'h1C18;
                        6'd20: body_font_row = 16'h380C;
                        6'd21: body_font_row = 16'h0000;
                        6'd22: body_font_row = 16'h0000;
                        6'd23: body_font_row = 16'h0000;
                        default: ;
                    endcase
                end
                default: ;
            endcase
        end
    endfunction

    function [19:0] title_font_row;
        input [7:0] ch;
        input [5:0] row;
        begin
            title_font_row = 20'd0;
            case (ch)
                8'h20: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h00000;
                        6'd5: title_font_row = 20'h00000;
                        6'd6: title_font_row = 20'h00000;
                        6'd7: title_font_row = 20'h00000;
                        6'd8: title_font_row = 20'h00000;
                        6'd9: title_font_row = 20'h00000;
                        6'd10: title_font_row = 20'h00000;
                        6'd11: title_font_row = 20'h00000;
                        6'd12: title_font_row = 20'h00000;
                        6'd13: title_font_row = 20'h00000;
                        6'd14: title_font_row = 20'h00000;
                        6'd15: title_font_row = 20'h00000;
                        6'd16: title_font_row = 20'h00000;
                        6'd17: title_font_row = 20'h00000;
                        6'd18: title_font_row = 20'h00000;
                        6'd19: title_font_row = 20'h00000;
                        6'd20: title_font_row = 20'h00000;
                        6'd21: title_font_row = 20'h00000;
                        6'd22: title_font_row = 20'h00000;
                        6'd23: title_font_row = 20'h00000;
                        6'd24: title_font_row = 20'h00000;
                        6'd25: title_font_row = 20'h00000;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h41: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h00F00;
                        6'd5: title_font_row = 20'h00F00;
                        6'd6: title_font_row = 20'h01F80;
                        6'd7: title_font_row = 20'h01F80;
                        6'd8: title_font_row = 20'h01D80;
                        6'd9: title_font_row = 20'h019C0;
                        6'd10: title_font_row = 20'h039C0;
                        6'd11: title_font_row = 20'h039C0;
                        6'd12: title_font_row = 20'h030C0;
                        6'd13: title_font_row = 20'h070E0;
                        6'd14: title_font_row = 20'h070E0;
                        6'd15: title_font_row = 20'h070E0;
                        6'd16: title_font_row = 20'h0E070;
                        6'd17: title_font_row = 20'h0E070;
                        6'd18: title_font_row = 20'h0FFF0;
                        6'd19: title_font_row = 20'h1FFF8;
                        6'd20: title_font_row = 20'h1FFF8;
                        6'd21: title_font_row = 20'h1C038;
                        6'd22: title_font_row = 20'h1C038;
                        6'd23: title_font_row = 20'h3801C;
                        6'd24: title_font_row = 20'h3801C;
                        6'd25: title_font_row = 20'h3801C;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h43: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h007C0;
                        6'd5: title_font_row = 20'h01FF0;
                        6'd6: title_font_row = 20'h03FF8;
                        6'd7: title_font_row = 20'h07C38;
                        6'd8: title_font_row = 20'h0F008;
                        6'd9: title_font_row = 20'h0E000;
                        6'd10: title_font_row = 20'h0E000;
                        6'd11: title_font_row = 20'h1E000;
                        6'd12: title_font_row = 20'h1C000;
                        6'd13: title_font_row = 20'h1C000;
                        6'd14: title_font_row = 20'h1C000;
                        6'd15: title_font_row = 20'h1C000;
                        6'd16: title_font_row = 20'h1C000;
                        6'd17: title_font_row = 20'h1C000;
                        6'd18: title_font_row = 20'h1E000;
                        6'd19: title_font_row = 20'h0E000;
                        6'd20: title_font_row = 20'h0E000;
                        6'd21: title_font_row = 20'h0F008;
                        6'd22: title_font_row = 20'h07C38;
                        6'd23: title_font_row = 20'h03FF8;
                        6'd24: title_font_row = 20'h01FF0;
                        6'd25: title_font_row = 20'h007C0;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h45: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h0FFF8;
                        6'd5: title_font_row = 20'h0FFF8;
                        6'd6: title_font_row = 20'h0FFF8;
                        6'd7: title_font_row = 20'h0E000;
                        6'd8: title_font_row = 20'h0E000;
                        6'd9: title_font_row = 20'h0E000;
                        6'd10: title_font_row = 20'h0E000;
                        6'd11: title_font_row = 20'h0E000;
                        6'd12: title_font_row = 20'h0E000;
                        6'd13: title_font_row = 20'h0FFF0;
                        6'd14: title_font_row = 20'h0FFF0;
                        6'd15: title_font_row = 20'h0FFF0;
                        6'd16: title_font_row = 20'h0E000;
                        6'd17: title_font_row = 20'h0E000;
                        6'd18: title_font_row = 20'h0E000;
                        6'd19: title_font_row = 20'h0E000;
                        6'd20: title_font_row = 20'h0E000;
                        6'd21: title_font_row = 20'h0E000;
                        6'd22: title_font_row = 20'h0E000;
                        6'd23: title_font_row = 20'h0FFF8;
                        6'd24: title_font_row = 20'h0FFF8;
                        6'd25: title_font_row = 20'h0FFF8;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h46: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h0FFF8;
                        6'd5: title_font_row = 20'h0FFF8;
                        6'd6: title_font_row = 20'h0FFF8;
                        6'd7: title_font_row = 20'h0E000;
                        6'd8: title_font_row = 20'h0E000;
                        6'd9: title_font_row = 20'h0E000;
                        6'd10: title_font_row = 20'h0E000;
                        6'd11: title_font_row = 20'h0E000;
                        6'd12: title_font_row = 20'h0E000;
                        6'd13: title_font_row = 20'h0FFF0;
                        6'd14: title_font_row = 20'h0FFF0;
                        6'd15: title_font_row = 20'h0FFF0;
                        6'd16: title_font_row = 20'h0E000;
                        6'd17: title_font_row = 20'h0E000;
                        6'd18: title_font_row = 20'h0E000;
                        6'd19: title_font_row = 20'h0E000;
                        6'd20: title_font_row = 20'h0E000;
                        6'd21: title_font_row = 20'h0E000;
                        6'd22: title_font_row = 20'h0E000;
                        6'd23: title_font_row = 20'h0E000;
                        6'd24: title_font_row = 20'h0E000;
                        6'd25: title_font_row = 20'h0E000;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h4D: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h3C03C;
                        6'd5: title_font_row = 20'h3E07C;
                        6'd6: title_font_row = 20'h3E07C;
                        6'd7: title_font_row = 20'h3E07C;
                        6'd8: title_font_row = 20'h3B0DC;
                        6'd9: title_font_row = 20'h3B0DC;
                        6'd10: title_font_row = 20'h3B0DC;
                        6'd11: title_font_row = 20'h3999C;
                        6'd12: title_font_row = 20'h3999C;
                        6'd13: title_font_row = 20'h3999C;
                        6'd14: title_font_row = 20'h38F1C;
                        6'd15: title_font_row = 20'h38F1C;
                        6'd16: title_font_row = 20'h38F1C;
                        6'd17: title_font_row = 20'h3861C;
                        6'd18: title_font_row = 20'h3801C;
                        6'd19: title_font_row = 20'h3801C;
                        6'd20: title_font_row = 20'h3801C;
                        6'd21: title_font_row = 20'h3801C;
                        6'd22: title_font_row = 20'h3801C;
                        6'd23: title_font_row = 20'h3801C;
                        6'd24: title_font_row = 20'h3801C;
                        6'd25: title_font_row = 20'h3801C;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h4E: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h1E038;
                        6'd5: title_font_row = 20'h1E038;
                        6'd6: title_font_row = 20'h1F038;
                        6'd7: title_font_row = 20'h1F038;
                        6'd8: title_font_row = 20'h1F838;
                        6'd9: title_font_row = 20'h1D838;
                        6'd10: title_font_row = 20'h1D838;
                        6'd11: title_font_row = 20'h1DC38;
                        6'd12: title_font_row = 20'h1CC38;
                        6'd13: title_font_row = 20'h1CE38;
                        6'd14: title_font_row = 20'h1C638;
                        6'd15: title_font_row = 20'h1C738;
                        6'd16: title_font_row = 20'h1C738;
                        6'd17: title_font_row = 20'h1C338;
                        6'd18: title_font_row = 20'h1C3B8;
                        6'd19: title_font_row = 20'h1C1B8;
                        6'd20: title_font_row = 20'h1C1F8;
                        6'd21: title_font_row = 20'h1C1F8;
                        6'd22: title_font_row = 20'h1C0F8;
                        6'd23: title_font_row = 20'h1C0F8;
                        6'd24: title_font_row = 20'h1C078;
                        6'd25: title_font_row = 20'h1C078;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h4F: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h01F80;
                        6'd5: title_font_row = 20'h03FC0;
                        6'd6: title_font_row = 20'h07FE0;
                        6'd7: title_font_row = 20'h0F0F0;
                        6'd8: title_font_row = 20'h0E070;
                        6'd9: title_font_row = 20'h1E078;
                        6'd10: title_font_row = 20'h1C038;
                        6'd11: title_font_row = 20'h1C038;
                        6'd12: title_font_row = 20'h1C038;
                        6'd13: title_font_row = 20'h1C038;
                        6'd14: title_font_row = 20'h1C038;
                        6'd15: title_font_row = 20'h1C038;
                        6'd16: title_font_row = 20'h1C038;
                        6'd17: title_font_row = 20'h1C038;
                        6'd18: title_font_row = 20'h1C038;
                        6'd19: title_font_row = 20'h1C038;
                        6'd20: title_font_row = 20'h1E078;
                        6'd21: title_font_row = 20'h0E070;
                        6'd22: title_font_row = 20'h0F0F0;
                        6'd23: title_font_row = 20'h07FE0;
                        6'd24: title_font_row = 20'h03FC0;
                        6'd25: title_font_row = 20'h01F80;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h50: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h0FFC0;
                        6'd5: title_font_row = 20'h0FFF0;
                        6'd6: title_font_row = 20'h0FFF8;
                        6'd7: title_font_row = 20'h0E078;
                        6'd8: title_font_row = 20'h0E038;
                        6'd9: title_font_row = 20'h0E03C;
                        6'd10: title_font_row = 20'h0E01C;
                        6'd11: title_font_row = 20'h0E03C;
                        6'd12: title_font_row = 20'h0E038;
                        6'd13: title_font_row = 20'h0E078;
                        6'd14: title_font_row = 20'h0FFF8;
                        6'd15: title_font_row = 20'h0FFF0;
                        6'd16: title_font_row = 20'h0FFC0;
                        6'd17: title_font_row = 20'h0E000;
                        6'd18: title_font_row = 20'h0E000;
                        6'd19: title_font_row = 20'h0E000;
                        6'd20: title_font_row = 20'h0E000;
                        6'd21: title_font_row = 20'h0E000;
                        6'd22: title_font_row = 20'h0E000;
                        6'd23: title_font_row = 20'h0E000;
                        6'd24: title_font_row = 20'h0E000;
                        6'd25: title_font_row = 20'h0E000;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                8'h52: begin
                    case (row)
                        6'd0: title_font_row = 20'h00000;
                        6'd1: title_font_row = 20'h00000;
                        6'd2: title_font_row = 20'h00000;
                        6'd3: title_font_row = 20'h00000;
                        6'd4: title_font_row = 20'h1FF80;
                        6'd5: title_font_row = 20'h1FFE0;
                        6'd6: title_font_row = 20'h1FFE0;
                        6'd7: title_font_row = 20'h1C0F0;
                        6'd8: title_font_row = 20'h1C070;
                        6'd9: title_font_row = 20'h1C078;
                        6'd10: title_font_row = 20'h1C078;
                        6'd11: title_font_row = 20'h1C078;
                        6'd12: title_font_row = 20'h1C070;
                        6'd13: title_font_row = 20'h1C0F0;
                        6'd14: title_font_row = 20'h1FFE0;
                        6'd15: title_font_row = 20'h1FF80;
                        6'd16: title_font_row = 20'h1FFC0;
                        6'd17: title_font_row = 20'h1C1E0;
                        6'd18: title_font_row = 20'h1C0E0;
                        6'd19: title_font_row = 20'h1C070;
                        6'd20: title_font_row = 20'h1C070;
                        6'd21: title_font_row = 20'h1C038;
                        6'd22: title_font_row = 20'h1C038;
                        6'd23: title_font_row = 20'h1C01C;
                        6'd24: title_font_row = 20'h1C01C;
                        6'd25: title_font_row = 20'h1C00E;
                        6'd26: title_font_row = 20'h00000;
                        6'd27: title_font_row = 20'h00000;
                        6'd28: title_font_row = 20'h00000;
                        6'd29: title_font_row = 20'h00000;
                        6'd30: title_font_row = 20'h00000;
                        6'd31: title_font_row = 20'h00000;
                        default: ;
                    endcase
                end
                default: ;
            endcase
        end
    endfunction

    function [7:0] static_body_char;
        input [3:0] text_id;
        input [4:0] index;
        begin
            static_body_char = 8'h20;
            case (text_id)
                4'd0: begin
                    case (index)
                        5'd0: static_body_char = 8'h52;
                        5'd1: static_body_char = 8'h45;
                        5'd2: static_body_char = 8'h4E;
                        5'd3: static_body_char = 8'h44;
                        5'd4: static_body_char = 8'h45;
                        5'd5: static_body_char = 8'h52;
                        5'd6: static_body_char = 8'h45;
                        5'd7: static_body_char = 8'h52;
                        default: ;
                    endcase
                end
                4'd1: begin
                    case (index)
                        5'd0: static_body_char = 8'h4F;
                        5'd1: static_body_char = 8'h42;
                        5'd2: static_body_char = 8'h4A;
                        5'd3: static_body_char = 8'h45;
                        5'd4: static_body_char = 8'h43;
                        5'd5: static_body_char = 8'h54;
                        5'd6: static_body_char = 8'h53;
                        default: ;
                    endcase
                end
                4'd2: begin
                    case (index)
                        5'd0: static_body_char = 8'h46;
                        5'd1: static_body_char = 8'h50;
                        5'd2: static_body_char = 8'h53;
                        5'd3: static_body_char = 8'h20;
                        5'd4: static_body_char = 8'h43;
                        5'd5: static_body_char = 8'h45;
                        5'd6: static_body_char = 8'h49;
                        5'd7: static_body_char = 8'h4C;
                        default: ;
                    endcase
                end
                4'd3: begin
                    case (index)
                        5'd0: static_body_char = 8'h43;
                        5'd1: static_body_char = 8'h50;
                        5'd2: static_body_char = 8'h55;
                        default: ;
                    endcase
                end
                4'd4: begin
                    case (index)
                        5'd0: static_body_char = 8'h42;
                        5'd1: static_body_char = 8'h4C;
                        5'd2: static_body_char = 8'h49;
                        5'd3: static_body_char = 8'h54;
                        default: ;
                    endcase
                end
                4'd5: begin
                    case (index)
                        5'd0: static_body_char = 8'h52;
                        5'd1: static_body_char = 8'h45;
                        5'd2: static_body_char = 8'h43;
                        5'd3: static_body_char = 8'h54;
                        default: ;
                    endcase
                end
                4'd6: begin
                    case (index)
                        5'd0: static_body_char = 8'h42;
                        5'd1: static_body_char = 8'h4C;
                        5'd2: static_body_char = 8'h49;
                        5'd3: static_body_char = 8'h54;
                        5'd4: static_body_char = 8'h20;
                        5'd5: static_body_char = 8'h54;
                        5'd6: static_body_char = 8'h49;
                        5'd7: static_body_char = 8'h4D;
                        5'd8: static_body_char = 8'h45;
                        default: ;
                    endcase
                end
                4'd7: begin
                    case (index)
                        5'd0: static_body_char = 8'h52;
                        5'd1: static_body_char = 8'h45;
                        5'd2: static_body_char = 8'h43;
                        5'd3: static_body_char = 8'h54;
                        5'd4: static_body_char = 8'h20;
                        5'd5: static_body_char = 8'h54;
                        5'd6: static_body_char = 8'h49;
                        5'd7: static_body_char = 8'h4D;
                        5'd8: static_body_char = 8'h45;
                        default: ;
                    endcase
                end
                4'd8: begin
                    case (index)
                        5'd0: static_body_char = 8'h41;
                        5'd1: static_body_char = 8'h53;
                        5'd2: static_body_char = 8'h59;
                        5'd3: static_body_char = 8'h4E;
                        5'd4: static_body_char = 8'h43;
                        default: ;
                    endcase
                end
                4'd9: begin
                    case (index)
                        5'd0: static_body_char = 8'h43;
                        5'd1: static_body_char = 8'h4F;
                        5'd2: static_body_char = 8'h4C;
                        5'd3: static_body_char = 8'h4C;
                        5'd4: static_body_char = 8'h49;
                        5'd5: static_body_char = 8'h44;
                        5'd6: static_body_char = 8'h45;
                        default: ;
                    endcase
                end
                4'd10: begin
                    case (index)
                        5'd0: static_body_char = 8'h45;
                        5'd1: static_body_char = 8'h52;
                        5'd2: static_body_char = 8'h52;
                        5'd3: static_body_char = 8'h4F;
                        5'd4: static_body_char = 8'h52;
                        default: ;
                    endcase
                end
                4'd11: begin
                    case (index)
                        5'd0: static_body_char = 8'h4C;
                        5'd1: static_body_char = 8'h49;
                        5'd2: static_body_char = 8'h56;
                        5'd3: static_body_char = 8'h45;
                        5'd4: static_body_char = 8'h53;
                        default: ;
                    endcase
                end
                4'd12: begin
                    case (index)
                        5'd0: static_body_char = 8'h53;
                        5'd1: static_body_char = 8'h43;
                        5'd2: static_body_char = 8'h4F;
                        5'd3: static_body_char = 8'h52;
                        5'd4: static_body_char = 8'h45;
                        default: ;
                    endcase
                end
                4'd13: begin
                    case (index)
                        5'd0: static_body_char = 8'h47;
                        5'd1: static_body_char = 8'h41;
                        5'd2: static_body_char = 8'h4D;
                        5'd3: static_body_char = 8'h45;
                        5'd4: static_body_char = 8'h20;
                        5'd5: static_body_char = 8'h4F;
                        5'd6: static_body_char = 8'h56;
                        5'd7: static_body_char = 8'h45;
                        5'd8: static_body_char = 8'h52;
                        default: ;
                    endcase
                end
                default: ;
            endcase
        end
    endfunction

    function [3:0] time_bcd_digit;
        input [31:0] word;
        input [3:0] position;
        begin
            case (position)
                4'd0: time_bcd_digit = word[31:28];
                4'd1: time_bcd_digit = word[27:24];
                4'd2: time_bcd_digit = word[23:20];
                4'd3: time_bcd_digit = word[19:16];
                4'd4: time_bcd_digit = word[15:12];
                4'd5: time_bcd_digit = word[11:8];
                4'd6: time_bcd_digit = word[7:4];
                default: time_bcd_digit = word[3:0];
            endcase
        end
    endfunction

    function [7:0] hex_char;
        input [3:0] value;
        begin
            if (value < 4'd10) hex_char = 8'h30 + value;
            else hex_char = 8'h41 + value - 4'd10;
        end
    endfunction

    // Packed BCD microseconds are already converted by software to the
    // displayed millisecond digits. This formatter only selects characters.
    function [7:0] time_char;
        input [31:0] word;
        input [2:0] digits;
        input [4:0] index;
        input valid;
        reg [3:0] position;
        begin
            time_char = 8'h20;
            if (!valid) begin
                if (index < 5'd2) time_char = 8'h2D;
            end else if (word == 32'hFFFFFFFF) begin
                case (index)
                    5'd0: time_char = 8'h53; // S
                    5'd1: time_char = 8'h41; // A
                    5'd2: time_char = 8'h54; // T
                    default: ;
                endcase
            end else if (index < {2'd0,digits}) begin
                position = 4'd8 - {1'b0,digits} + index;
                time_char = 8'h30 + time_bcd_digit(word, position);
            end else if (index == {2'd0,digits}) begin
                time_char = 8'h2E;
            end else if (index == ({2'd0,digits} + 5'd1)) begin
                time_char = 8'h30 + word[11:8];
            end else if (index == ({2'd0,digits} + 5'd2)) begin
                time_char = 8'h30 + word[7:4];
            end else if (index == ({2'd0,digits} + 5'd3)) begin
                time_char = 8'h30 + word[3:0];
            end else if (index == ({2'd0,digits} + 5'd5)) begin
                time_char = 8'h6D; // m
            end else if (index == ({2'd0,digits} + 5'd6)) begin
                time_char = 8'h73; // s
            end
        end
    endfunction


    // P1 geometry/field selection is combinational, then captured.
    reg [15:0] pixel_next;
    reg [7:0] body_char;
    reg [15:0] body_color;
    reg [15:0] title_color, body_bits;
    reg [11:0] title_x, title_y;
    reg [3:0] title_index;
    reg [19:0] title_bits;
    reg [11:0] body_x, body_y;
    reg [12:0] body_delta_x;
    reg [3:0] body_len;
    reg [4:0] body_region;
    reg body_active;
    reg [4:0] body_index, body_row;
    reg [3:0] body_col;
    reg title_active;
    reg [7:0] title_char;
    reg [5:0] title_row;
    reg [4:0] title_col;
    reg [31:0] selected_row;
    reg selected_row_valid;
    reg [1:0] selected_row_kind;
    reg [2:0] time_digits;
    reg [31:0] selected_time;
    reg selected_time_valid;
    reg [1:0] status_code;
    reg selected_error_valid;
    reg [15:0] p1_pixel_base;
    reg p1_crosshair;

    reg [15:0] s1_pixel_base;
    reg s1_body_active;
    reg [4:0] s1_body_region;
    reg [11:0] s1_body_x, s1_body_y;
    reg [3:0] s1_body_len;
    reg [4:0] s1_body_index, s1_body_row;
    reg [3:0] s1_body_col;
    reg [15:0] s1_body_color;
    reg s1_title_active;
    reg [7:0] s1_title_char;
    reg [5:0] s1_title_row;
    reg [4:0] s1_title_col;
    reg [31:0] s1_selected_row;
    reg s1_selected_row_valid;
    reg [1:0] s1_selected_row_kind;
    reg [2:0] s1_time_digits;
    reg [31:0] s1_selected_time;
    reg s1_selected_time_valid;
    reg [1:0] s1_status_code;
    reg s1_selected_error_valid;
    reg [9:0] s1_panel_flags;
    reg [31:0] s1_panel_error;
    reg s1_snapshot_valid;
    reg s1_de, s1_hs, s1_vs, s1_crosshair;

    always @(*) begin

        pixel_next = pixel_in;
        body_char = 8'h20;
        title_char = 8'h20;
        body_color = PANEL_TEXT;
        title_color = PANEL_TITLE;
        body_x = 12'd0;
        body_y = 12'd0;
        body_len = 4'd0;
        body_region = 4'd0;
        body_active = 1'b0;
        body_index = 5'd0;
        body_row = 5'd0;
        body_col = 4'd0;
        title_x = 12'd0;
        title_y = 12'd0;
        title_active = 1'b0;
        title_index = 4'd0;
        title_row = 6'd0;
        title_col = 5'd0;
        body_bits = 16'd0;
        title_bits = 20'd0;
        selected_row = 32'd0;
        selected_row_valid = 1'b0;
        selected_row_kind = 2'd0;
        time_digits = 3'd1;
        selected_time = 32'd0;
        selected_time_valid = 1'b0;
        status_code = 2'd0;
        selected_error_valid = 1'b0;

        if (!quad_enable && de_in) begin
          if (panel_flags[10]) begin
            /* Game mode: the scanout pixel is the background. Only glyph bits
             * may replace it. Same font and pipeline as the existing HUD. */
            if (snapshot_valid_pix && panel_flags[7]) begin
                selected_row = cpu_row_word;
                selected_row_valid = 1'b1;
                if (game_band[0]) begin
                    body_region = 5'd19; body_x = 12'd1696; body_y = 12'd32; body_len = 4'd3;
                    if ((scan_x >= 12'd1840) && (scan_x < 12'd1888)) begin
                        body_region = 5'd21;
                        if (cpu_row_word[19:16] != 0) begin body_x = 12'd1840; body_len = 4'd3; end
                        else if (cpu_row_word[15:12] != 0) begin body_x = 12'd1856; body_len = 4'd2; end
                        else begin body_x = 12'd1872; body_len = 4'd1; end
                    end
                end else if (game_band[1]) begin
                    body_region = 5'd20; body_x = 12'd1696; body_y = 12'd64; body_len = 4'd7;
                    if ((scan_x >= 12'd1824) && (scan_x < 12'd1888)) begin
                        body_region = 5'd22;
                        if (blit_row_word[11:8] != 0) begin body_x = 12'd1824; body_len = 4'd4; end
                        else if (blit_row_word[7:4] != 0) begin body_x = 12'd1840; body_len = 4'd3; end
                        else if (cpu_row_word[7:4] != 0) begin body_x = 12'd1856; body_len = 4'd2; end
                        else begin body_x = 12'd1872; body_len = 4'd1; end
                    end
                end else if (panel_flags[11] && game_band[2]) begin
                    body_region = 5'd23; body_x = 12'd1696; body_y = 12'd96; body_len = 4'd5;
                    if ((scan_x >= 12'd1872) && (scan_x < 12'd1888)) begin
                        body_region = 5'd24; body_x = 12'd1872; body_len = 4'd1; selected_row = blit_row_word;
                    end
                end else if (panel_flags[11] && game_band[3]) begin
                    body_region = 5'd25; body_x = 12'd1696; body_y = 12'd128; body_len = 4'd5;
                    if ((scan_x >= 12'd1792) && (scan_x < 12'd1888)) begin
                        body_region = 5'd26; body_x = 12'd1792; body_len = 4'd6; selected_row = rect_row_word;
                    end
                end else if (panel_flags[12] && game_band[4]) begin
                    body_region = 5'd27; body_x = 12'd888; body_y = 12'd520; body_len = 4'd9;
                end
            end
          end
        end

        body_active = 1'b0;
        // Coordinates are only consumed when s1_body_active is set. Keep
        // their arithmetic independent of the text-length/range predicate:
        // clearing unused coordinates otherwise puts that entire predicate
        // on every coordinate register and adds unnecessary fanout.
        if (quad_enable) begin
            body_delta_x = {1'b0, local_x} - {1'b0, body_x};
            body_row = local_y - body_y;
        end else begin
            body_delta_x = {1'b0, scan_x} - {1'b0, body_x};
            body_row = scan_y - body_y;
        end
        body_index = body_delta_x[8:4];
        body_col = body_delta_x[3:0];
        body_color = (!quad_enable && panel_flags[10]) ? 16'hF800 :
                     ((body_region <= 4'd3) ? PANEL_TITLE : PANEL_TEXT);
        // All selected origins/lengths end below 4096. The 13-bit subtract
        // preserves the borrow, while a character-index comparison avoids
        // a serial body_x + 16*body_len carry chain in the active predicate.
        if (q3_active && (body_region != 0) &&
            !body_delta_x[12] &&
            (body_delta_x[11:4] < {4'd0, body_len})) begin
            body_active = 1'b1;
        end else if (!quad_enable && de_in && (body_region != 0) &&
                     !body_delta_x[12] &&
                     (body_delta_x[11:4] < {4'd0, body_len})) begin
            body_active = 1'b1;
        end
        p1_pixel_base = pixel_next;
        p1_crosshair = crosshair;
    end

    // V1 game rows: character from x, registered y band and snapshot words, in
    // parallel with the body decode (used only where body_active selects 23..27).
    reg [7:0] game_char, s1_game_char;
    reg [3:0] s1_objects_hundreds, s1_objects_thousands; // HUD4: BLIT[7:4] hundreds, BLIT[11:8] thousands (both already in the CDC)
    wire [11:0] game_x_label = scan_x - 12'd1696;
    wire [11:0] game_x_score = scan_x - 12'd1792;
    wire [11:0] game_x_over  = scan_x - 12'd888;
    always @(*) begin
        game_char = 8'h20;
        if (game_band[2]) game_char = (scan_x >= 12'd1872) ? 8'h30 + blit_row_word[3:0]
                                                          : static_body_char(4'd11, game_x_label[8:4]);
        else if (game_band[3]) game_char = (scan_x >= 12'd1792) ? 8'h30 + rect_row_word[4*(5-game_x_score[6:4]) +: 4]
                                                               : static_body_char(4'd12, game_x_label[8:4]);
        else if (game_band[4]) game_char = static_body_char(4'd13, game_x_over[8:4]);
    end

    // P2 carries the transaction and snapshot fields selected by P1.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s1_objects_hundreds <= 4'd0; s1_objects_thousands <= 4'd0; s1_game_char <= 8'h20; s1_pixel_base <= 16'd0; s1_body_active <= 1'b0; s1_body_region <= 5'd0;
            s1_body_x <= 12'd0; s1_body_y <= 12'd0; s1_body_len <= 4'd0;
            s1_body_index <= 5'd0; s1_body_row <= 5'd0; s1_body_col <= 4'd0;
            s1_body_color <= PANEL_TEXT; s1_title_active <= 1'b0;
            s1_title_char <= 8'h20; s1_title_row <= 6'd0; s1_title_col <= 5'd0;
            s1_selected_row <= 32'd0; s1_selected_row_valid <= 1'b0; s1_selected_row_kind <= 2'd0;
            s1_time_digits <= 3'd1; s1_selected_time <= 32'd0; s1_selected_time_valid <= 1'b0;
            s1_status_code <= 2'd0; s1_selected_error_valid <= 1'b0;
            s1_panel_flags <= 10'd0; s1_panel_error <= 32'd0; s1_snapshot_valid <= 1'b0;
            s1_de <= 1'b0; s1_hs <= 1'b0; s1_vs <= 1'b0; s1_crosshair <= 1'b0;
        end else begin
            s1_objects_hundreds <= blit_row_word[7:4]; s1_objects_thousands <= blit_row_word[11:8]; s1_game_char <= game_char; s1_pixel_base <= p1_pixel_base; s1_body_active <= body_active; s1_body_region <= body_region;
            s1_body_x <= body_x; s1_body_y <= body_y; s1_body_len <= body_len;
            s1_body_index <= body_index; s1_body_row <= body_row; s1_body_col <= body_col;
            s1_body_color <= body_color; s1_title_active <= title_active;
            s1_title_char <= title_char; s1_title_row <= title_row; s1_title_col <= title_col;
            s1_selected_row <= selected_row; s1_selected_row_valid <= selected_row_valid; s1_selected_row_kind <= selected_row_kind;
            s1_time_digits <= time_digits; s1_selected_time <= selected_time; s1_selected_time_valid <= selected_time_valid;
            s1_status_code <= status_code; s1_selected_error_valid <= selected_error_valid;
            s1_panel_flags <= panel_flags[9:0]; s1_panel_error <= panel_error; s1_snapshot_valid <= snapshot_valid_pix;
            s1_de <= de_in; s1_hs <= hs_in; s1_vs <= vs_in; s1_crosshair <= p1_crosshair;
        end
    end

    // P3/P4/P5: character select, font row, bit select and final mix.
    reg [15:0] pixel_next_s2;
    reg [7:0] body_char_s2;
    reg [15:0] body_bits_s2;
    reg [19:0] title_bits_s2;
    always @(*) begin
        pixel_next_s2 = s1_pixel_base;
        body_char_s2 = 8'h20;
        body_bits_s2 = 16'd0;
        title_bits_s2 = 20'd0;
        if (s1_body_active) begin
                case (s1_body_region)
                    4'd1: body_char_s2 = static_body_char(4'd0, s1_body_index);
                    4'd2: body_char_s2 = static_body_char(4'd1, s1_body_index);
                    4'd3: body_char_s2 = static_body_char(4'd2, s1_body_index);
                    4'd4: body_char_s2 = static_body_char(4'd3, s1_body_index);
                    4'd5: body_char_s2 = static_body_char(4'd4, s1_body_index);
                    4'd6: body_char_s2 = static_body_char(4'd5, s1_body_index);
                    4'd7: begin
                        if (!s1_selected_row_valid) body_char_s2 = 8'h2D;
                        else if (s1_selected_row[7:4] == 0) body_char_s2 = 8'h30 + s1_selected_row[3:0];
                        else if (s1_body_index == 0) body_char_s2 = 8'h30 + s1_selected_row[7:4];
                        else body_char_s2 = 8'h30 + s1_selected_row[3:0];
                    end
                    4'd8: begin
                        if (!s1_selected_row_valid || !s1_selected_row[20]) body_char_s2 = 8'h2D;
                        else if (s1_selected_row[19:16] != 0) begin
                            case (s1_body_index)
                                5'd0: body_char_s2 = 8'h30 + s1_selected_row[19:16];
                                5'd1: body_char_s2 = 8'h30 + s1_selected_row[15:12];
                                default: body_char_s2 = 8'h30 + s1_selected_row[11:8];
                            endcase
                        end else if (s1_selected_row[15:12] != 0) begin
                            if (s1_body_index == 0) body_char_s2 = 8'h30 + s1_selected_row[15:12];
                            else body_char_s2 = 8'h30 + s1_selected_row[11:8];
                        end else body_char_s2 = 8'h30 + s1_selected_row[11:8];
                    end
                    4'd9: body_char_s2 = static_body_char(4'd6, s1_body_index);
                    4'd10: body_char_s2 = static_body_char(4'd7, s1_body_index);
                    4'd11: body_char_s2 = static_body_char(4'd8, s1_body_index);
                    4'd12: body_char_s2 = time_char(s1_selected_time, s1_time_digits, s1_body_index, s1_selected_time_valid);
                    4'd13: begin
                        if (!s1_snapshot_valid) body_char_s2 = 8'h2D;
                        else if (s1_status_code == 2'd2) begin
                            case (s1_body_index) 5'd0:body_char_s2=8'h50; 5'd1:body_char_s2=8'h45; 5'd2:body_char_s2=8'h4E; default:body_char_s2=8'h44; endcase
                        end else if (s1_status_code == 2'd1) begin
                            case (s1_body_index) 5'd0:body_char_s2=8'h4F; 5'd1:body_char_s2=8'h4E; default:body_char_s2=8'h20; endcase
                        end else begin
                            case (s1_body_index) 5'd0:body_char_s2=8'h4F; 5'd1:body_char_s2=8'h46; 5'd2:body_char_s2=8'h46; default:body_char_s2=8'h20; endcase
                        end
                    end
                    4'd14: body_char_s2 = static_body_char(4'd9, s1_body_index);
                    4'd15: begin
                        if (!s1_snapshot_valid) body_char_s2 = 8'h2D;
                        else if (s1_status_code == 2'd2) begin
                            case (s1_body_index) 5'd0:body_char_s2=8'h4D; 5'd1:body_char_s2=8'h49; 5'd2:body_char_s2=8'h58; default:body_char_s2=8'h20; endcase
                        end else if (s1_status_code == 2'd1) begin
                            case (s1_body_index) 5'd0:body_char_s2=8'h4F; 5'd1:body_char_s2=8'h4E; default:body_char_s2=8'h20; endcase
                        end else begin
                            case (s1_body_index) 5'd0:body_char_s2=8'h4F; 5'd1:body_char_s2=8'h46; 5'd2:body_char_s2=8'h46; default:body_char_s2=8'h20; endcase
                        end
                    end
                    5'd16: body_char_s2 = static_body_char(4'd10, s1_body_index);
                    5'd18: begin
                        if (s1_panel_flags[9:8] == 2'd0) begin
                            case (s1_body_index)
                                5'd0: body_char_s2 = 8'h42; 5'd1: body_char_s2 = 8'h4C;
                                5'd2: body_char_s2 = 8'h49; default: body_char_s2 = 8'h54;
                            endcase
                        end else if (s1_panel_flags[9:8] == 2'd1) begin
                            case (s1_body_index)
                                5'd0: body_char_s2 = 8'h52; 5'd1: body_char_s2 = 8'h45;
                                5'd2: body_char_s2 = 8'h43; default: body_char_s2 = 8'h54;
                            endcase
                        end else begin
                            case (s1_body_index)
                                5'd0: body_char_s2 = 8'h43; 5'd1: body_char_s2 = 8'h50;
                                default: body_char_s2 = 8'h55;
                            endcase
                        end
                    end
                    5'd19: body_char_s2 = static_body_char(4'd2, s1_body_index);
                    5'd20: body_char_s2 = static_body_char(4'd1, s1_body_index);
                    5'd21: begin
                        if (!s1_selected_row_valid || !s1_selected_row[20]) body_char_s2 = 8'h2D;
                        else if (s1_selected_row[19:16] != 0) begin
                            case (s1_body_index)
                                5'd0: body_char_s2 = 8'h30 + s1_selected_row[19:16];
                                5'd1: body_char_s2 = 8'h30 + s1_selected_row[15:12];
                                default: body_char_s2 = 8'h30 + s1_selected_row[11:8];
                            endcase
                        end else if (s1_selected_row[15:12] != 0) begin
                            if (s1_body_index == 0) body_char_s2 = 8'h30 + s1_selected_row[15:12];
                            else body_char_s2 = 8'h30 + s1_selected_row[11:8];
                        end else body_char_s2 = 8'h30 + s1_selected_row[11:8];
                    end
                    5'd23, 5'd24, 5'd25, 5'd26, 5'd27: body_char_s2 = s1_game_char;
                    5'd22: begin
                        if (!s1_selected_row_valid || !s1_selected_row[20]) body_char_s2 = 8'h2D;
                        else if (s1_objects_thousands != 0) begin
                            case (s1_body_index)
                                5'd0: body_char_s2 = 8'h30 + s1_objects_thousands;
                                5'd1: body_char_s2 = 8'h30 + s1_objects_hundreds;
                                5'd2: body_char_s2 = 8'h30 + s1_selected_row[7:4];
                                default: body_char_s2 = 8'h30 + s1_selected_row[3:0];
                            endcase
                        end else if (s1_objects_hundreds != 0) begin
                            case (s1_body_index)
                                5'd0: body_char_s2 = 8'h30 + s1_objects_hundreds;
                                5'd1: body_char_s2 = 8'h30 + s1_selected_row[7:4];
                                default: body_char_s2 = 8'h30 + s1_selected_row[3:0];
                            endcase
                        end else if (s1_selected_row[7:4] != 0) begin
                            if (s1_body_index == 0) body_char_s2 = 8'h30 + s1_selected_row[7:4];
                            else body_char_s2 = 8'h30 + s1_selected_row[3:0];
                        end else body_char_s2 = 8'h30 + s1_selected_row[3:0];
                    end
                    default: begin
                        if (!s1_snapshot_valid) body_char_s2 = 8'h2D;
                        else if (!s1_selected_error_valid) begin
                            case (s1_body_index) 5'd0:body_char_s2=8'h4F; 5'd1:body_char_s2=8'h4B; default:body_char_s2=8'h20; endcase
                        end else if (s1_body_index == 0) body_char_s2 = 8'h30;
                        else if (s1_body_index == 1) body_char_s2 = 8'h78;
                        else body_char_s2 = hex_char(s1_panel_error[4*(9-s1_body_index) +: 4]);
                    end
                endcase
            body_bits_s2 = body_font_row(body_char_s2, s1_body_row);
            if (body_bits_s2[15-s1_body_col]) pixel_next_s2 = s1_body_color;
        end
        if (s1_title_active) begin
            title_bits_s2 = title_font_row(s1_title_char, s1_title_row);
            if (title_bits_s2[19-s1_title_col]) pixel_next_s2 = PANEL_TITLE;
        end
        if (s1_crosshair) pixel_next_s2 = PANEL_CROSSHAIR;
    end

    // Final output boundary. Pixel and HS/VS/DE leave together.
    reg [15:0] pixel_out_r;
    reg de_out_r, hs_out_r, vs_out_r;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pixel_out_r <= 16'd0; de_out_r <= 1'b0; hs_out_r <= 1'b0; vs_out_r <= 1'b0;
        end else begin
            pixel_out_r <= pixel_next_s2;
            de_out_r <= s1_de; hs_out_r <= s1_hs; vs_out_r <= s1_vs;
        end
    end
    assign pixel_out = pixel_out_r;
    assign de_out = de_out_r;
    assign hs_out = hs_out_r;
    assign vs_out = vs_out_r;
endmodule



// External transaction boundary keeps legacy inputs and q3 internal P1-P5 stages atomic.
module q3_perf_overlay_pipe (
    input wire clk, input wire rst_n,
    input wire [15:0] pixel_in, input wire de_in, input wire hs_in, input wire vs_in,
    input wire quad_enable, input wire [11:0] scan_x, input wire [11:0] scan_y,
    input wire [223:0] snapshot_pix, input wire snapshot_valid_pix,
    output wire [15:0] pixel_out, output wire de_out, output wire hs_out, output wire vs_out
);
    reg [15:0] p1_pixel; reg p1_de,p1_hs,p1_vs,p1_quad,p1_valid; reg [11:0] p1_x,p1_y; reg [223:0] p1_snapshot;
    reg [4:0] p1_game_band;
    wire [4:0] game_band_next={(scan_y>=12'd520)&&(scan_y<12'd544),(scan_y>=12'd128)&&(scan_y<12'd152),
                               (scan_y>=12'd96)&&(scan_y<12'd120),(scan_y>=12'd64)&&(scan_y<12'd88),(scan_y>=12'd32)&&(scan_y<12'd56)};
    wire [15:0] p2_pixel; wire p2_de,p2_hs,p2_vs;
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin p1_pixel<=16'd0; p1_de<=1'b0; p1_hs<=1'b0; p1_vs<=1'b0; p1_quad<=1'b0; p1_valid<=1'b0; p1_x<=12'd0; p1_y<=12'd0; p1_snapshot<=224'd0; p1_game_band<=5'd0; end
        else begin p1_pixel<=pixel_in; p1_de<=de_in; p1_hs<=hs_in; p1_vs<=vs_in; p1_quad<=quad_enable; p1_valid<=snapshot_valid_pix; p1_x<=scan_x; p1_y<=scan_y; p1_snapshot<=snapshot_pix; p1_game_band<=game_band_next; end
    end
    q3_perf_overlay u_q3_perf_overlay_core (.clk(clk), .rst_n(rst_n), .pixel_in(p1_pixel), .de_in(p1_de), .hs_in(p1_hs), .vs_in(p1_vs), .quad_enable(p1_quad), .scan_x(p1_x), .scan_y(p1_y), .snapshot_pix(p1_snapshot), .snapshot_valid_pix(p1_valid), .game_band(p1_game_band), .pixel_out(p2_pixel), .de_out(p2_de), .hs_out(p2_hs), .vs_out(p2_vs));
    reg [15:0] p2_pixel_r; reg p2_de_r,p2_hs_r,p2_vs_r;
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin p2_pixel_r<=16'd0; p2_de_r<=1'b0; p2_hs_r<=1'b0; p2_vs_r<=1'b0; end
        else begin p2_pixel_r<=p2_pixel; p2_de_r<=p2_de; p2_hs_r<=p2_hs; p2_vs_r<=p2_vs; end
    end
    assign pixel_out=p2_pixel_r; assign de_out=p2_de_r; assign hs_out= p2_hs_r; assign vs_out=p2_vs_r;
endmodule
