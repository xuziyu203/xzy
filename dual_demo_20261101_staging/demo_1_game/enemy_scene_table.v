// Two immutable frame banks, one sequential APB staging stream. No pixel RAM.
// Four segments per bank: enemies, enemy bullets, player bullets, effects.
// V1 contract: every segment is staged in non-decreasing y order (signed for
// bullets); the compositor relies on this to scan a sorted window per row.
`include "enemy_config.vh"
module enemy_scene_table #(parameter CAPACITY=`ENEMY_CAPACITY, parameter BULLET_CAPACITY=`BULLET_CAPACITY,
 parameter PLAYER_BULLET_CAPACITY=`PLAYER_BULLET_CAPACITY, parameter EFFECT_CAPACITY=`EFFECT_CAPACITY, parameter MISSILE_CAPACITY=`MISSILE_CAPACITY,
 parameter BANK_WORDS_P=`SCENE_BANK_WORDS)(
 input wire clk,rst_n,input wire [11:0] addr,input wire write,
 input wire [31:0] wdata,output reg [31:0] rdata,
 input wire engine_idle,claim,
 output reg mode_multi,output reg ready,
 output wire [31:0] frame_count,frame_enabled,output reg [31:0] frame_seq,
 input wire [31:0] read_index,input wire [1:0] read_word,output reg [31:0] read_data,output wire [3:0] read_dir,
 input wire read_bullet,output reg bullet_mode,output reg bullet_ready,output wire pair_ready,
 output wire [31:0] bullet_count,output reg [31:0] bullet_seq,
 input wire read_player_bullet,output reg player_bullet_mode,output reg player_bullet_ready,
 output wire [31:0] player_bullet_count,output reg [31:0] player_bullet_seq,
 input wire read_effect,output reg effect_mode,output reg effect_ready,
 output wire [31:0] effect_count,output reg [31:0] effect_seq,
 input wire read_missile,output reg missile_mode,output reg missile_ready,output wire [31:0] missile_count,output reg [31:0] missile_seq
);
 `include "enemy_catalog.vh"
 localparam CW=$clog2(CAPACITY+1)>0?$clog2(CAPACITY+1):1;
 localparam BCW=$clog2(BULLET_CAPACITY+1),BULLET_OFFSET=CAPACITY;
 localparam PCW=$clog2(PLAYER_BULLET_CAPACITY+1),PLAYER_BULLET_OFFSET=CAPACITY+BULLET_CAPACITY;
 localparam FCW=$clog2(EFFECT_CAPACITY+1),EFFECT_OFFSET=CAPACITY+BULLET_CAPACITY+PLAYER_BULLET_CAPACITY;
 localparam MCW=$clog2(MISSILE_CAPACITY+1),MISSILE_OFFSET=EFFECT_OFFSET+EFFECT_CAPACITY;
 // Three ten-bit cells per entry, parity-striped across two SDP RAMs.
 // Non-FX: [x-low, metadata, y-low]; FX: [metadata0, coordinate, metadata1].
 // Each render request and each two-cell APB write uses adjacent cells, so
 // the two physical memories each need at most one write and one read.
 // 40/256/16/16: 984 logical cells/bank, stride1024, two 10x1024 RAMs.
 // 40/512/16/17/8 (BU5): 1779 logical cells/bank, stride2048, two 10x2048 RAMs.
 // GHD4: bank stride from the generator (`SCENE_BANK_WORDS = 1024 x ceil(cells/1024)); may be a non power of two (e.g. 3072 =
 // three RAM10 per parity memory). Parity striping is unchanged: the stride is even, so bank 1 keeps the cell parity.
 localparam BANK_WORDS=BANK_WORDS_P,BW=$clog2(BANK_WORDS),MW=BW+1;
 initial if(BANK_WORDS<3*(MISSILE_OFFSET+MISSILE_CAPACITY) || BANK_WORDS%2!=0)$fatal(1,"scene table bank stride too small or odd");
 // GHD4: each parity memory = NB explicit 1024 x 10 sub-arrays (one RAM10 each); see the generate block below.
 localparam NB=BANK_WORDS/1024;
 initial if(BANK_WORDS%1024!=0)$fatal(1,"scene table bank stride must be a multiple of 1024");
 // ---- enemies ----
 reg active_bank,stage_bank,staging,frame_valid;
 reg [CW-1:0] count_shadow,written,enabled_shadow,live_count,live_enabled;
 reg [1:0] word_index;
 reg [31:0] sequence_shadow,error,d0,d1,d2;
 reg [10:0] last_y;
 wire [31:0] material_size=fixed_material_size(d1[7:0]);
 wire entry_ok=d1[31:9]==0 && d1[7:0]<4 &&
     d2[15:0]==material_size && d2[31:16]==material_size &&
     ({16'd0,d0[15:0]}+material_size<=1920) &&
     ({16'd0,d0[31:16]}+material_size<=1080) &&
     (written==0 || d0[26:16]>=last_y);
 wire data_address=addr>=12'h214 && addr<=12'h220 && addr[1:0]==0;
 wire accept_word=write && data_address && staging && !ready && error==0 &&
     written<count_shadow && written<CAPACITY && addr==12'h214+{8'd0,word_index,2'd0};
 // ---- enemy bullets ----
 reg b_active_bank,b_stage_bank,b_staging,b_frame_valid,b_phase;
 reg [BCW-1:0] b_count_shadow,b_written,b_live_count;
 reg [31:0] b_sequence_shadow,b_error;
 reg [15:0] b_d0;
 reg b_d0_valid;
 reg [2:0] b_member_mask;reg b_layout;reg [3:0] b_dir; // GHD5R: triple direction (word0[23:20])
 reg signed [16:0] b_last_y;
 wire signed [16:0] bx=$signed({b_d0[15],b_d0[15:0]});
 wire signed [16:0] by=$signed({wdata[15],wdata[15:0]});
 // GHD5: styles of BULLET_TRIPLE_STYLE_MASK may use the triple layout; the alt style (round disks) has its own span
 wire [3:0] b_tri_styles=`BULLET_TRIPLE_STYLE_MASK;
 wire b_tri_ok=b_tri_styles[wdata[17:16]];
 wire b_alt=wdata[17:16]==`BULLET_TRIPLE_ALT_STYLE;
 wire b_entry_ok=b_d0_valid && wdata[31:28]==0 &&
      bx>=-(b_layout?(17'sd`BULLET_TRI_ROT_WMAX-17'sd1):17'sd27) && bx<=17'sd1919 &&   // GHD5R: rotated bbox, all directions
      by>=-(b_layout?(17'sd`BULLET_TRI_ROT_HMAX-17'sd1):17'sd11) && by<=17'sd1079 &&
      (b_layout?b_tri_ok:!b_member_mask[2]) && {22'd0,wdata[27:18]}=={{(32-BCW){1'b0}},b_written} &&
      (b_written==0 || by>=b_last_y);
 wire b_data_address=addr==12'h254 || addr==12'h258;
 // GHD5R direction RAM: {bank, rank} -> 4-bit direction of a triple group; written with the entry, read beside the cells
 (* syn_ramstyle="block_ram" *) reg [3:0] dir_mem[0:2047];
 reg [3:0] dir_q;reg dir_qv;
 wire b_accept_word=write && b_data_address && b_staging && !bullet_ready && b_error==0 &&
      b_written<b_count_shadow && b_written<BULLET_CAPACITY && addr==(b_phase?12'h258:12'h254);
 // ---- player bullets ----
 reg p_active_bank,p_stage_bank,p_staging,p_frame_valid,p_phase,p_d0_valid;
 reg [PCW-1:0] p_count_shadow,p_written,p_live_count;
 reg [31:0] p_sequence_shadow;
 reg [2:0] p_error;
 reg [15:0] p_d0;
 reg signed [16:0] p_last_y;
 wire signed [16:0] px=$signed({p_d0[15],p_d0});
 wire p_entry_ok=p_d0_valid && wdata[31:21]==0 && wdata[16] &&
      px>=-17'sd15 && px<=17'sd1919 && by>=-17'sd31 && by<=17'sd1079 &&
      (p_written==0 || by>=p_last_y);
 wire p_data_address=addr==12'h2a4 || addr==12'h2a8;
 wire p_accept_word=write && p_data_address && p_staging && !player_bullet_ready && p_error==0 &&
      p_written<p_count_shadow && p_written<PLAYER_BULLET_CAPACITY && addr==(p_phase?12'h2a8:12'h2a4);
 // ---- missiles ----
 reg m_active_bank,m_stage_bank,m_staging,m_frame_valid,m_phase,m_d0_valid;
 reg [MCW-1:0] m_count_shadow,m_written,m_live_count;
 reg [31:0] m_sequence_shadow;
 reg [2:0] m_error;
 reg [15:0] m_d0;reg m_type;
 reg signed [16:0] m_last_y;
 wire signed [16:0] mx=$signed({m_d0[15],m_d0});
 wire m_entry_ok=m_d0_valid && wdata[31:19]==0 && wdata[18:16]==m_written &&
      mx>=-(m_type?17'sd11:17'sd7) && mx<=17'sd1919 && by>=-17'sd31 && by<=17'sd1079 &&
      (m_written==0 || by>=m_last_y);
 wire m_data_address=addr==12'h354 || addr==12'h358;
 wire m_accept_word=write && m_data_address && m_staging && !missile_ready && m_error==0 &&
      m_written<m_count_shadow && m_written<MISSILE_CAPACITY && addr==(m_phase?12'h358:12'h354);
 // ---- effects: data0={type[15],frame[14:12],x[11:0]} x multiple of 8; data1={y[11:0]} ----
 reg f_active_bank,f_stage_bank,f_staging,f_frame_valid,f_phase,f_d0_valid;
 reg [FCW-1:0] f_count_shadow,f_written,f_live_count;
 reg [31:0] f_sequence_shadow;
 reg [2:0] f_error;
 reg [16:0] f_d0;
 reg signed [11:0] f_last_y;
 wire [31:0] f_size=fixed_effect_size({30'd0,f_d0[16:15]});
 wire [31:0] f_frames=fixed_effect_frames({30'd0,f_d0[16:15]});
 wire signed [12:0] f_x=$signed({f_d0[11],f_d0[11:0]});
 wire signed [12:0] f_y=$signed({wdata[11],wdata[11:0]});
 wire f_clip=f_d0[16:15]==2'd3;
 wire f_bounds=f_clip ? (f_x<1920 && f_x+13'sd128>0 && f_y<1080 && f_y+13'sd128>0) :
      (f_d0[2:0]==0 && {20'd0,f_d0[11:0]}+f_size<=1920 && {20'd0,wdata[11:0]}+f_size<=1080);
 wire f_entry_ok=f_d0_valid && wdata[31:12]==0 && {29'd0,f_d0[14:12]}<f_frames && f_bounds &&
      (f_written==0 || f_y>=$signed({f_last_y[11],f_last_y}));
 wire f_data_address=addr==12'h2dc || addr==12'h2e0;
 wire f_accept_word=write && f_data_address && f_staging && !effect_ready && f_error==0 &&
      f_written<f_count_shadow && f_written<EFFECT_CAPACITY && addr==(f_phase?12'h2e0:12'h2dc);
 assign pair_ready=ready && error==0 &&
      (!bullet_mode || (mode_multi && bullet_ready && b_error==0 && sequence_shadow==b_sequence_shadow)) &&
      (!player_bullet_mode || (mode_multi && player_bullet_ready && p_error==0 && sequence_shadow==p_sequence_shadow)) &&
      (!effect_mode || (mode_multi && effect_ready && f_error==0 && sequence_shadow==f_sequence_shadow)) &&
      (!missile_mode || (mode_multi && missile_ready && m_error==0 && sequence_shadow==m_sequence_shadow));
 wire accepted_claim=claim && mode_multi && pair_ready;
 assign missile_count=m_live_count;
 assign bullet_count=b_live_count;assign player_bullet_count=p_live_count;assign effect_count=f_live_count;
 wire select_missile=read_missile===1'b1;
 wire select_bullet=read_bullet===1'b1;
 wire select_player_bullet=read_player_bullet===1'b1;
 wire select_effect=read_effect===1'b1;
 wire [31:0] selected_live=select_missile?m_live_count:(select_effect?f_live_count:(select_player_bullet?p_live_count:(select_bullet?b_live_count:live_count)));
 assign frame_count=live_count;assign frame_enabled=live_enabled;
 // BU2.1 adds two mask bits to the existing metadata cells; no memory growth. The redundant
 // transmitted sort-slot is validated but is not part of the rendering payload.
 // Each accepted APB word writes at most once to each RAM, including interleaved
 // segments; seal/claim therefore never waits for a hidden write to drain.
 wire nonfx_write=(accept_word && !word_index[1]) || b_accept_word || p_accept_word || m_accept_word;
 wire second=m_accept_word?m_phase:(p_accept_word?p_phase:(b_accept_word?b_phase:word_index[0]));
 wire wb=m_accept_word?m_stage_bank:(p_accept_word?p_stage_bank:(b_accept_word?b_stage_bank:stage_bank));
 wire write_bank=f_accept_word?f_stage_bank:wb;
 wire [BW-1:0] write_entry=m_accept_word?MISSILE_OFFSET+m_written:(f_accept_word?EFFECT_OFFSET+f_written:
    (p_accept_word?PLAYER_BULLET_OFFSET+p_written:(b_accept_word?BULLET_OFFSET+b_written:written)));
 wire [MW-1:0] write_base=(write_bank?BANK_WORDS:0)+write_entry*3;
 wire [MW-1:0] ca_write=write_base+(f_accept_word?1:(second?2:0));
 wire [MW-1:0] ma_write=write_base+(f_accept_word?(f_phase?2:0):1);
 wire ca_enable=nonfx_write || (f_accept_word && f_phase);
 wire ma_enable=(nonfx_write && second) || f_accept_word;
 wire [9:0] ca_data=f_accept_word?{wdata[11:9],f_d0[6:0]}:
    (accept_word && word_index[0]?d0[25:16]:wdata[9:0]);
 wire [9:0] ma_data=f_accept_word?(f_phase?{1'b0,wdata[8:0]}:wdata[16:7]):
    m_accept_word?{3'd0,2'b01,m_type,wdata[11:10],m_d0[11:10]}:
    p_accept_word?{3'd0,2'b01,1'b1,wdata[11:10],p_d0[11:10]}:
    b_accept_word?{b_layout,b_member_mask[2],wdata[17],b_member_mask[1:0],wdata[16],wdata[11:10],b_d0[11:10]}:
    {3'd0,wdata[1:0],wdata[8],1'b0,d0[26],1'b0,d0[10]};
 wire rb=select_missile?m_active_bank:(select_effect?f_active_bank:(select_player_bullet?p_active_bank:(select_bullet?b_active_bank:active_bank)));

 always @(posedge clk)begin
  if(b_accept_word && b_phase)dir_mem[{b_stage_bank,b_written[9:0]}]<=b_dir;
  dir_q<=dir_mem[{b_active_bank,read_index[9:0]}];
  dir_qv<=select_bullet && !select_missile && !select_player_bullet && !select_effect;
 end
 assign read_dir=dir_qv?dir_q:4'd0;
 wire [BW-1:0] read_entry=select_missile?MISSILE_OFFSET+read_index:(select_effect?EFFECT_OFFSET+read_index:
    (select_player_bullet?PLAYER_BULLET_OFFSET+read_index:(select_bullet?BULLET_OFFSET+read_index:read_index)));
 wire [MW-1:0] read_base=(rb?BANK_WORDS:0)+read_entry*3;
 wire [MW-1:0] ca_read=read_base+(select_effect?1:(read_word[0]?2:0));
 wire [MW-1:0] ma_read=read_base+(select_effect?(read_word[0]?2:0):1);
 wire even_enable=(ca_enable && !ca_write[0]) || (ma_enable && !ma_write[0]);
 wire odd_enable=(ca_enable && ca_write[0]) || (ma_enable && ma_write[0]);
 wire [BW-1:0] even_write=(ma_enable && !ma_write[0])?ma_write[MW-1:1]:ca_write[MW-1:1];
 wire [BW-1:0] odd_write=(ma_enable && ma_write[0])?ma_write[MW-1:1]:ca_write[MW-1:1];
 wire [9:0] even_data=(ma_enable && !ma_write[0])?ma_data:ca_data;
 wire [9:0] odd_data=(ma_enable && ma_write[0])?ma_data:ca_data;
 wire [BW-1:0] even_read=ca_read[0]?ma_read[MW-1:1]:ca_read[MW-1:1];
 wire [BW-1:0] odd_read=ca_read[0]?ca_read[MW-1:1]:ma_read[MW-1:1];
 wire [9:0] even_q,odd_q;
 reg q_ca_odd,q_valid,q_word,q_effect,q_bullet;
 // GHD4 sub-arrays: write enable decoded by address[BW-1:10]; per sub-array read register; registered sub-array select
 wire [9:0] even_qs[0:NB-1];wire [9:0] odd_qs[0:NB-1];
 reg [BW-11:0] even_sel_q,odd_sel_q;
 always @(posedge clk)begin even_sel_q<=even_read[BW-1:10];odd_sel_q<=odd_read[BW-1:10];end
 genvar gi;
 generate for(gi=0;gi<NB;gi=gi+1)begin:sub
  (* syn_ramstyle="block_ram" *) reg [9:0] em[0:1023];
  (* syn_ramstyle="block_ram" *) reg [9:0] om[0:1023];
  reg [9:0] eq,oq;
  always @(posedge clk)begin
   if(even_enable && even_write[BW-1:10]==gi)em[even_write[9:0]]<=even_data;
   if(odd_enable && odd_write[BW-1:10]==gi)om[odd_write[9:0]]<=odd_data;
   eq<=em[even_read[9:0]];oq<=om[odd_read[9:0]];
  end
  assign even_qs[gi]=eq;assign odd_qs[gi]=oq;
 end endgenerate
 assign even_q=even_qs[even_sel_q];assign odd_q=odd_qs[odd_sel_q];
 wire [9:0] ca_q=q_ca_odd?odd_q:even_q;
 wire [9:0] ma_q=q_ca_odd?even_q:odd_q;
 always @(posedge clk)begin
  q_ca_odd<=ca_read[0];
  q_valid<=read_index<selected_live && !read_word[1];
  q_word<=read_word[0];q_effect<=select_effect;q_bullet<=select_bullet || select_player_bullet || select_missile;
 end
 // Preserve the one-clock, two-word rendering interface. FX x is losslessly
 // FX2 stores signed pixel coordinates losslessly in the same three 10-bit cells.
 // Unconsumed sort-slot copies in word1[31:13] are now zero, never read by RTL.
 always @*begin
  read_data=0;
  if(q_valid)begin
   if(q_effect)read_data=q_word?{20'd0,ca_q[9:7],ma_q[8:0]}:{15'd0,ma_q,ca_q[6:0]};
   else if(!q_word)read_data={16'd0,(q_bullet?ma_q[9:8]:2'b00),(q_bullet?ma_q[6:5]:2'b00),ma_q[1:0],ca_q};
   else if(q_bullet)read_data={18'd0,ma_q[7],ma_q[4],ma_q[3:2],ca_q};
   else read_data={18'd0,ma_q[6:4],ma_q[2],ca_q};
  end
 end
 always @*begin
  rdata=0;
  case(addr)
   12'h200:rdata=32'h4d450102; // ME1.2: enemy instances staged in non-decreasing y
   12'h204:rdata=CAPACITY;
   12'h20c:rdata=count_shadow;
   12'h210:rdata=written;
   12'h214:rdata=d0;
   12'h218:rdata=d1;
   12'h21c:rdata=d2;
   12'h224:rdata=sequence_shadow;
   12'h228:rdata={26'd0,frame_valid,word_index,(error!=0),staging,ready};
   12'h22c:rdata=frame_seq;
   12'h230:rdata=live_enabled; // accepted claim, not a displayed-frame assertion
   12'h234:rdata=error;
   12'h238:rdata={31'd0,mode_multi};
   12'h23c:rdata=32'h00020000; // ME features: y-sorted staging
   12'h240:rdata=32'h42550700; // GHD5R: BU7 = BU6 + triple direction word0[23:20]
   // // BU6: BU5 encoding with a 10-bit rank word1[27:18] (BULLET_CAPACITY<=1023, here 600)
   12'h244:rdata=BULLET_CAPACITY;
   12'h24c:rdata=b_count_shadow;
   12'h250:rdata=b_written;
   12'h25c:rdata=b_sequence_shadow;
   12'h260:rdata={27'd0,b_frame_valid,b_phase,(b_error!=0),b_staging,bullet_ready};
   12'h264:rdata=bullet_seq;
   12'h268:rdata=b_live_count;
   12'h26c:rdata=b_error;
   12'h270:rdata={31'd0,bullet_mode};
   12'h274:rdata=32'h000000ff; // prior features plus fixed dual members; mask00 draws nothing
   12'h290:rdata=32'h50420200; // PB2.0: y-sorted
   12'h294:rdata=PLAYER_BULLET_CAPACITY;
   12'h29c:rdata=p_count_shadow;
   12'h2a0:rdata=p_written;
   12'h2ac:rdata=p_sequence_shadow;
   12'h2b0:rdata={27'd0,p_frame_valid,p_phase,(p_error!=0),p_staging,player_bullet_ready};
   12'h2b4:rdata=player_bullet_seq;
   12'h2b8:rdata=p_live_count;
   12'h2bc:rdata={29'd0,p_error};
   12'h2c0:rdata={31'd0,player_bullet_mode};
   12'h2c4:rdata=32'h0000003f;
   12'h2c8:rdata=32'h46580101; // FX1.0: alpha-blended effect instances
   12'h2cc:rdata=EFFECT_CAPACITY;
   12'h2d4:rdata=f_count_shadow;
   12'h2d8:rdata=f_written;
   12'h2e4:rdata=f_sequence_shadow;
   12'h2e8:rdata={27'd0,f_frame_valid,f_phase,(f_error!=0),f_staging,effect_ready};
   12'h2ec:rdata=effect_seq;
   12'h2f0:rdata=f_live_count;
   12'h2f4:rdata={29'd0,f_error};
   12'h2f8:rdata={31'd0,effect_mode};
   12'h2fc:rdata=32'h0000000f; // two types, 4-bit straight alpha, 8 frames max, y-sorted
   12'h340:rdata=32'h4d530100; // PB2.0: y-sorted
   12'h344:rdata=MISSILE_CAPACITY;
   12'h34c:rdata=m_count_shadow;
   12'h350:rdata=m_written;
   12'h35c:rdata=m_sequence_shadow;
   12'h360:rdata={27'd0,m_frame_valid,m_phase,(m_error!=0),m_staging,missile_ready};
   12'h364:rdata=missile_seq;
   12'h368:rdata=m_live_count;
   12'h36c:rdata={29'd0,m_error};
   12'h370:rdata={31'd0,missile_mode};
   12'h374:rdata=32'h0000007f;
  endcase
 end
 always @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   active_bank<=0;stage_bank<=1;staging<=0;ready<=0;frame_valid<=0;
   count_shadow<=0;written<=0;enabled_shadow<=0;live_count<=0;live_enabled<=0;
   word_index<=0;sequence_shadow<=0;error<=0;d0<=0;d1<=0;d2<=0;last_y<=0;
   frame_seq<=0;mode_multi<=0;
   bullet_mode<=0;bullet_ready<=0;bullet_seq<=0;b_active_bank<=0;b_stage_bank<=1;
   b_staging<=0;b_frame_valid<=0;b_phase<=0;b_count_shadow<=0;b_written<=0;b_live_count<=0;
   b_sequence_shadow<=0;b_error<=0;b_d0<=0;b_d0_valid<=1;b_member_mask<=0;b_layout<=0; b_last_y<=0;
   player_bullet_mode<=0;player_bullet_ready<=0;player_bullet_seq<=0;p_active_bank<=0;p_stage_bank<=1;
   p_staging<=0;p_frame_valid<=0;p_phase<=0;p_count_shadow<=0;p_written<=0;p_live_count<=0;
   p_sequence_shadow<=0;p_error<=0;p_d0<=0;p_d0_valid<=0;p_last_y<=0;
   missile_mode<=0;missile_ready<=0;missile_seq<=0;m_active_bank<=0;m_stage_bank<=1;
   m_staging<=0;m_frame_valid<=0;m_phase<=0;m_count_shadow<=0;m_written<=0;m_live_count<=0;
   m_sequence_shadow<=0;m_error<=0;m_d0<=0;m_type<=0;m_d0_valid<=0;m_last_y<=0;
   effect_mode<=0;effect_ready<=0;effect_seq<=0;f_active_bank<=0;f_stage_bank<=1;
   f_staging<=0;f_frame_valid<=0;f_phase<=0;f_count_shadow<=0;f_written<=0;f_live_count<=0;
   f_sequence_shadow<=0;f_error<=0;f_d0<=0;f_d0_valid<=0;f_last_y<=0;
  end else begin
   if(accepted_claim)begin
    active_bank<=stage_bank;live_count<=count_shadow;live_enabled<=enabled_shadow;
    frame_seq<=sequence_shadow;ready<=0;staging<=0;frame_valid<=1;
   end
   if(accepted_claim && bullet_mode)begin
    b_active_bank<=b_stage_bank;b_live_count<=b_count_shadow;bullet_seq<=b_sequence_shadow;
    bullet_ready<=0;b_staging<=0;b_frame_valid<=1;
   end
   if(accepted_claim && player_bullet_mode)begin
    p_active_bank<=p_stage_bank;p_live_count<=p_count_shadow;player_bullet_seq<=p_sequence_shadow;
    player_bullet_ready<=0;p_staging<=0;p_frame_valid<=1;
   end
   if(accepted_claim && missile_mode)begin
    m_active_bank<=m_stage_bank;m_live_count<=m_count_shadow;missile_seq<=m_sequence_shadow;
    missile_ready<=0;m_staging<=0;m_frame_valid<=1;
   end
   if(accepted_claim && effect_mode)begin
    f_active_bank<=f_stage_bank;f_live_count<=f_count_shadow;effect_seq<=f_sequence_shadow;
    effect_ready<=0;f_staging<=0;f_frame_valid<=1;
   end
   if(write)case(addr)
    12'h208:case(wdata)
     1:if(staging || ready)error<=2;
       else begin stage_bank<=~active_bank;staging<=1;error<=0;count_shadow<=0;
        written<=0;enabled_shadow<=0;word_index<=0;last_y<=0;end
     2:if(ready)begin end // idempotent seal; never queues a second frame
       else if(!staging || error!=0 || word_index!=0 || written!=count_shadow)begin
        if(error==0)error<=3;
       end else if(frame_valid && sequence_shadow==frame_seq)error<=4;
       else begin ready<=1;staging<=0;end
     4:begin staging<=0;ready<=0;error<=0;word_index<=0;written<=0;end
     default:error<=1;
    endcase
    12'h20c:if(staging && written==0 && word_index==0 && wdata<=CAPACITY)
      count_shadow<=wdata[CW-1:0];else error<=1;
    12'h210:if(!staging || wdata!=written || word_index!=0)error<=1;
    12'h224:if(staging)sequence_shadow<=wdata;else error<=2;
    12'h214,12'h218,12'h21c,12'h220:
     if(!accept_word)error<=1;
     else begin
      case(word_index)
       0:d0<=wdata;
       1:d1<=wdata;
       2:d2<=wdata;
       3:begin
        if(!entry_ok)error<=1;
        else if(d1[8])enabled_shadow<=enabled_shadow+1'b1;
        last_y<=d0[26:16];written<=written+1'b1;
       end
      endcase
      word_index<=word_index+1'b1;
     end
    12'h238:if(engine_idle && !staging && !ready && !b_staging && !bullet_ready &&
       !p_staging && !player_bullet_ready && !f_staging && !effect_ready && !m_staging && !missile_ready &&
       !bullet_mode && !player_bullet_mode && !effect_mode && !missile_mode && wdata<=1)mode_multi<=wdata[0];else error<=2;
    12'h248:case(wdata)
     1:if(b_staging || bullet_ready)b_error<=2;
       else begin b_stage_bank<=~b_active_bank;b_staging<=1;b_error<=0;b_count_shadow<=0;
        b_written<=0;b_phase<=0;b_last_y<=0;end
     2:if(bullet_ready)begin end
       else if(!b_staging || b_error!=0 || b_phase || b_written!=b_count_shadow)begin
        if(b_error==0)b_error<=3;
       end else if(b_frame_valid && b_sequence_shadow==bullet_seq)b_error<=4;
       else begin bullet_ready<=1;b_staging<=0;end
     4:begin b_staging<=0;bullet_ready<=0;b_error<=0;b_phase<=0;b_written<=0;end
     default:b_error<=1;
    endcase
    12'h24c:if(b_staging && b_written==0 && !b_phase && wdata<=BULLET_CAPACITY)
      b_count_shadow<=wdata[BCW-1:0];else b_error<=1;
    12'h25c:if(b_staging)b_sequence_shadow<=wdata;else b_error<=2;
    12'h254,12'h258:if(!b_accept_word)b_error<=1;
     else if(!b_phase)begin b_d0<=wdata[15:0];b_member_mask<=wdata[18:16];b_layout<=wdata[19];b_dir<=wdata[23:20];
      b_d0_valid<=wdata[31:24]==0 && (wdata[19] || wdata[23:20]==0);b_phase<=1;end // GHD5R: direction only for triple layout
     else begin
      if(!b_entry_ok)b_error<=1;
      b_last_y<=by;b_written<=b_written+1'b1;b_phase<=0;
     end
    12'h270:if(engine_idle && !staging && !ready && !b_staging && !bullet_ready && !p_staging && !player_bullet_ready &&
       !f_staging && !effect_ready && !m_staging && !missile_ready && wdata<=1 && (!wdata[0] || mode_multi))bullet_mode<=wdata[0];else b_error<=2;
    12'h298:case(wdata)
     1:if(p_staging || player_bullet_ready)p_error<=2;
       else begin p_stage_bank<=~p_active_bank;p_staging<=1;p_error<=0;p_count_shadow<=0;
        p_written<=0;p_phase<=0;p_last_y<=0;end
     2:if(player_bullet_ready)begin end
       else if(!p_staging || p_error!=0 || p_phase || p_written!=p_count_shadow)begin
        if(p_error==0)p_error<=3;
       end else if(p_frame_valid && p_sequence_shadow==player_bullet_seq)p_error<=4;
       else begin player_bullet_ready<=1;p_staging<=0;end
     4:begin p_staging<=0;player_bullet_ready<=0;p_error<=0;p_phase<=0;p_written<=0;end
     default:p_error<=1;
    endcase
    12'h29c:if(p_staging && p_written==0 && !p_phase && wdata<=PLAYER_BULLET_CAPACITY)
      p_count_shadow<=wdata[PCW-1:0];else p_error<=1;
    12'h2ac:if(p_staging)p_sequence_shadow<=wdata;else p_error<=2;
    12'h2a4,12'h2a8:if(!p_accept_word)p_error<=1;
     else if(!p_phase)begin p_d0<=wdata[15:0];p_d0_valid<=wdata[31:16]==0;p_phase<=1;end
     else begin
      if(!p_entry_ok)p_error<=1;
      p_last_y<=by;p_written<=p_written+1'b1;p_phase<=0;
     end
    12'h2c0:if(engine_idle && !staging && !ready && !b_staging && !bullet_ready && !p_staging && !player_bullet_ready &&
       !f_staging && !effect_ready && !m_staging && !missile_ready && wdata<=1 && (!wdata[0] || mode_multi))player_bullet_mode<=wdata[0];else p_error<=2;
    12'h348:case(wdata)
     1:if(m_staging || missile_ready)m_error<=2;
       else begin m_stage_bank<=~m_active_bank;m_staging<=1;m_error<=0;m_count_shadow<=0;
        m_written<=0;m_phase<=0;m_last_y<=0;end
     2:if(missile_ready)begin end
       else if(!m_staging || m_error!=0 || m_phase || m_written!=m_count_shadow)begin
        if(m_error==0)m_error<=3;
       end else if(m_frame_valid && m_sequence_shadow==missile_seq)m_error<=4;
       else begin missile_ready<=1;m_staging<=0;end
     4:begin m_staging<=0;missile_ready<=0;m_error<=0;m_phase<=0;m_written<=0;end
     default:m_error<=1;
    endcase
    12'h34c:if(m_staging && m_written==0 && !m_phase && wdata<=MISSILE_CAPACITY)
      m_count_shadow<=wdata[MCW-1:0];else m_error<=1;
    12'h35c:if(m_staging)m_sequence_shadow<=wdata;else m_error<=2;
    12'h354,12'h358:if(!m_accept_word)m_error<=1;
     else if(!m_phase)begin m_d0<=wdata[15:0];m_type<=wdata[16];m_d0_valid<=wdata[31:18]==0 && wdata[17];m_phase<=1;end
     else begin
      if(!m_entry_ok)m_error<=1;
      m_last_y<=by;m_written<=m_written+1'b1;m_phase<=0;
     end
    12'h370:if(engine_idle && !staging && !ready && !b_staging && !bullet_ready && !p_staging && !player_bullet_ready &&
       !f_staging && !effect_ready && !m_staging && !missile_ready && wdata<=1 && (!wdata[0] || mode_multi))missile_mode<=wdata[0];else m_error<=2;
    12'h2d0:case(wdata)
     1:if(f_staging || effect_ready)f_error<=2;
       else begin f_stage_bank<=~f_active_bank;f_staging<=1;f_error<=0;f_count_shadow<=0;
        f_written<=0;f_phase<=0;f_last_y<=0;end
     2:if(effect_ready)begin end
       else if(!f_staging || f_error!=0 || f_phase || f_written!=f_count_shadow)begin
        if(f_error==0)f_error<=3;
       end else if(f_frame_valid && f_sequence_shadow==effect_seq)f_error<=4;
       else begin effect_ready<=1;f_staging<=0;end
     4:begin f_staging<=0;effect_ready<=0;f_error<=0;f_phase<=0;f_written<=0;end
     default:f_error<=1;
    endcase
    12'h2d4:if(f_staging && f_written==0 && !f_phase && wdata<=EFFECT_CAPACITY)
      f_count_shadow<=wdata[FCW-1:0];else f_error<=1;
    12'h2e4:if(f_staging)f_sequence_shadow<=wdata;else f_error<=2;
    12'h2dc,12'h2e0:if(!f_accept_word)f_error<=1;
     else if(!f_phase)begin f_d0<=wdata[16:0];f_d0_valid<=wdata[31:17]==0;f_phase<=1;end
     else begin
      if(!f_entry_ok)f_error<=1;
      f_last_y<=wdata[11:0];f_written<=f_written+1'b1;f_phase<=0;
     end
    12'h2f8:if(engine_idle && !staging && !ready && !b_staging && !bullet_ready && !p_staging && !player_bullet_ready &&
       !f_staging && !effect_ready && !m_staging && !missile_ready && wdata<=1 && (!wdata[0] || mode_multi))effect_mode<=wdata[0];else f_error<=2;
    default:;
   endcase
  end
 end
endmodule
