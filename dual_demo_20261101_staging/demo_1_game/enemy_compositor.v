// Shared single/multi enemy engine: raw RGB565LE assets -> MSB-first scanout.
// 32-beat read/work blocks; up to 128 queued write beats, row/4 KiB bounded.
// V1: descriptor tables are y-sorted, so each row scans a sliding window per
// table; bullet sprites are cached on chip at claim; effects use a 4-bit
// straight alpha plane blended into the working block (enemies, bullets,
// effects, then player).
// BGT-A (bg_turret_overnight_20261028): background mode 2 = I4 palette image (BG_I4 rows, stride 960, even x high nibble;
// BG_PALETTE 16 x RGB565LE latched per frame). One 960 B row is fetched per row into a 64-bit line buffer (burst split
// at 4 KiB) while the row tables are scanned; direct blocks emit 16 px/cycle from a per-row Q8 table (palette x phase,
// built on the shared encoders, so Q8 is still applied once at the absolute pixel phase); object blocks keep the lazy
// lanes and substitute palette[I4] from the line buffer. Sixteen frame-latched decor slots (32x32 RGB565LE + straight A4,
// DECOR_BASE0/1) are drawn before every game object with the existing FX fetch / BLEND arithmetic.
`include "enemy_config.vh"
module enemy_compositor #(parameter CAPACITY=`ENEMY_CAPACITY, parameter OPTIMIZE_SPRITES=1, parameter BULLET_CAPACITY=`BULLET_CAPACITY,
 parameter PLAYER_BULLET_CAPACITY=`PLAYER_BULLET_CAPACITY, parameter EFFECT_CAPACITY=`EFFECT_CAPACITY, parameter MISSILE_CAPACITY=`MISSILE_CAPACITY,
 // 0: perf block keeps compose total and present only (V1 area budget); 1: 19 buckets.
 parameter PERF_DETAIL=0,
 // R1: gradient object blocks skip the per-beat background fill (lazy lanes).
 parameter LAZY_BG=1) (
 input wire clk,rst_n,
 input wire [11:0] reg_addr,input wire reg_write,input wire [31:0] reg_wdata,
 output reg [31:0] reg_rdata,
 input wire owner_grant,input wire [31:0] back_base,generation,
 input wire present_ack,display_ok,display_error,
 output reg owner_active,render_done,
 output wire probe_composing,probe_present, // frame probe: passive state observation
 output reg [31:0] araddr,output reg [7:0] arlen,
 output reg arvalid,input wire arready,
 input wire [127:0] rdata,input wire [1:0] rresp,input wire [3:0] rid,
 input wire rlast,rvalid,output wire rready,
 input wire fifo_full,fifo_empty,ddr_idle,ddr_accept,ddr_done,ddr_error,
 output reg fifo_reset, output wire fifo_valid,output wire [143:0] fifo_data,
 output reg wr_start,output wire [32:0] wr_addr,output wire [23:0] wr_beats,
 // Read-only timing taps; never used by compositor control.
 input wire perf_ar_selected,perf_awvalid,perf_awready,
 input wire perf_wvalid,perf_wready,perf_wlast,perf_bvalid,perf_bready
);
 localparam IDLE=0,RESET=1,BG_AR=2,BG_R=3,PIXEL=4,COLOR_AR=5,COLOR_R=6,
 MASK_AR=7,MASK_R=8,PUSH=9,START=10,ACCEPT=11,RESPONSE=12,
 NEXT=13,PRESENT=14,SHOWN=15,DRAIN=16,CLEAN=17,
 ROW_INIT=18,SCAN_A=19,SCAN_B=20,SCAN_TABLE=21,SCAN_W=22,
 BLOCK_SETUP=23,BLOCK_WAIT=24,BLOCK_CHECK=25,
 OBJ_WAIT=26,OBJ_CHECK=27,OBJ_FETCH=28,PASTE0=29,PASTE1=30,OBJ_NEXT=31,
 BLOCK_DONE=32,CACHE_AR=33,CACHE_R=34,CACHE_FILL=35,CACHE_READ=36,
 BLEND_LOAD=37,BLEND=38,BLEND_WRITE=39,BG_GEN=40,PAL_AR=41,PAL_R=42,DECOR_CHECK=43;
 reg [5:0] state;
 reg [127:0] pixels[0:31]; // bounded 512-byte block, never a frame buffer
 reg [127:0] color;
 reg [5:0] index,received,block_beats;
 reg [3:0] reset_count;
 reg [10:0] row;
 reg [10:0] configured_y,latched_y;
 reg [16:0] configured_key,latched_key; // enable[16], logical RGB565[15:0]
 reg key_frame_valid;
 reg configured_background,latched_background,background_frame_valid;
 reg configured_i4,latched_i4; // BG mode 2 (I4); latched only in multi mode, otherwise the gradient fallback
 reg configured_menu,latched_menu; // CMD: I4 source 0 = game (BG_I4/BG_PALETTE), 1 = start menu (MENU_I4/MENU_PALETTE)
 // ---- BGT-A: I4 background state ----
 reg [15:0] pal[0:15]; reg pal_done;              // palette of the claimed frame
 (* syn_ramstyle="block_ram" *) reg [63:0] lb[0:511]; // row line buffer: word w = bytes 8w..8w+7 = beat columns 2w,2w+1
 // (single write port: four 512x16 RAM10; A1 uses words 0..119, A2 two banks at 0 and 128)
 reg [63:0] lb_q; reg [7:0] lb_raddr;
 wire [8:0] lb_waddr; wire [63:0] lb_wdata; // driven below (skid has priority; lf_rready excludes a same-cycle beat)
 reg lf_ar,lf_active,lf_second,lf_skid_full; reg [6:0] lf_count,lf_left,lf_second_beats; reg [5:0] lf_skid_word;
 reg [10:0] lf_row; reg [1:0] lb_valid; reg [10:0] lb_tag0,lb_tag1; // A2: row being fetched; per-bank row tags
 reg [63:0] lf_skid; reg [31:0] lf_second_addr;
 reg [7:0] q8tab[0:63]; reg qt_active; reg [2:0] qt_step; // {colour,phase} -> Q8 for this row's y phase
 // ---- BGT-A: decor slots: [31] enable, [30] skin, [29:24] 0, [23:12] signed y, [11:0] signed x (top-left) ----
 reg [31:0] configured_decor[0:15],latched_decor[0:15]; reg configured_decor_en,latched_decor_en,decor_frame_valid;
 reg [1:0] decor_error; reg [15:0] decor_rows,decor_pending; reg [4:0] decor_rel[0:15]; reg obj_decor,obj_skin;
 reg [1:0] background_error;
 // Fixed RGB565 anchors: y=0 08aa, y=512 208c, y=1079 3049.
 // Advance once per completed row; no divider, ROM or extra work buffer.
 reg [4:0] bg_red,bg_blue;
 reg [5:0] bg_green;
 reg [9:0] bg_er,bg_eg,bg_eb;
 wire bg_lower=row>=512;
 wire [9:0] bg_period=bg_lower?10'd567:10'd512;
 wire [9:0] bg_ur=bg_er+(bg_lower?10'd2:10'd3);
 wire [9:0] bg_ug=bg_eg+(bg_lower?10'd2:10'd1);
 wire [9:0] bg_ub=bg_eb+(bg_lower?10'd3:10'd2);
 reg [31:0] configured_player,latched_player;
 reg [1:0] player_error;
 reg player_frame_valid,obj_player;
 reg [7:0] column; // beat column 0..239, variable 16/32 beat blocks
 reg [31:0] dst,src,latched_back,start_generation;
 reg [22:0] committed; // <= 1920*1080*2 = 4,147,200
 reg [2:0] error_code;  // 0 or 2..5
 reg [31:0] group_addr;
 reg [7:0] group_beats; // completed blocks queued in the existing write FIFO
 reg cancel_pending,shown;
 reg write_inflight;reg [11:0] inflight_bytes; // one accepted group may drain while the next block composes
 wire [8:0] end_column;wire perf_claim;
 localparam IW=$clog2(CAPACITY)>0?$clog2(CAPACITY):1;
 localparam CW=$clog2(CAPACITY+1)>0?$clog2(CAPACITY+1):1;
 localparam SIW=$clog2((CAPACITY>BULLET_CAPACITY?CAPACITY:BULLET_CAPACITY)+1);
 localparam ROW_BULLET_CAPACITY=BULLET_CAPACITY+PLAYER_BULLET_CAPACITY+MISSILE_CAPACITY;
 localparam RCW=$clog2((CAPACITY>ROW_BULLET_CAPACITY?CAPACITY:ROW_BULLET_CAPACITY)+1);
 `include "enemy_catalog.vh"
 `include "enemy_tri_rot.vh"
 wire multi_mode,table_ready;wire [31:0] table_data,table_count,table_enabled,table_seq,table_rdata;
 wire bullet_mode,bullet_ready,pair_ready;wire [31:0] bullet_count,bullet_seq;
 wire player_bullet_mode,player_bullet_ready;wire [31:0] player_bullet_count,player_bullet_seq;
 wire missile_mode,missile_ready;wire [31:0] missile_count,missile_seq;
 wire effect_mode,effect_ready;wire [31:0] effect_count,effect_seq;
 reg latched_missiles,scan_missile,obj_missile;
 reg latched_player_bullets,latched_effects,scan_player_bullet,scan_effect;
 reg latched_multi,latched_bullets,scan_bullet,obj_bullet,obj_effect,obj_pb;
 reg [1:0] member_index; // current member within one immutable bullet row entry
 reg [1:0] row_kind; // 0 enemies, 1 enemy+player bullets, 2 effects
 reg [SIW-1:0] scan_index;reg scan_word;reg [31:0] scan_d0;
 // Sorted-window scan: per table start pointer (first entry that may still
 // cover a later row) and an "everything before was expired" flag.
 reg [SIW-1:0] start_enemy,start_bullet,start_player_bullet,start_effect,start_missile;
 reg scan_all_expired,scan_valid;
 reg [CW-1:0] row_count;reg [RCW-1:0] row_cursor;
 reg [$clog2(ROW_BULLET_CAPACITY+1)-1:0] bullet_row_count;
 reg [$clog2(EFFECT_CAPACITY+1)-1:0] effect_row_count;
 localparam ROW_BULLET_OFFSET=(1<<IW),ROW_EFFECT_OFFSET=ROW_BULLET_OFFSET+ROW_BULLET_CAPACITY,
   ROW_DEPTH=(1<<$clog2(ROW_EFFECT_OFFSET+EFFECT_CAPACITY));
 wire [RCW-1:0] selected_row_count=row_kind==2?effect_row_count:(row_kind==1?bullet_row_count:row_count);
 wire [31:0] selected_table_count=scan_effect?effect_count:(scan_player_bullet?player_bullet_count:(scan_missile?missile_count:(scan_bullet?bullet_count:table_count)));
 (* syn_ramstyle="block_ram" *) reg [27:0] row_list[0:ROW_DEPTH-1]; // GHD5R: [27:24] triple direction
 reg [27:0] row_entry;
 wire [3:0] table_dir;
 reg block_has_objects;
 // GHD5 block hit list: BLOCK_CHECK records (kind,index) of every row entry whose block hull hits the block; the object pass then
 // visits only those (same order as the full walk). More than HL_DEPTH hits: hl_over -> the original full walk for that block.
 localparam HL_DEPTH=16;
 (* syn_ramstyle="registers" *) reg [1:0] hl_kind[0:HL_DEPTH-1];
 (* syn_ramstyle="registers" *) reg [RCW-1:0] hl_idx[0:HL_DEPTH-1];
 reg [4:0] hl_n,hl_pos;reg hl_over,hl_done;
 wire hl_mode=!hl_over && latched_multi; // legacy single-object path keeps the original walk
 wire [3:0] hl_next=hl_pos[3:0]+1'b1;
 wire [RCW-1:0] hl_first_cursor=(hl_mode && hl_n!=0)?hl_idx[0]:{RCW{1'b0}};
 wire [1:0] hl_first_kind=(hl_mode && hl_n!=0)?hl_kind[0]:2'd0;
 wire obj_list_end=hl_mode?hl_done:(row_cursor>=selected_row_count);
 reg signed [11:0] obj_x;reg [6:0] obj_row;reg [1:0] obj_material;reg [2:0] obj_frame;
 reg [4:0] source_beat,last_source_beat;reg [7:0] object_mask;reg [31:0] object_alpha;
 reg [4:0] fragment_last_beat;
 reg [127:0] object_mask_cache;
 reg [31:0] object_mask_cache_tag;reg object_mask_cache_valid;
 reg multi_read_pending;
 // ---- membership of the scanned entry (word1 in table_data, word0 in scan_d0) ----
 wire [31:0] row_material_size=fixed_material_size({6'd0,table_data[13:12]});
 wire enemy_row_member=table_data[11] && row>=table_data[10:0] &&
                 ({21'd0,row}-{16'd0,table_data[10:0]})<row_material_size;
 wire enemy_row_expired=!table_data[11] || ({21'd0,table_data[10:0]}+row_material_size<={21'd0,row});
 wire enemy_row_beyond=table_data[11] && table_data[10:0]>row;
 wire signed [12:0] bullet_y=$signed({table_data[11],table_data[11:0]});
 wire signed [12:0] bullet_relative=$signed({2'b0,row})-bullet_y;
 wire scan_triple=!scan_player_bullet && !scan_missile && scan_d0[15];
 // GHD5: the alt triple style (round disks, triangle) is taller; the style is word1[13:12] here (table_data = word1 during the scan)
 wire signed [12:0] scan_triple_height={8'd0,tri_rot_h({table_data[13:12]==`BULLET_TRIPLE_ALT_STYLE,table_dir,2'd0})}; // GHD5R: rotated bbox height // signed: keeps the bullet_relative compares signed (an unsigned operand would make rows above a group look expired)
 wire bullet_row_member=bullet_relative>=0 && bullet_relative<((scan_player_bullet || scan_missile)?32:(scan_triple?scan_triple_height:`BULLET_DRAW_HEIGHT)) &&
       (scan_player_bullet || (scan_missile?scan_d0[12]:(|{scan_d0[14],scan_d0[13:12]})));
 wire bullet_row_expired=bullet_relative>=((scan_player_bullet || scan_missile)?32:(scan_triple?scan_triple_height:`BULLET_DRAW_HEIGHT));
 wire bullet_row_beyond=bullet_relative<0;
 wire [31:0] effect_size=fixed_effect_size({30'd0,scan_d0[16:15]});
 wire signed [12:0] effect_y=$signed({table_data[11],table_data[11:0]});
 wire signed [12:0] effect_delta=$signed({2'b0,row})-effect_y;
 wire effect_row_member=effect_delta>=0 && effect_delta<$signed(effect_size);
 wire effect_row_expired=effect_delta>=$signed(effect_size);
 wire effect_row_beyond=effect_delta<0;
 wire row_member=scan_effect?effect_row_member:(scan_bullet?bullet_row_member:enemy_row_member);
 wire row_expired=scan_effect?effect_row_expired:(scan_bullet?bullet_row_expired:enemy_row_expired);
 wire row_beyond=scan_effect?effect_row_beyond:(scan_bullet?bullet_row_beyond:enemy_row_beyond);
 wire [6:0] row_relative=row-table_data[10:0];
 wire [6:0] effect_relative=effect_delta[6:0];
 // ---- candidate decode from row_list by kind ----
 wire signed [12:0] candidate_anchor=row_kind==2?$signed({row_entry[11],row_entry[11:0]}):
                                 (row_kind==1?$signed({row_entry[11],row_entry[11:0]}):$signed({2'b0,row_entry[10:0]}));
 wire candidate_missile=row_kind==1 && row_entry[22];
 wire candidate_triple=row_kind==1 && !candidate_missile && row_entry[23];
 wire [2:0] candidate_mask={row_entry[21],row_entry[19:18]};
 wire candidate_pb=row_kind==1 && candidate_mask==0 && !candidate_triple && !candidate_missile;
 wire candidate_alt={row_entry[20],row_entry[17]}==`BULLET_TRIPLE_ALT_STYLE; // GHD5: per-style triple geometry
 wire [3:0] candidate_dir=row_entry[27:24]; // GHD5R: rotated triple, offsets from the anchor (= rotated bbox top-left)
 wire [4:0] member_dx=candidate_triple?tri_rot_dx({candidate_alt,candidate_dir,member_index}):(member_index==1?5'd`BULLET_MEMBER_DX:5'd0);
 wire [4:0] member_dy=candidate_triple?tri_rot_dy({candidate_alt,candidate_dir,member_index}):5'd0;
 wire signed [12:0] candidate_x=candidate_anchor+((row_kind==1 && !candidate_missile && state!=BLOCK_CHECK)?$signed({8'd0,member_dx}):13'sd0);
 wire [4:0] group_row=row_entry[16:12];
 wire member_row_live=group_row>=member_dy && group_row<member_dy+`BULLET_DRAW_HEIGHT;
 wire candidate_live=candidate_missile || candidate_pb || row_kind!=1 || (state==BLOCK_CHECK ? (|candidate_mask) : (candidate_mask[member_index] && member_row_live));
 wire [6:0] candidate_row=row_kind==2?row_entry[18:12]:(row_kind==1?{2'b0,group_row-member_dy}:row_entry[17:11]);
 wire [1:0] candidate_material=row_kind==2?row_entry[23:22]:(row_kind==1?(candidate_missile?{1'b0,row_entry[17]}:(candidate_pb?2'b00:{row_entry[20],row_entry[17]})):row_entry[19:18]);
 wire [2:0] candidate_frame=row_entry[21:19];
 wire signed [12:0] candidate_size=row_kind==1?(candidate_missile?(row_entry[17]?13'sd12:13'sd8):(candidate_pb?13'sd16:((state==BLOCK_CHECK)?(candidate_triple?{8'd0,tri_rot_w({candidate_alt,candidate_dir,2'd0})}:(row_entry[19]?13'sd28:13'sd8)):13'sd8))):((row_kind==2?row_entry[22]:(candidate_material==3))?13'sd128:13'sd64);
 wire another_member=row_kind==1 && !candidate_pb && !candidate_missile && (candidate_triple?
      ((member_index==0 && (|candidate_mask[2:1])) || (member_index==1 && candidate_mask[2])):
      (member_index==0 && candidate_mask[1]));
 wire [1:0] next_member=(candidate_triple && member_index==0 && !candidate_mask[1])?2:member_index+1'b1;
 wire signed [12:0] block_x=$signed({2'b0,column,3'd0});
 wire signed [12:0] block_end_x=$signed({1'b0,end_column,3'd0});
 wire candidate_hit=candidate_live && candidate_x<block_end_x && candidate_x+candidate_size>block_x;
 wire signed [12:0] span_left=block_x>candidate_x?block_x:candidate_x;
 wire signed [12:0] span_right=block_end_x<(candidate_x+candidate_size)?block_end_x:(candidate_x+candidate_size);
 wire [12:0] source_left=span_left-candidate_x;
 wire [12:0] source_last=span_right-candidate_x-1'b1;
 wire player_row_active=latched_multi && latched_player[31] && row>=latched_player[21:11] &&
      ({21'd0,row}-{21'd0,latched_player[21:11]})<`PLAYER_HEIGHT;
 wire [12:0] player_x={2'b0,latched_player[10:0]};
 wire [12:0] player_end_x=player_x+`PLAYER_WIDTH;
 wire player_hit=player_row_active && player_x<block_end_x && player_end_x>block_x;
 wire [12:0] player_source_left=(block_x>player_x?block_x:player_x)-player_x;
 wire [12:0] player_source_last=(block_end_x<player_end_x?block_end_x:player_end_x)-player_x-1'b1;
 wire [31:0] player_color_row=({25'd0,obj_row}<<7)+({25'd0,obj_row}<<6); // 96 * 2
 wire [31:0] player_mask_row=({25'd0,obj_row}<<3)+({25'd0,obj_row}<<2); // 96 / 8
 // Effect planes: size 64 -> 128 B colour rows, 8192 B frames, 32 B alpha rows, 2048 B alpha frames.
 wire effect_large=obj_material[0];
 wire [31:0] effect_color_offset=effect_large ? ({29'd0,obj_frame}<<15)+({25'd0,obj_row}<<8) : ({29'd0,obj_frame}<<13)+({25'd0,obj_row}<<7);
 wire [31:0] effect_alpha_offset=effect_large ? ({29'd0,obj_frame}<<13)+({25'd0,obj_row}<<6) : ({29'd0,obj_frame}<<11)+({25'd0,obj_row}<<5);
 wire [1:0] bullet_cache_material=obj_missile?{1'b1,obj_material[0]}:{1'b0,obj_pb}; // PB page1; four enemy styles share page0
 wire [4:0] bullet_source_row=(obj_pb || obj_missile)?obj_row[4:0]:{obj_material[1],obj_row[3:0]};
 wire bullet_source_beat=(obj_pb || obj_missile)?source_beat[0]:obj_material[0];
 wire [31:0] decor_base=obj_skin?`DECOR_BASE1:`DECOR_BASE0;
 wire [31:0] object_color_address=obj_player ?
      fixed_asset_base(`PLAYER_COLOR_ID)+player_color_row+{23'b0,source_beat,4'b0} : obj_decor ?
      decor_base+{25'd0,obj_row[4:0],6'd0}+{23'b0,source_beat,4'b0} : obj_effect ?
      fixed_effect_color({30'b0,obj_material})+effect_color_offset+{23'b0,source_beat,4'b0} : obj_bullet ?
      fixed_bullet_color({30'b0,bullet_cache_material})+{22'b0,bullet_source_row,5'b0}+{27'b0,bullet_source_beat,4'b0} :
      fixed_material_color({30'd0,obj_material})+
      (obj_material==3?{17'd0,obj_row,8'd0}:{18'd0,obj_row,7'd0})+{23'd0,source_beat,4'd0};
 wire [31:0] object_mask_offset=obj_player ? player_mask_row+source_beat : obj_decor ? 32'd2048+{23'd0,obj_row[4:0],4'd0}+{25'b0,source_beat,2'b0} :
      obj_effect ? effect_alpha_offset+{25'b0,source_beat,2'b0} :
      obj_bullet ? {26'b0,bullet_source_row,1'b0}+{31'b0,bullet_source_beat} :
      (obj_material==3?{21'd0,obj_row,4'd0}:{22'd0,obj_row,3'd0})+source_beat;
 wire [31:0] object_mask_address=(obj_player?fixed_asset_base(`PLAYER_MASK_ID):(obj_decor?decor_base:(obj_effect?fixed_effect_alpha({30'b0,obj_material}):
      (obj_bullet?fixed_bullet_mask({30'b0,bullet_cache_material}):fixed_material_mask({30'd0,obj_material})))))+{object_mask_offset[31:4],4'd0};
 // A mask byte describes one colour beat (4 alpha bytes per beat for effects).
 // End a colour burst before the cached 16-byte segment or its AXI 4 KiB page.
 // Effect alpha for the current source beat, from the cached 16-byte segment.
 wire [31:0] beat_alpha=alpha_lanes(object_mask_cache[{object_mask_offset[3:2],5'd0}+:32]);
 wire [31:0] fetched_alpha=alpha_lanes(rdata[{object_mask_offset[3:2],5'd0}+:32]);
 wire alpha_all_zero=beat_alpha==32'd0;
 reg alpha_binary; // every lane is 0 or 15: an exact masked paste
 always @* begin alpha_binary=1'b1;for(integer k=0;k<8;k=k+1)if(beat_alpha[k*4+:4]!=4'd0 && beat_alpha[k*4+:4]!=4'd15)alpha_binary=1'b0;end
 // A transparent 16-byte alpha segment covers four colour beats: skip them.
 wire [4:0] segment_last_beat={source_beat[4:2],2'b11};
 wire [4:0] skip_to_beat=segment_last_beat<last_source_beat?segment_last_beat:last_source_beat;
 wire [5:0] fragment_remaining={1'b0,last_source_beat}-{1'b0,source_beat}+1'b1;
 wire [5:0] mask_remaining=obj_effect ? (6'd16-{2'b0,object_mask_offset[3:0]})>>2 : 6'd16-{2'b0,object_mask_offset[3:0]};
 wire [8:0] page_remaining=9'd256-{1'b0,object_color_address[11:4]};
 wire [5:0] mask_bounded=fragment_remaining<mask_remaining?fragment_remaining:mask_remaining;
 wire [5:0] fragment_beats=page_remaining<mask_bounded?page_remaining[5:0]:mask_bounded;
 wire signed [12:0] paste_x=$signed({obj_x[11],obj_x})+$signed({5'b0,source_beat,3'b0});
 wire signed [9:0] paste_beat=paste_x[12:3];
 wire [2:0] paste_shift=obj_x[2:0];
 // One indexed pixel write port. Rotate once, then use fixed 16-bit lanes;
 // variable indexed part-select writes infer large replicated write decoders.
 reg [7:0] source_visible;
 reg [2:0] blend_lane;reg blend_second;
 wire [255:0] rotated_color={color,color} >> {paste_shift,4'd0};
 wire [15:0] rotated_mask={source_visible,source_visible} >> paste_shift;
 wire paste_second=state==PASTE1 || (blend_second && (state==BLEND_LOAD || state==BLEND));
 wire signed [10:0] paste_destination=$signed({paste_beat[9],paste_beat})+$signed({10'b0,paste_second});
 wire [10:0] paste_index=paste_destination-$signed({3'b0,column});
 wire paste_in_block=paste_destination>=$signed({3'b0,column}) && paste_destination<$signed({2'b0,end_column});
 wire [7:0] paste_lane_mask=rotated_mask[7:0] & (paste_second?~(8'hff >> paste_shift):(8'hff >> paste_shift));
 reg pixel_write;reg [4:0] pixel_index;
 reg [127:0] pixel_data;reg [7:0] pixel_mask;
 integer lane;
 // ISOLATED EXPERIMENT: pairs of {mask,color}; same 17408 cache payload bits.
 (* syn_ramstyle="block_ram" *) reg [33:0] bullet_cache[0:1023];
 reg [33:0] bullet_cache_rd;
 reg [1:0] cache_material;reg cache_stage,cache_valid,fill_phase;reg [1:0] cache_chunk;
 reg [2:0] fill_row;reg [3:0] fill_x;reg [15:0] fill_mask_row;
 reg [2:0] cache_step;
 wire [127:0] pixel_rd=pixels[index[4:0]]; // single shared read port (PUSH, blend, cache fill)
 // BGT-A: I4 nibbles of beat column column+index (column is a multiple of 16, so the half is index[0]); lane order as pixels[].
 wire [31:0] i4_lanes=alpha_lanes(index[0]?lb_q[63:32]:lb_q[31:0]);
 // R1 lane flags: bit index*8+lane set once that lane of the work buffer was
 // written in the current block. Only consulted in gradient multi frames.
 reg [255:0] bg_written;
 wire lazy_frame=LAZY_BG && latched_multi && (latched_background || latched_i4);
 wire [7:0] rd_written=bg_written[{index[4:0],3'd0} +: 8];
 reg [127:0] pixel_bg_rd; // pixel_rd with never-written lanes replaced by this row's gradient
 always @* for(integer k=0;k<8;k=k+1)
  pixel_bg_rd[k*16+:16]=(lazy_frame && !rd_written[k])?(latched_i4?pal[i4_lanes[k*4+:4]]:{bg_red,bg_green,bg_blue}):pixel_rd[k*16+:16];
 wire [15:0] fill_mask_lane=pixel_rd[(3'd7-fill_row)*16 +: 16];
 wire [31:0] fill_color_pair=pixel_rd[(3'd6-fill_x[2:0])*16 +: 32];
 wire [9:0] cache_raddr={bullet_cache_material,bullet_source_row,bullet_source_beat,cache_step[1:0]};
 // ---- 4-bit straight alpha blend, one destination lane per cycle ----
 // Lane bl of the destination beat takes source lane (bl+shift) mod 8, which is
 // exactly rotated_color/rotated alpha lane bl; the destination lane is read from
 // pixels[index] (index held at the destination beat) and written back in place.
 wire [2:0] bl=blend_lane;
 wire [2:0] blend_source_lane=blend_lane+paste_shift;
 wire [15:0] blend_src=color[blend_source_lane*16 +: 16];
 wire [15:0] blend_dst_px=pixel_bg_rd[bl*16 +: 16];
 wire [3:0] blend_a=object_alpha[blend_source_lane*4 +: 4];
 wire [4:0] blend_w={1'b0,blend_a}+{4'd0,blend_a==4'd15};
 wire signed [6:0] dr=$signed({2'b0,blend_src[15:11]})-$signed({2'b0,blend_dst_px[15:11]});
 wire signed [7:0] dg=$signed({2'b0,blend_src[10:5]})-$signed({2'b0,blend_dst_px[10:5]});
 wire signed [6:0] db=$signed({2'b0,blend_src[4:0]})-$signed({2'b0,blend_dst_px[4:0]});
 wire signed [12:0] pr=dr*$signed({1'b0,blend_w});
 wire signed [13:0] pg=dg*$signed({1'b0,blend_w});
 wire signed [12:0] pb=db*$signed({1'b0,blend_w});
 wire signed [12:0] rr=$signed({8'd0,blend_dst_px[15:11]})+((pr+13'sd8)>>>4);
 wire signed [13:0] rg=$signed({8'd0,blend_dst_px[10:5]})+((pg+14'sd8)>>>4);
 wire signed [12:0] rb=$signed({8'd0,blend_dst_px[4:0]})+((pb+13'sd8)>>>4);
 wire [15:0] blend_result={rr[4:0],rg[5:0],rb[4:0]};
 wire [23:0] row_new24=scan_effect ? {scan_d0[16:15],scan_d0[14:12],effect_relative,scan_d0[11:0]} :
       scan_bullet ? ((scan_missile && !scan_player_bullet)?{1'b0,1'b1,4'b0000,table_data[12],bullet_relative[4:0],scan_d0[11:0]}:{(scan_player_bullet?1'b0:scan_d0[15]),1'b0,(scan_player_bullet?1'b0:scan_d0[14]),table_data[13],(scan_player_bullet?2'b00:scan_d0[13:12]),table_data[12],bullet_relative[4:0],scan_d0[11:0]}) :
                     {table_data[13:12],row_relative,scan_d0[10:0]}; // GHD5R: unchanged 24-bit entry, direction appended above it
 always @(posedge clk)begin
  if(state==SCAN_A && scan_valid && row_member)
    row_list[scan_effect ? ROW_EFFECT_OFFSET+effect_row_count : (scan_bullet ? ROW_BULLET_OFFSET+bullet_row_count : row_count)]<=
       {((scan_bullet && !scan_missile && !scan_player_bullet)?table_dir:4'd0),row_new24};
  row_entry<=row_list[(row_kind==2?ROW_EFFECT_OFFSET:(row_kind==1?ROW_BULLET_OFFSET:0))+row_cursor];
  if(state==CACHE_FILL && fill_phase)bullet_cache[{cache_material,cache_chunk,fill_row,fill_x[3:1]}]<=
    {fill_mask_row[4'd15-fill_x],fill_color_pair[31:16],fill_mask_row[4'd14-fill_x],fill_color_pair[15:0]};
  bullet_cache_rd<=bullet_cache[cache_raddr];
 end
 enemy_scene_table #(.CAPACITY(CAPACITY),.BULLET_CAPACITY(BULLET_CAPACITY),.PLAYER_BULLET_CAPACITY(PLAYER_BULLET_CAPACITY),.EFFECT_CAPACITY(EFFECT_CAPACITY)) scene_table(
  .clk(clk),.rst_n(rst_n),.addr(reg_addr),.write(reg_write),.wdata(reg_wdata),.rdata(table_rdata),
  .engine_idle(state==IDLE),.claim(perf_claim),.mode_multi(multi_mode),.ready(table_ready),
  .frame_count(table_count),.frame_enabled(table_enabled),.frame_seq(table_seq),
  .read_index({{(32-SIW){1'b0}},scan_index}),.read_word({1'b0,scan_word}),.read_data(table_data),.read_dir(table_dir),.read_bullet(scan_bullet),
  .bullet_mode(bullet_mode),.bullet_ready(bullet_ready),.pair_ready(pair_ready),.bullet_count(bullet_count),.bullet_seq(bullet_seq),
  .read_player_bullet(scan_player_bullet),.player_bullet_mode(player_bullet_mode),.player_bullet_ready(player_bullet_ready),
  .player_bullet_count(player_bullet_count),.player_bullet_seq(player_bullet_seq),
  .read_effect(scan_effect),.effect_mode(effect_mode),.effect_ready(effect_ready),
  .effect_count(effect_count),.effect_seq(effect_seq),
  .read_missile(scan_missile && !scan_player_bullet && !scan_effect),.missile_mode(missile_mode),.missile_ready(missile_ready),.missile_count(missile_count),.missile_seq(missile_seq));
 wire cancel_now=reg_write && reg_addr==12'h164 && reg_wdata==2;
 // ---- BGT-A decor geometry: 16 slots in parallel (CMD) ----
 function signed [12:0] dsx(input [31:0] w);dsx=$signed({w[11],w[11:0]});endfunction
 function signed [12:0] dsy(input [31:0] w);dsy=$signed({w[23],w[23:12]});endfunction
 reg [15:0] decor_rows_next,decor_block_hits;reg [4:0] decor_rel_next[0:15];reg signed [12:0] drel;
 always @* for(integer k=0;k<16;k=k+1)begin
  drel=$signed({2'b0,row})-dsy(latched_decor[k]);
  decor_rows_next[k]=latched_decor_en && latched_decor[k][31] && drel>=0 && drel<32;
  decor_rel_next[k]=drel[4:0];
  decor_block_hits[k]=decor_rows[k] && dsx(latched_decor[k])<block_end_x && dsx(latched_decor[k])+13'sd32>block_x;
 end
 wire decor_block_hit=|decor_block_hits;
 reg [3:0] ds;always @* begin ds=0;for(integer k=15;k>=0;k=k-1)if(decor_pending[k])ds=k;end // lowest pending slot first
 wire [31:0] dw=latched_decor[ds];
 wire signed [12:0] dxs=dsx(dw);
 wire signed [12:0] dspan_left=block_x>dxs?block_x:dxs;
 wire signed [12:0] dspan_right=block_end_x<(dxs+13'sd32)?block_end_x:(dxs+13'sd32);
 wire [12:0] dsrc_left=dspan_left-dxs;wire [12:0] dsrc_last=dspan_right-dxs-1'b1;
 // ---- BGT-A line fetch: row y at BG_I4_BASE+960y, 60 beats, split at a 4 KiB boundary ----
 // A2: the fetch target is a row number (row y or y+1), not necessarily the row being composed.
 wire row_ready=row[0]?(lb_valid[1] && lb_tag1==row):(lb_valid[0] && lb_tag0==row);
 wire [10:0] next_row=row+1'b1;
 wire next_ready=next_row[0]?(lb_valid[1] && lb_tag1==next_row):(lb_valid[0] && lb_tag0==next_row);
 wire [10:0] lf_target=row_ready?next_row:row;
 wire lf_want=latched_i4 && !lf_active && (!row_ready || (row!=11'd1079 && !next_ready));
 wire [31:0] row32={21'd0,lf_target};
 wire [31:0] lf_row_addr=(latched_menu?`MENU_I4_BASE:`BG_I4_BASE)+(row32<<10)-(row32<<6);
 wire [8:0] lf_page_left=9'd256-{1'b0,lf_row_addr[11:4]};
 wire [6:0] lf_first_beats=lf_page_left<9'd60 ? lf_page_left[6:0] : 7'd60;
 wire lf_rready=lf_active && !lf_ar && !lf_skid_full;
 wire lf_beat=lf_rready && rvalid;
 assign lb_waddr=lf_skid_full?{1'b0,lf_row[0],lf_skid_word,1'b1}:{1'b0,lf_row[0],lf_count[5:0],1'b0};
 assign lb_wdata=lf_skid_full?lf_skid:rdata[63:0];
 wire aborting=cancel_pending || cancel_now || error_code!=0 || display_error;
 wire [8:0] logical_column={1'b0,column}+index;
 wire [10:0] enemy_row=row-latched_y;
 wire in_enemy=row>=latched_y && enemy_row<64 && logical_column>=116 && logical_column<124;
 wire [2:0] enemy_column=logical_column-9'd116;
 wire [3:0] mask_byte={enemy_row[0],enemy_column};
 wire [7:0] mask=rdata[mask_byte*8+:8];
 wire reading=state==BG_R || state==COLOR_R || state==MASK_R || state==CACHE_R || state==PAL_R;
 assign end_column={1'b0,column}+block_beats;
 wire direct_block=latched_multi ? !block_has_objects : !(row>=latched_y && enemy_row<64 && column<124 && end_column>116);
 wire r1_skip=lazy_frame && !direct_block; // R1: object block of a gradient multi frame
 wire q8_blocked;
 wire gen_fire=state==BG_GEN && reset_count==0 && !aborting && (!direct_block || !q8_blocked);
 // CACHE_R deliberately selects the real bus, even in a gradient frame.
 wire [127:0] background_beat=state==BG_GEN?{8{bg_red,bg_green,bg_blue}}:reverse_pixels(rdata);
 assign rready=(reading && (state!=BG_R || !direct_block || !q8_blocked || aborting)) || lf_rready; // never both: lf only while not reading
 always @*begin
  for(integer k=0;k<8;k=k+1)
   source_visible[7-k]=obj_effect ? (object_alpha[(7-k)*4+:4]!=0) :
     (object_mask[7-k] && (obj_player || obj_bullet || !latched_key[16] || color[(7-k)*16+:16]!=latched_key[15:0]));
  pixel_write=0;pixel_index=0;pixel_data=color;pixel_mask=0;
  if(((((state==BG_R && !direct_block) || state==CACHE_R) && rvalid && rready) ||
      (gen_fire && !direct_block && !r1_skip)) && received<block_beats)begin
   pixel_write=1;pixel_index=state==CACHE_R?{cache_stage,received[3:0]}:received[4:0];pixel_data=background_beat;pixel_mask=8'hff;
  end else if(state==MASK_R && !latched_multi && rvalid && rlast && !aborting)begin
   pixel_write=1;pixel_index=index[4:0];
   for(integer k=0;k<8;k=k+1)
    pixel_mask[7-k]=mask[7-k] && (!latched_key[16] || color[(7-k)*16+:16]!=latched_key[15:0]);
  end else if((state==PASTE0 || state==PASTE1) && !aborting && paste_in_block)begin
   pixel_write=1;pixel_index=paste_index[4:0];pixel_data=rotated_color[127:0];
   pixel_mask=paste_lane_mask;
  end else if(state==BLEND && !aborting && paste_in_block)begin
   pixel_write=1;pixel_index=paste_index[4:0];pixel_data={8{blend_result}};
   pixel_mask=paste_lane_mask & (8'd1 << bl);
  end
 end
 always @(posedge clk)if(pixel_write)
  for(lane=0;lane<8;lane=lane+1)if(pixel_mask[lane])pixels[pixel_index][lane*16+:16]<=pixel_data[lane*16+:16];
 always @(posedge clk)
  if(state==BG_GEN && reset_count==0 && r1_skip)bg_written<=256'd0;
  else if(pixel_write)bg_written[{pixel_index,3'd0} +: 8]<=bg_written[{pixel_index,3'd0} +: 8]|pixel_mask;


 assign wr_addr={1'b0,group_addr};assign wr_beats={16'd0,group_beats};
 wire [8:0] group_total={1'b0,group_beats}+{4'd0,block_beats[5:1]};
 wire [31:0] group_bytes={20'd0,group_beats,4'd0};
 wire [31:0] block_bytes={23'd0,block_beats,3'd0};
 wire [31:0] source_block_bytes={22'd0,block_beats,4'd0};
 wire [31:0] next_dst=dst+block_bytes;
 wire [7:0] next_column=end_column==240?8'd0:end_column[7:0];
 wire [31:0] next_src=src+source_block_bytes;
 function [5:0] bounded_beats(input [31:0] address,input [7:0] col);
  reg [8:0] n,left_row,left_page;begin
   left_row=9'd240-{1'b0,col};left_page=9'd256-{1'b0,address[11:4]};
   n=32;if(left_row<n)n=left_row;if(left_page<n)n=left_page;
   bounded_beats=n[5:0];
  end
 endfunction
 wire [31:0] perf_rdata;
 // Frame probe (rtl/frame_probe.v): compose = every state between the accepted claim and the entry into PRESENT.
 assign probe_composing=state!=IDLE && state!=PRESENT && state!=SHOWN && state!=DRAIN && state!=CLEAN;
 assign probe_present=state==PRESENT;
 assign perf_claim=state==IDLE && reg_write && reg_addr==12'h164 && reg_wdata==1 &&
      owner_grant && ddr_idle && fifo_empty && (!multi_mode || pair_ready) &&
      (back_base==32'h01000000 || back_base==32'h01400000);
 // 1.3: W/B buckets count only while composing waits on the writer; 1.4: buckets omitted.
 enemy_perf_counters #(.ABI(PERF_DETAIL?32'h45500103:32'h45500104),.DETAIL(PERF_DETAIL)) perf (
  .clk(clk),.rst_n(rst_n),.start(perf_claim),.phase(state),
  .done(state==PRESENT && present_ack && generation!=start_generation && display_ok),
  .ar_selected(perf_ar_selected),.arready(arready),.rvalid(rvalid),.rready(rready),.fifo_full(fifo_full),
  .awvalid(perf_awvalid),.awready(perf_awready),.wvalid(perf_wvalid),.wready(perf_wready),
  .wlast(perf_wlast),.bvalid(perf_bvalid),.bready(perf_bready),
  .y(latched_y),.back(latched_back),.generation(generation),.addr(reg_addr),.rdata(perf_rdata));
 // Final quantization only; the RGB565 work block and all source data remain unchanged.
 `include "framebuffer_q8.vh"
 reg [63:0] q8_half;
 reg q8_half_valid,q8_out_valid;
 reg [143:0] q8_out;
 assign q8_blocked=q8_out_valid && fifo_full;
 wire q8_take=!q8_blocked && !aborting;
 wire q8_direct_gen=direct_block && state==BG_GEN && reset_count==0;
 wire q8_direct_image=direct_block && state==BG_R && rvalid && rready;
 wire q8_push=state==PUSH;
 wire q8_emit=q8_direct_gen || ((q8_direct_image || q8_push) && q8_half_valid);
 function [63:0] q8_eight(input [127:0] p,input [1:0] y);
  integer k;begin for(k=0;k<8;k=k+1)q8_eight[k*8+:8]=framebuffer_q8_encode(p[(7-k)*16+:16],y,k%4);end
 endfunction
 // BGT-A row Q8 table build: the shared encoders see {pal[2s] x4, pal[2s+1] x4} (px0..7 = lanes 7..0) for 8 cycles.
 wire [127:0] qt_pixels={{4{pal[{qt_step,1'b0}]}},{4{pal[{qt_step,1'b1}]}}};
 wire [63:0] q8_current=q8_eight(q8_push?pixel_bg_rd:(qt_active?qt_pixels:background_beat),row[1:0]);
 reg [127:0] q8_i4; // 16 direct I4 pixels of lb_q: byte k = Q8(pal[nibble k]) at phase k%4 (x = 8*column+16j+k)
 always @* for(integer k=0;k<16;k=k+1)q8_i4[k*8+:8]=q8tab[{lb_q[(k/8)*32+((k%8)/2)*8+(((k%8)%2)?0:4)+:4],2'b00}+(k%4)];
 always @(posedge clk)if(qt_active)for(integer p=0;p<4;p=p+1)begin
  q8tab[{qt_step,1'b0,2'b00}+p]<=q8_current[p*8+:8];q8tab[{qt_step,1'b1,2'b00}+p]<=q8_current[(4+p)*8+:8];
 end
 // Line buffer: one write per cycle (low half when a beat is accepted, buffered high half the next cycle);
 // registered read whose address is the NEXT cycle's beat column (direct emission, PUSH, BLEND).
 always @* begin
  lb_raddr={row[0],column[7:1]};
  if(state==BG_GEN)lb_raddr={row[0],7'd0}+(({1'b0,column}+{3'd0,received}+((gen_fire && direct_block)?9'd2:9'd0))>>1);
  else if(state==PUSH)lb_raddr={row[0],7'd0}+(({1'b0,column}+{3'd0,index}+((q8_blocked || aborting || index==block_beats-1'b1)?9'd0:9'd1))>>1);
  else if(state==BLEND_LOAD)lb_raddr={row[0],7'd0}+(({1'b0,column}+{4'd0,paste_index[4:0]})>>1);
  else if(state==BLEND)lb_raddr={row[0],7'd0}+(({1'b0,column}+{3'd0,index})>>1);
 end
 always @(posedge clk)begin
  lb_q<=lb[{1'b0,lb_raddr}];
  if(lf_skid_full || lf_beat)lb[lb_waddr]<=lb_wdata; // one write statement = one RAM write port
 end
 always @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin q8_half<=0;q8_half_valid<=0;q8_out<=0;q8_out_valid<=0;end
  else if(aborting || state==IDLE || state==RESET || state==CLEAN)begin
   q8_half_valid<=0;q8_out_valid<=0;
  end else if(q8_take)begin
   q8_out_valid<=q8_emit;
   if(q8_emit)q8_out<={16'hffff,q8_direct_gen?(latched_i4?q8_i4:{q8_current,q8_current}):{q8_current,q8_half}};
   if(q8_direct_image || q8_push)begin
    q8_half_valid<=!q8_half_valid;
    if(!q8_half_valid)q8_half<=q8_current;
   end
  end
 end
 assign fifo_valid=q8_out_valid && !aborting;
 assign fifo_data=q8_out;
 integer j;
 // 4 alpha bytes (high nibble = left pixel) -> lane order, pixel k at lane 7-k.
 function [31:0] alpha_lanes(input [31:0] raw);
  integer k;begin for(k=0;k<8;k=k+1) alpha_lanes[(7-k)*4+:4]=raw[(k/2)*8+((k%2)?0:4)+:4];end
 endfunction
 function [127:0] reverse_pixels(input [127:0] raw);
  integer k;begin for(k=0;k<8;k=k+1) reverse_pixels[(7-k)*16+:16]=raw[k*16+:16];end
 endfunction
 always @* begin
  reg_rdata=0;
  case(reg_addr)
   12'h160:reg_rdata=32'h454e0101;
   12'h168:reg_rdata={24'd0,(state==IDLE),1'b0,(error_code!=0),shown,
                                (state==PRESENT),1'b0,(owner_active&&!shown),owner_active};
   12'h16c:reg_rdata={29'd0,error_code};
   12'h170:reg_rdata=latched_back;
   12'h174:reg_rdata={9'd0,committed};
   12'h178:reg_rdata={26'd0,state};
   12'h17c:reg_rdata=start_generation;
   12'h180:reg_rdata={21'd0,configured_y};
   12'h1e8:reg_rdata=32'h42330163; // CMD: + board keys/I2C, 16 decor slots, menu I4 source select (0x324); // R2 prefetch AR fix; // BGT-B (A2 + 600-group pool): P1000/RAM160 + I4 background mode + 8 A4 decor slots (low byte = RTL variant)
   12'h1f0:reg_rdata={15'd0,configured_key};
   12'h1f4:reg_rdata=32'h434b0101; // CK 1.1, independent enemy-only capability
   12'h1f8:reg_rdata={key_frame_valid,14'd0,latched_key};
   12'h280:reg_rdata=32'h504c0100; // PL1.0 independent static player, mask only
   12'h284:reg_rdata=configured_player;
   12'h288:reg_rdata=latched_player|{1'b0,player_frame_valid,30'b0};
   12'h28c:reg_rdata={30'b0,player_error};
   12'h300:reg_rdata=32'h42470100; // BG 1.0: independent image/gradient capability
   12'h304:reg_rdata={30'd0,configured_i4,configured_background};
   12'h308:reg_rdata={background_frame_valid,29'd0,latched_i4,latched_background};
   12'h30c:reg_rdata={30'd0,background_error};
   12'h380:reg_rdata=32'h42540101; // BT1.1: background mode 2 = I4 (BG_I4 + BG_PALETTE) and 8 frame-latched A4 decor slots
   12'h384:reg_rdata={31'd0,configured_decor_en};
   12'h388:reg_rdata=configured_decor[0];12'h38c:reg_rdata=configured_decor[1];12'h390:reg_rdata=configured_decor[2];12'h394:reg_rdata=configured_decor[3];
   12'h398:reg_rdata=configured_decor[4];12'h39c:reg_rdata=configured_decor[5];12'h3a0:reg_rdata=configured_decor[6];12'h3a4:reg_rdata=configured_decor[7];
   12'h3a8:reg_rdata={decor_frame_valid,29'd0,(latched_decor_en?decor_rows!=0:1'b0),latched_decor_en};
   12'h3ac:reg_rdata={30'd0,decor_error};
   12'h3b0:reg_rdata=configured_decor[8];12'h3b4:reg_rdata=configured_decor[9];12'h3b8:reg_rdata=configured_decor[10];12'h3bc:reg_rdata=configured_decor[11];
   12'h3c0:reg_rdata=configured_decor[12];12'h3c4:reg_rdata=configured_decor[13];12'h3c8:reg_rdata=configured_decor[14];12'h3cc:reg_rdata=configured_decor[15];
   12'h3d0:reg_rdata=32'd16; // CMD: decor slot capacity
   12'h324:reg_rdata={31'd0,configured_menu};
   12'h328:reg_rdata={31'd0,latched_menu};
   default:reg_rdata=((reg_addr>=12'h200 && reg_addr<=12'h27c) || (reg_addr>=12'h290 && reg_addr<=12'h2fc) || (reg_addr>=12'h340 && reg_addr<=12'h374))?table_rdata:perf_rdata;
  endcase
 end
 // Sorted window scan helpers: next table in draw order.
 wire scan_last_entry={{(32-SIW){1'b0}},scan_index}>=selected_table_count;
 always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin
   state<=IDLE;owner_active<=0;render_done<=0;araddr<=0;arlen<=0;arvalid<=0;
   fifo_reset<=0;wr_start<=0;index<=0;received<=0;reset_count<=0;row<=0;
   column<=0;block_beats<=0;dst<=0;src<=0;latched_back<=0;committed<=0;start_generation<=0;
   group_addr<=0;group_beats<=0;write_inflight<=0;inflight_bytes<=0;
   error_code<=0;cancel_pending<=0;shown<=0;color<=0;
   configured_y<=0;latched_y<=0;
   latched_missiles<=0;scan_missile<=0;obj_missile<=0;latched_player_bullets<=0;latched_effects<=0;scan_player_bullet<=0;scan_effect<=0;latched_multi<=0;latched_bullets<=0;scan_bullet<=0;
   member_index<=0;row_kind<=0;obj_bullet<=0;obj_effect<=0;obj_pb<=0;bullet_row_count<=0;effect_row_count<=0;scan_index<=0;scan_word<=0;scan_d0<=0;row_count<=0;row_cursor<=0;
   start_missile<=0;start_enemy<=0;start_bullet<=0;start_player_bullet<=0;start_effect<=0;scan_all_expired<=0;scan_valid<=0;
   block_has_objects<=0;hl_n<=0;hl_pos<=0;hl_over<=0;hl_done<=0;obj_x<=0;obj_row<=0;obj_material<=0;obj_frame<=0;source_beat<=0;last_source_beat<=0;object_mask<=0;object_alpha<=0;
   object_mask_cache<=0;object_mask_cache_tag<=0;object_mask_cache_valid<=0;multi_read_pending<=0;
   configured_key<=0;latched_key<=0;key_frame_valid<=0;
   configured_background<=0;latched_background<=0;background_frame_valid<=0;background_error<=0;
   bg_red<=1;bg_green<=5;bg_blue<=10;bg_er<=0;bg_eg<=0;bg_eb<=0;
   configured_player<=0;latched_player<=0;player_frame_valid<=0;player_error<=0;obj_player<=0;fragment_last_beat<=0;
   cache_material<=0;cache_chunk<=0;cache_stage<=0;cache_valid<=0;cache_step<=0;fill_phase<=0;fill_row<=0;fill_x<=0;fill_mask_row<=0;
   blend_lane<=0;blend_second<=0;
   configured_i4<=0;latched_i4<=0;configured_menu<=0;latched_menu<=0;pal_done<=0;lf_ar<=0;lf_active<=0;lf_second<=0;lf_skid_full<=0;lf_count<=0;lf_left<=0;
   lf_second_beats<=0;lf_skid_word<=0;lf_skid<=0;lf_second_addr<=0;qt_active<=0;qt_step<=0;lf_row<=0;lb_valid<=0;lb_tag0<=0;lb_tag1<=0;
   configured_decor_en<=0;latched_decor_en<=0;decor_frame_valid<=0;decor_error<=0;decor_rows<=0;decor_pending<=0;obj_decor<=0;obj_skin<=0;
   for(integer k=0;k<16;k=k+1)begin configured_decor[k]<=0;latched_decor[k]<=0;decor_rel[k]<=0;end
   for(integer k=0;k<16;k=k+1)pal[k]<=0;
  end else begin
   render_done<=0;fifo_reset<=0;wr_start<=0;
   // Only configure an idle engine. In-flight coordinate and Back are immutable.
   if(reg_write && reg_addr==12'h180 && state==IDLE && reg_wdata<=1016)
     configured_y<=reg_wdata[10:0];
   // One APB word updates enable/key atomically. Config and claim have distinct
   // addresses: config before claim applies; config after claim is ignored.
   if(reg_write && reg_addr==12'h1f0 && state==IDLE && reg_wdata[31:17]==0)
     configured_key<=reg_wdata[16:0];
   if(reg_write && reg_addr==12'h304)begin
    if(state!=IDLE)background_error<=2;
    else if(reg_wdata>32'd2)background_error<=1;
    else begin configured_background<=reg_wdata==1;configured_i4<=reg_wdata==2;background_error<=0;end
   end
   if(reg_write && reg_addr==12'h324)begin // CMD: I4 source select, idle only (same rules as 0x304)
    if(state!=IDLE)background_error<=2;
    else if(reg_wdata>32'd1)background_error<=1;
    else begin configured_menu<=reg_wdata[0];background_error<=0;end
   end
   // Decor shadow registers may be written at any time; a frame uses the copy latched at its claim.
   if(reg_write && reg_addr==12'h384)begin
    if(reg_wdata[31:1]!=0)decor_error<=1;else begin configured_decor_en<=reg_wdata[0];decor_error<=0;end
   end
   if(reg_write && ((reg_addr>=12'h388 && reg_addr<=12'h3a4) || (reg_addr>=12'h3b0 && reg_addr<=12'h3cc)) && reg_addr[1:0]==0)begin
    if(reg_wdata[29:24]!=0 || (reg_wdata[31] && (dsx(reg_wdata)<-13'sd31 || dsx(reg_wdata)>13'sd1919 || dsy(reg_wdata)<-13'sd31 || dsy(reg_wdata)>13'sd1079)))decor_error<=1;
    else begin configured_decor[reg_addr>=12'h3b0 ? {1'b1,reg_addr[4:2]-3'd4}:{1'b0,reg_addr[4:2]-3'd2}]<=reg_wdata;decor_error<=0;end
   end
   if(qt_active)begin qt_step<=qt_step+1'b1;if(qt_step==3'd7)qt_active<=0;end
   if(lf_ar && arready)begin arvalid<=0;lf_ar<=0;end
   if(lf_skid_full)begin
    lf_skid_full<=0;
    if(lf_count==7'd60 && !lf_second)begin
     lf_active<=0;
     if(!aborting)begin if(lf_row[0])begin lb_valid[1]<=1;lb_tag1<=lf_row;end else begin lb_valid[0]<=1;lb_tag0<=lf_row;end end
    end
   end
   if(lf_beat)begin
    lf_skid<=rdata[127:64];lf_skid_full<=1;lf_skid_word<=lf_count[5:0];lf_count<=lf_count+1'b1;lf_left<=lf_left-1'b1;
    if(rresp!=0 || rid!=0 || (rlast!=(lf_left==7'd1)))if(error_code==0)error_code<=2;
    if(rlast && lf_second)begin
     lf_second<=0;
     if(aborting)lf_count<=7'd60; // abandon the second burst; the accepted first burst is fully consumed
     else begin araddr<=lf_second_addr;arlen<=lf_second_beats-1'b1;arvalid<=1;lf_ar<=1;lf_left<=lf_second_beats;end
    end
   end
   if(reg_write && reg_addr==12'h284)begin
    if(state!=IDLE)player_error<=2;
    else if(reg_wdata[30:22]!=0 || reg_wdata[10:0]>1920-`PLAYER_WIDTH ||
       reg_wdata[21:11]>1080-`PLAYER_HEIGHT || (reg_wdata[31] && !multi_mode))player_error<=1;
    else begin configured_player<=reg_wdata;player_error<=0;end
   end
   if(cancel_now && owner_active) cancel_pending<=1;
   if(display_error && owner_active && error_code==0) error_code<=5;
   // The writer reports exactly one done/error per accepted group.
   if(write_inflight && ddr_done)begin committed<=committed+inflight_bytes;write_inflight<=0;end
   if(write_inflight && ddr_error)begin write_inflight<=0;if(error_code==0)error_code<=3;end
   if(reading && rvalid && rready && (rresp!=0 || rid!=0 || (rlast!=(received==arlen))))
     if(error_code==0) error_code<=2;
   case(state)
    IDLE: if(reg_write && reg_addr==12'h164 && reg_wdata==1) begin
     if(owner_grant && ddr_idle && fifo_empty && (!multi_mode || pair_ready) &&
          (back_base==32'h01000000 || back_base==32'h01400000)) begin
      owner_active<=1;state<=RESET;reset_count<=15;fifo_reset<=1;
      row<=0;column<=0;src<=32'h02000000;dst<=back_base;latched_back<=back_base;
      group_addr<=back_base;group_beats<=0;
      latched_y<=configured_y;latched_multi<=multi_mode;latched_bullets<=bullet_mode;latched_player_bullets<=player_bullet_mode;latched_effects<=effect_mode;latched_missiles<=missile_mode;
      object_mask_cache_valid<=0;obj_bullet<=0;obj_effect<=0;
      latched_player<=configured_player;player_frame_valid<=1;obj_player<=0;
      latched_key<=configured_key;key_frame_valid<=1;
      latched_background<=configured_background || (configured_i4 && !multi_mode);latched_i4<=configured_i4 && multi_mode;background_frame_valid<=1;pal_done<=0;
      latched_decor_en<=configured_decor_en && multi_mode;decor_frame_valid<=1;decor_rows<=0;decor_pending<=0;obj_decor<=0;latched_menu<=configured_menu;
      for(integer k=0;k<16;k=k+1)latched_decor[k]<=configured_decor[k];
      lf_active<=0;lf_ar<=0;lf_second<=0;lf_skid_full<=0;qt_active<=0;lb_valid<=0;
      bg_red<=1;bg_green<=5;bg_blue<=10;bg_er<=0;bg_eg<=0;bg_eb<=0;
      committed<=0;error_code<=0;cancel_pending<=0;shown<=0;start_generation<=generation;write_inflight<=0;
      start_missile<=0;start_enemy<=0;start_bullet<=0;start_player_bullet<=0;start_effect<=0;cache_valid<=0;
     end // Unavailable grant is retryable; no partial ownership.
    end
    RESET: if(reset_count!=0) begin fifo_reset<=1;reset_count<=reset_count-1'b1;end
     else if(ddr_idle && fifo_empty) begin
     if(latched_multi && latched_i4 && !pal_done)begin // BGT-A: 2 beats = 16 RGB565LE entries
      araddr<=latched_menu?`MENU_PALETTE_BASE:`BG_PALETTE_BASE;arlen<=1;arvalid<=1;received<=0;state<=PAL_AR;
     end else if(latched_multi)begin
      if(latched_bullets || latched_player_bullets || latched_missiles)begin
       cache_material<=0;cache_chunk<=0;cache_stage<=0;received<=0;block_beats<=4;
       araddr<=fixed_bullet_mask(0);arlen<=3;arvalid<=1;state<=CACHE_AR;
      end else state<=ROW_INIT;
     end else begin
      block_beats<=bounded_beats(src,column);
      // SINGLE has no table/cache latency to cover the FIFO's synchronized
      // reset release. Both FIFO clocks are core_clk; allow its 6-stage ready
      // handshake to settle before a generated beat can be acknowledged.
      if(latched_background)reset_count<=8;
      araddr<=src;arlen<=bounded_beats(src,column)-1'b1;arvalid<=!latched_background;received<=0;state<=latched_background?BG_GEN:BG_AR;
     end end
    // Bullet sprite cache: per material, 4 mask beats into pixels[0..3], then four
    // 16-beat colour chunks into pixels[16..31], each expanded to 128 cache pixels.
    PAL_AR: if(arready) begin arvalid<=0;state<=PAL_R;end
    PAL_R: if(rvalid && rready) begin
      if(received<63) received<=received+1'b1;
      for(integer k=0;k<8;k=k+1)pal[{received[0],3'd0}+k]<=rdata[k*16+:16];
      if(rlast) begin if(aborting) state<=DRAIN; else begin pal_done<=1;state<=RESET;end end
     end
    CACHE_AR: if(arready) begin arvalid<=0;state<=CACHE_R;end
    CACHE_R: if(rvalid && rready) begin
      if(received<63) received<=received+1'b1;
      if(rlast) begin
       if(aborting) state<=DRAIN; // consume the entire accepted burst before release
       else if(!cache_stage)begin
        cache_stage<=1;cache_chunk<=0;received<=0;block_beats<=16;
        araddr<=fixed_bullet_color({30'b0,cache_material});arlen<=15;arvalid<=1;state<=CACHE_AR;
       end else begin fill_row<=0;fill_x<=0;fill_phase<=0;index<={3'd0,cache_chunk};state<=CACHE_FILL;end
      end
     end
    // Per row of the chunk: one cycle latches the row's 16 mask bits from mask
    // beat <chunk> (pixels[0..3]), then eight cycles write two {mask,colour}
    // pixels from colour beats pixels[16..31] into the cache.
    CACHE_FILL: if(aborting)state<=DRAIN;else if(!fill_phase)begin
      fill_mask_row<={fill_mask_lane[7:0],fill_mask_lane[15:8]};
      fill_phase<=1;index<={2'b01,fill_row,1'b0};
    end else begin
      fill_x<=fill_x+4'd2;
      if(fill_x==4'd6)index<={2'b01,fill_row,1'b1};
      if(fill_x==4'd14)begin
       fill_phase<=0;index<={3'd0,cache_chunk};
       if(fill_row!=3'd7)fill_row<=fill_row+1'b1;
       else if(cache_chunk!=2'd3)begin
        cache_chunk<=cache_chunk+1'b1;received<=0;block_beats<=16;
        araddr<=fixed_bullet_color({30'b0,cache_material})+{22'd0,cache_chunk+2'd1,8'd0};arlen<=15;arvalid<=1;state<=CACHE_AR;
       end else if(cache_material==(latched_missiles?2'd3:2'd1))begin cache_valid<=1;state<=ROW_INIT;end
       else begin
        cache_material<=cache_material+1'b1;cache_stage<=0;received<=0;block_beats<=4;
        araddr<=fixed_bullet_mask({30'b0,cache_material+2'd1});arlen<=3;arvalid<=1;state<=CACHE_AR;
       end
      end
    end
    BG_GEN: if(aborting)state<=DRAIN;
     else if(reset_count!=0)reset_count<=reset_count-1'b1;
     else if(r1_skip)begin // R1: lanes stay unwritten; consumers substitute the gradient / I4 background
      received<=block_beats;index<=0;row_cursor<=hl_first_cursor;row_kind<=hl_first_kind;hl_pos<=0;hl_done<=hl_mode && hl_n==0;decor_pending<=decor_block_hits;state<=decor_block_hit?DECOR_CHECK:OBJ_WAIT;
     end else if(gen_fire)begin
      received<=received+(direct_block?2:1);
      if(received+(direct_block?2:1)>=block_beats)begin
       index<=0;row_cursor<=hl_first_cursor;row_kind<=hl_first_kind;hl_pos<=0;hl_done<=hl_mode && hl_n==0;decor_pending<=decor_block_hits;
       state<=direct_block?BLOCK_DONE:(latched_multi?(decor_block_hit?DECOR_CHECK:OBJ_WAIT):PIXEL);
      end
     end
    BG_AR: if(arready) begin arvalid<=0;state<=BG_R;end
    BG_R: if(rvalid && rready) begin
     if(received<63) received<=received+1'b1;
     if(rlast && received>=block_beats-1'b1) begin
      index<=0;row_cursor<=hl_first_cursor;row_kind<=hl_first_kind;hl_pos<=0;hl_done<=hl_mode && hl_n==0;decor_pending<=decor_block_hits;
      state<=direct_block?BLOCK_DONE:(latched_multi?(decor_block_hit?DECOR_CHECK:OBJ_WAIT):PIXEL);end
    end
    PIXEL: if(aborting) state<=DRAIN;
     else if(in_enemy) begin
      araddr<=32'h02400000+{14'd0,enemy_row,7'd0}+{25'd0,enemy_column,4'd0};
      arlen<=0;arvalid<=1;received<=0;state<=COLOR_AR;
     end else if(index==block_beats-1'b1) begin index<=0;state<=PUSH;end
     else index<=index+1'b1;
    COLOR_AR: if(arready) begin
     arvalid<=0;state<=COLOR_R;
     if(latched_multi && OPTIMIZE_SPRITES)multi_read_pending<=1;
    end
    COLOR_R: if(rvalid) begin
     color<=reverse_pixels(rdata);
     if(latched_multi && OPTIMIZE_SPRITES)begin
      if(received<63)received<=received+1'b1;
      if(rlast)multi_read_pending<=0;
      if(aborting || rresp!=0 || rid!=0 || (rlast!=(received==arlen)))begin
       if(rlast)state<=DRAIN; // consume the entire accepted burst before release
      end else begin
       object_mask<=object_mask_cache[object_mask_offset[3:0]*8+:8];
       object_alpha<=beat_alpha;
       state<=!obj_effect?PASTE0:(alpha_all_zero?OBJ_NEXT:(alpha_binary?PASTE0:BLEND_LOAD));
      end
     end else if(rlast) begin
      if(aborting || rresp!=0 || rid!=0) state<=DRAIN;
      else begin araddr<=latched_multi?object_mask_address:(32'h02402000+{18'd0,enemy_row[10:1],4'd0});
       arlen<=0;arvalid<=1;received<=0;state<=MASK_AR;end
     end else if(received<63) received<=received+1'b1;
    end
    MASK_AR: if(arready) begin arvalid<=0;state<=MASK_R;end
    MASK_R: if(rvalid) begin
     if(rlast) begin
     if(latched_multi && OPTIMIZE_SPRITES)begin
      if(aborting || rresp!=0 || rid!=0 || received!=0)state<=DRAIN;
      else begin
       object_mask_cache<=rdata;object_mask_cache_tag<=object_mask_address;object_mask_cache_valid<=1;
       if(obj_effect && rdata==128'd0)begin
        source_beat<=skip_to_beat;fragment_last_beat<=skip_to_beat;state<=OBJ_NEXT;
       end else begin
       araddr<=object_color_address;arlen<=fragment_beats-1'b1;
       fragment_last_beat<=source_beat+fragment_beats-1'b1;
       arvalid<=1;received<=0;state<=COLOR_AR;
       end
      end
     end else if(latched_multi)begin object_mask<=rdata[object_mask_offset[3:0]*8+:8];state<=PASTE0;end
     else begin
      if(index==block_beats-1'b1) begin index<=0;state<=PUSH;end
      else begin index<=index+1'b1;state<=PIXEL;end
     end end else if(received<63) received<=received+1'b1;
    end
    PUSH: if(aborting) state<=DRAIN;
     else if(!q8_blocked) begin
      if(index==block_beats-1'b1) begin index<=0;state<=BLOCK_DONE;end else index<=index+1'b1;
     end
    // All data for this block has entered the FIFO. No AW is issued until
    // the entire group is queued. DRAIN may discard a never-issued group;
    // after START, ACCEPT/RESPONSE still wait for the writer's final B.
    BLOCK_DONE: if(aborting)state<=DRAIN;
     else if(q8_out_valid && fifo_full)state<=BLOCK_DONE;
     else if(group_total==0 || group_total>128)begin error_code<=4;state<=DRAIN;end
     else begin
      if(group_beats==0)group_addr<=dst;
      group_beats<=group_total[7:0];
      if(group_total==128 || end_column==240 || next_dst[11:0]==0)state<=START;
      else state<=NEXT;
     end
    // The writer accepts one group at a time: wait for the previous group's
    // response before starting the next. Its data is already queued in order.
    START: if(aborting) state<=DRAIN;
     else if(!write_inflight && ddr_idle)begin wr_start<=1;state<=ACCEPT;end
    // A pulsed start has a synchronization delay. Never skip ACCEPT on EXIT.
    ACCEPT: if(ddr_accept)begin
     write_inflight<=1;inflight_bytes<=group_bytes[11:0];group_beats<=0;state<=NEXT;
    end
    RESPONSE: state<=NEXT; // unused since write/compose overlap
    NEXT: if(aborting) state<=DRAIN;
     else begin
      if(row==1079 && end_column==240) begin
       if(!write_inflight && ddr_idle && group_beats==0 && fifo_empty && committed==2073600 && !q8_half_valid && !q8_out_valid)begin render_done<=1;state<=PRESENT;end
      end
      else begin
       dst<=dst+block_bytes;src<=next_src;
       column<=next_column;if(end_column==240)row<=row+1'b1;
       if(latched_background && end_column==240)begin
        if(row==511)begin
         bg_red<=4;bg_green<=4;bg_blue<=12;bg_er<=0;bg_eg<=0;bg_eb<=0;
        end else begin
         if(bg_ur>=bg_period)begin bg_er<=bg_ur-bg_period;bg_red<=bg_red+1'b1;end else bg_er<=bg_ur;
         if(bg_ug>=bg_period)begin bg_eg<=bg_ug-bg_period;bg_green<=bg_green-1'b1;end else bg_eg<=bg_ug;
         if(bg_ub>=bg_period)begin bg_eb<=bg_ub-bg_period;bg_blue<=bg_lower?bg_blue-1'b1:bg_blue+1'b1;end else bg_eb<=bg_ub;
        end
       end
       if(latched_multi)state<=end_column==240?ROW_INIT:BLOCK_SETUP;
       else begin
        block_beats<=bounded_beats(next_src,next_column);
        araddr<=next_src;arlen<=bounded_beats(next_src,next_column)-1'b1;arvalid<=!latched_background;received<=0;state<=latched_background?BG_GEN:BG_AR;
       end
      end
     end
    // Per row: scan each enabled table from its start pointer, in draw order,
    // two cycles per entry (word0 then word1 addresses are issued back to back).
    ROW_INIT: if(aborting)state<=DRAIN;else begin
      decor_rows<=decor_rows_next;for(integer k=0;k<16;k=k+1)decor_rel[k]<=decor_rel_next[k];
      if(latched_i4)begin qt_active<=1;qt_step<=0;end
      if(lf_want)begin
       araddr<=lf_row_addr;arlen<=lf_first_beats-1'b1;arvalid<=1;lf_ar<=1;lf_active<=1;lf_count<=0;lf_left<=lf_first_beats;lf_row<=lf_target;
       lf_second<=lf_first_beats!=7'd60;lf_second_addr<=lf_row_addr+{21'd0,lf_first_beats,4'd0};lf_second_beats<=7'd60-lf_first_beats;
       if(lf_target[0])lb_valid[1]<=0;else lb_valid[0]<=0;
      end
      row_count<=0;bullet_row_count<=0;effect_row_count<=0;row_kind<=0;
      scan_bullet<=0;scan_missile<=0;scan_player_bullet<=0;scan_effect<=0;
      scan_index<=start_enemy;scan_word<=0;scan_valid<=0;scan_all_expired<=1;
      state<={{(32-SIW){1'b0}},start_enemy}<table_count?SCAN_W:SCAN_TABLE;
    end
    // SCAN_W: word0 address of the first window entry is being presented.
    SCAN_W: if(aborting)state<=DRAIN;else begin scan_word<=1;state<=SCAN_B;end
    // SCAN_B: word1 address presented; table_data holds word0 of scan_index.
    SCAN_B: if(aborting)state<=DRAIN;else begin scan_d0<=table_data;scan_index<=scan_index+1'b1;scan_word<=0;scan_valid<=1;state<=SCAN_A;end
    // SCAN_A: table_data holds word1 of entry scan_index-1; evaluate while the
    // next entry's word0 address is already being presented.
    SCAN_A: if(aborting)state<=DRAIN;else begin
      scan_valid<=0;
      if(row_member)begin
       if(scan_effect)effect_row_count<=effect_row_count+1'b1;
       else if(scan_bullet)bullet_row_count<=bullet_row_count+1'b1;
       else row_count<=row_count+1'b1;
      end
      if(row_expired && scan_all_expired)begin
       if(scan_effect)start_effect<=scan_index;
       else if(scan_player_bullet)start_player_bullet<=scan_index;
       else if(scan_missile)start_missile<=scan_index;
       else if(scan_bullet)start_bullet<=scan_index;
       else start_enemy<=scan_index;
      end else scan_all_expired<=0;
      scan_word<=1;
      state<=(row_beyond || scan_last_entry)?SCAN_TABLE:SCAN_B;
    end
    SCAN_TABLE: if(aborting)state<=DRAIN;else begin
      scan_all_expired<=1;scan_word<=0;scan_valid<=0;
      if(!scan_bullet)begin
       scan_bullet<=1;scan_index<=start_bullet;
       state<=latched_bullets && {{(32-SIW){1'b0}},start_bullet}<bullet_count?SCAN_W:SCAN_TABLE;
      end else if(!scan_missile && latched_missiles)begin
       scan_missile<=1;scan_index<=start_missile;
       state<={{(32-SIW){1'b0}},start_missile}<missile_count?SCAN_W:SCAN_TABLE;
      end else if(!scan_player_bullet)begin
       scan_player_bullet<=1;scan_index<=start_player_bullet;
       state<=latched_player_bullets && {{(32-SIW){1'b0}},start_player_bullet}<player_bullet_count?SCAN_W:SCAN_TABLE;
      end else if(!scan_effect)begin
       scan_effect<=1;scan_index<=start_effect;
       state<=latched_effects && {{(32-SIW){1'b0}},start_effect}<effect_count?SCAN_W:SCAN_TABLE;
      end else state<=BLOCK_SETUP;
    end
    BLOCK_SETUP: if(aborting)state<=DRAIN;else if(lf_want && row_ready)begin // row y complete: start prefetching y+1 first
      araddr<=lf_row_addr;arlen<=lf_first_beats-1'b1;arvalid<=1;lf_ar<=1;lf_active<=1;lf_count<=0;lf_left<=lf_first_beats;lf_row<=lf_target;
      lf_second<=lf_first_beats!=7'd60;lf_second_addr<=lf_row_addr+{21'd0,lf_first_beats,4'd0};lf_second_beats<=7'd60-lf_first_beats;
      if(lf_target[0])lb_valid[1]<=0;else lb_valid[0]<=0;
     end else if((!latched_i4 || row_ready) && !qt_active)begin
      member_index<=0;block_beats<=bounded_beats(src,column);row_cursor<=0;row_kind<=0;block_has_objects<=0;hl_n<=0;hl_over<=0;state<=BLOCK_WAIT;
    end
    // Streaming block check: row_entry lags row_cursor by one cycle.
    BLOCK_WAIT: if(aborting)state<=DRAIN;else begin row_cursor<=1;state<=BLOCK_CHECK;end
    BLOCK_CHECK: if(aborting)state<=DRAIN;else begin
      if(selected_row_count!=0 && candidate_hit)begin
       block_has_objects<=1;
       if(hl_n==HL_DEPTH)hl_over<=1'b1;
       else begin hl_kind[hl_n[3:0]]<=row_kind;hl_idx[hl_n[3:0]]<=row_cursor-1'b1;hl_n<=hl_n+1'b1;end
      end
      if(selected_row_count==0 || row_cursor>=selected_row_count)begin
       if(row_kind==0 && bullet_row_count!=0)begin row_kind<=1;row_cursor<=0;state<=BLOCK_WAIT;end
       else if(row_kind<=1 && effect_row_count!=0)begin row_kind<=2;row_cursor<=0;state<=BLOCK_WAIT;end
       else begin
        if(player_hit || decor_block_hit)block_has_objects<=1;
        row_kind<=0;received<=0;
        // R2: BG_GEN modes must not touch the AR channel here: an A2 row prefetch AR may be pending (never withdraw it).
        if(!latched_background && !latched_i4)begin araddr<=src;arlen<=block_beats-1'b1;arvalid<=1;end
        state<=(latched_background || latched_i4)?BG_GEN:BG_AR;
       end
      end else row_cursor<=row_cursor+1'b1;
    end
    DECOR_CHECK: if(aborting)state<=DRAIN;else if(decor_pending==0)begin row_kind<=hl_first_kind;row_cursor<=hl_first_cursor;hl_pos<=0;hl_done<=hl_mode && hl_n==0;state<=OBJ_WAIT;end
     else begin
      decor_pending[ds]<=1'b0;
      obj_player<=0;obj_bullet<=0;obj_effect<=1;obj_decor<=1;obj_skin<=dw[30];obj_pb<=0;obj_missile<=0;
      obj_x<=dxs[11:0];obj_row<={2'b0,decor_rel[ds]};obj_material<=0;obj_frame<=0;
      source_beat<=dsrc_left[7:3];last_source_beat<=dsrc_last[7:3];state<=OBJ_FETCH;
     end
    OBJ_WAIT: state<=aborting?DRAIN:OBJ_CHECK;
    OBJ_CHECK: if(aborting)state<=DRAIN;else if(obj_list_end)begin
      if(!hl_mode && row_kind==0 && bullet_row_count!=0)begin row_kind<=1;row_cursor<=0;state<=OBJ_WAIT;end
      else if(!hl_mode && row_kind<=1 && effect_row_count!=0)begin row_kind<=2;row_cursor<=0;state<=OBJ_WAIT;end
      else if(player_hit)begin
       obj_player<=1;obj_bullet<=0;obj_effect<=0;obj_decor<=0;obj_pb<=0;obj_missile<=0;obj_x<={1'b0,latched_player[10:0]};
       obj_row<=row-latched_player[21:11];obj_material<=0;
       source_beat<=player_source_left[7:3];last_source_beat<=player_source_last[7:3];state<=OBJ_FETCH;
      end else begin index<=0;state<=PUSH;end
     end
     else if(candidate_hit)begin
      obj_player<=0;obj_bullet<=row_kind==1;obj_effect<=row_kind==2;obj_decor<=0;obj_pb<=candidate_pb;obj_missile<=candidate_missile;obj_x<=candidate_x[11:0];obj_row<=candidate_row;obj_material<=candidate_material;obj_frame<=candidate_frame;
      source_beat<=source_left[7:3];last_source_beat<=source_last[7:3];
      state<=(row_kind==1 && cache_valid)?CACHE_READ:OBJ_FETCH;cache_step<=0;
     end else if(another_member)begin member_index<=next_member;state<=OBJ_CHECK;end
     else begin member_index<=0;state<=OBJ_WAIT;
      if(!hl_mode)row_cursor<=row_cursor+1'b1; // GHD5: hit-list walk visits only the recorded entries
      else if(hl_pos+1'b1<hl_n)begin hl_pos<=hl_pos+1'b1;row_kind<=hl_kind[hl_next];row_cursor<=hl_idx[hl_next];end
      else hl_done<=1'b1;
     end
    // Counterfactual cached beat: four paired reads, then unchanged paste path.
    CACHE_READ: if(aborting)state<=DRAIN;else begin
      if(cache_step!=0)begin
       color[(4-cache_step)*32 +: 32]<={bullet_cache_rd[32:17],bullet_cache_rd[15:0]};
       object_mask[(4-cache_step)*2 +: 2]<={bullet_cache_rd[33],bullet_cache_rd[16]};
      end
      if(cache_step==4)state<=PASTE0;
      cache_step<=cache_step+1'b1;
    end
    OBJ_FETCH: if(aborting)state<=DRAIN;else if(!lf_active)begin // A2: the row prefetch owns the read path until done
      if(OPTIMIZE_SPRITES && object_mask_cache_valid && object_mask_cache_tag==object_mask_address && obj_effect && object_mask_cache==128'd0)begin
       source_beat<=skip_to_beat;fragment_last_beat<=skip_to_beat;state<=OBJ_NEXT;
      end else begin
      if(OPTIMIZE_SPRITES && object_mask_cache_valid && object_mask_cache_tag==object_mask_address)begin
       araddr<=object_color_address;arlen<=fragment_beats-1'b1;state<=COLOR_AR;
       fragment_last_beat<=source_beat+fragment_beats-1'b1;
      end else begin araddr<=OPTIMIZE_SPRITES?object_mask_address:object_color_address;
       arlen<=0;state<=OPTIMIZE_SPRITES?MASK_AR:COLOR_AR;end
      arvalid<=1;received<=0;
      end
    end
    PASTE0: if(aborting)state<=multi_read_pending?COLOR_R:DRAIN;else begin
      state<=paste_shift==0?OBJ_NEXT:PASTE1;
    end
    PASTE1: if(aborting)state<=multi_read_pending?COLOR_R:DRAIN;else begin
      state<=OBJ_NEXT;
    end
    // Alpha blend: point the shared read port at the destination beat, then
    // blend and write back one lane per cycle (masked lanes only).
    BLEND_LOAD: if(aborting)state<=multi_read_pending?COLOR_R:DRAIN;else begin
      index<=paste_index[4:0];blend_lane<=0;state<=BLEND;
    end
    BLEND: if(aborting)state<=multi_read_pending?COLOR_R:DRAIN;else begin
      blend_lane<=blend_lane+1'b1;
      if(blend_lane==3'd7)begin
       if(!blend_second && paste_shift!=0)begin blend_second<=1;state<=BLEND_LOAD;end
       else begin blend_second<=0;state<=OBJ_NEXT;end
      end
    end
    OBJ_NEXT: if(aborting)state<=multi_read_pending?COLOR_R:DRAIN;else if(source_beat<last_source_beat)begin
      source_beat<=source_beat+1'b1;cache_step<=0;
      state<=(obj_bullet && cache_valid)?CACHE_READ:((OPTIMIZE_SPRITES && source_beat<fragment_last_beat)?COLOR_R:OBJ_FETCH);
     end else if(obj_player)begin index<=0;state<=PUSH;end
     else if(obj_decor)begin obj_decor<=0;obj_effect<=0;state<=DECOR_CHECK;end
     else if(another_member)begin member_index<=next_member;state<=OBJ_CHECK;end
     else begin member_index<=0;state<=OBJ_WAIT;
      if(!hl_mode)row_cursor<=row_cursor+1'b1; // GHD5: hit-list walk visits only the recorded entries
      else if(hl_pos+1'b1<hl_n)begin hl_pos<=hl_pos+1'b1;row_kind<=hl_kind[hl_next];row_cursor<=hl_idx[hl_next];end
      else hl_done<=1'b1;
     end
    // render_done has already registered with surface manager: drain its
    // present handshake even if EXIT arrives now; never retract a commit.
    PRESENT: if(present_ack && generation!=start_generation && display_ok) begin
     shown<=1;state<=SHOWN;
    end
    SHOWN: if(aborting) state<=DRAIN;
    DRAIN: if(ddr_idle && !arvalid && !lf_active) begin
     fifo_reset<=1;reset_count<=15;state<=CLEAN;
    end
    CLEAN: if(reset_count!=0) begin fifo_reset<=1;reset_count<=reset_count-1'b1;end
     else if(ddr_idle && fifo_empty) begin owner_active<=0;state<=IDLE;end
    default: begin error_code<=4;state<=DRAIN;end
   endcase
  end
 end
endmodule

// 100 MHz core-clock, mutually exclusive cycle attribution. No control output.
// [0] compose total; [1..17] partition it; [18] present is a separate interval.
module enemy_perf_counters #(parameter [31:0] ABI=32'h45500100, parameter DETAIL=1) (
 input wire clk,rst_n,start,done,input wire [5:0] phase,
 input wire ar_selected,arready,rvalid,rready,fifo_full,awvalid,awready,
 input wire wvalid,wready,wlast,bvalid,bready,
 input wire [10:0] y,input wire [31:0] back,generation,
 input wire [11:0] addr,output reg [31:0] rdata
);
 reg [31:0] live[0:18],snap[0:18];
 reg [31:0] seq,snap_y,snap_back,snap_generation;
 reg b_pending,overflow,snap_overflow,valid;
 reg [4:0] bucket;
 integer k;
 always @* begin
  bucket=17; // RESET, pixel/FIFO work, command latency and writer bubbles
  if(phase==2) bucket=arready?3:(ar_selected?2:1);
  else if(phase==3) bucket=!rready?11:(rvalid?4:5);
  else if(phase==5 || phase==7 || phase==33) bucket=arready?8:(ar_selected?7:6);
  else if(phase==6 || phase==8 || phase==34) bucket=rvalid?9:10;
  else if(phase>=9 && phase<=13) begin
   if(phase==9 && fifo_full) bucket=11;
   else if(b_pending) bucket=(bvalid && bready)?16:15;
   else if(wvalid) bucket=wready?14:13;
   else if(awvalid && !awready) bucket=12;
  end
 end
 always @* begin
  rdata=0;
  case(addr)
   12'h184:rdata=ABI;
   12'h188:rdata=seq;
   12'h18c:rdata={30'd0,snap_overflow,valid};
   12'h1dc:rdata=snap_y;
   12'h1e0:rdata=snap_back;
   12'h1e4:rdata=snap_generation;
   default:if(addr>=12'h190 && addr<=12'h1d8 && addr[1:0]==0)
    rdata=snap[(addr-12'h190)>>2];
  endcase
 end
 always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin
   for(k=0;k<19;k=k+1)begin live[k]<=0;snap[k]<=0;end
   seq<=0;snap_y<=0;snap_back<=0;snap_generation<=0;
   b_pending<=0;overflow<=0;snap_overflow<=0;valid<=0;
  end else if(start) begin
   for(k=0;k<19;k=k+1)live[k]<=0;
   b_pending<=0;overflow<=0;
  end else begin
   if((phase>=1 && phase<=13) || phase>=18) begin
    live[0]<=live[0]+1'b1;if(DETAIL)live[bucket]<=live[bucket]+1'b1;
    if(live[0]==32'hffffffff || (DETAIL && live[bucket]==32'hffffffff))overflow<=1;
    if(wvalid && wready && wlast)b_pending<=1;
    if(bvalid && bready)b_pending<=0;
   end
   if(phase==14)begin
    live[18]<=live[18]+1'b1;
    if(live[18]==32'hffffffff)overflow<=1;
   end
   if(done)begin
    for(k=0;k<19;k=k+1)snap[k]<=live[k];
    // The completing PRESENT edge is part of the measured interval.
    snap[18]<=live[18]+1'b1;
    snap_overflow<=overflow || live[18]==32'hffffffff;
    snap_y<={21'd0,y};snap_back<=back;snap_generation<=generation;
    seq<=seq+1'b1;valid<=1;
   end
  end
 end
endmodule

`include "enemy_scene_table.v"
