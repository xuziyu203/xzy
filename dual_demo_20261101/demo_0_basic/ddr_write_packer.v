module ddr_write_packer
#(
    parameter I_VID_WIDTH        = 16,
    parameter AXI_DATA_WIDTH     = 128,

    // 对128bit RGB565：
    // PIXELS_PER_BEAT = 8
    // OFFSET_WIDTH    = 3
    parameter OFFSET_WIDTH =
        $clog2(AXI_DATA_WIDTH / I_VID_WIDTH)
)
(
    input  wire                         clk,
    input  wire                         rst_n,

    // 整体工作使能
    input  wire                         start,

    // Latched with frame_start; zero preserves the ordinary pixel path.
    input  wire                         solid_fill_enable,
    input  wire [I_VID_WIDTH-1:0]        solid_fill_color,

    //========================================================
    // 上游 pixel stream
    //========================================================

    input  wire [I_VID_WIDTH-1:0]       wr_fifo_rddata,
    input  wire                         i_de,
    input  wire                         valid,

    output wire                         ready,


    //========================================================
    // 本次有效像素数量
    //
    // 注意：
    // 仍然是真正要写的pixel数量
    //
    // 例如：
    // left=230,width=500
    //
    // wr_len仍然是500
    // 不是506
    //========================================================

    input  wire [23:0]                  wr_len,


    //========================================================
    // 新增：
    //
    // 当前矩形起点在AXI beat中的pixel偏移
    //
    // 对128bit RGB565：
    //
    // 0~7
    //
    // 例如：
    // left=230
    // 230 % 8 = 6
    //
    // start_pixel_offset = 6
    //========================================================

    input  wire [OFFSET_WIDTH-1:0]      start_pixel_offset,
    input  wire                         fifo_full,

    //========================================================
    // 行/帧启动握手
    //========================================================

    input  wire                         vs_valid,
    output wire                         vs_ready,


    //========================================================
    // FIFO
    //========================================================

    output wire                         fifo_wr_en,

    output wire [AXI_DATA_WIDTH-1:0]    fifo_wr_data,

    output wire [AXI_DATA_WIDTH/8-1:0]  wstrb,


    /*
        保留你原来的fifo接口。

        但是不再额外寄存一拍。

        保证：

            fifo_wr_en
            WSTRB
            WDATA

        是严格同一个beat。
    */

    output wire [
        AXI_DATA_WIDTH +
        AXI_DATA_WIDTH/8 - 1 : 0
    ]                                   fifo,


    //========================================================
    // 给后级DDR writer
    //========================================================

    output wire                         frame_start,

    output wire                         fifo_rst_p,

    output wire [23:0]                  ddr_len
);


/*============================================================
    参数
============================================================*/

localparam AXI_STRB_WIDTH =
        AXI_DATA_WIDTH / 8;


// 128bit / 16bit = 8 pixel
localparam PIXELS_PER_BEAT =
        AXI_DATA_WIDTH / I_VID_WIDTH;


// RGB565 = 2 Byte
localparam BYTES_PER_PIXEL =
        I_VID_WIDTH / 8;


// pack_cnt必须能表示0~PIXELS_PER_BEAT
localparam PACK_CNT_WIDTH =
        $clog2(PIXELS_PER_BEAT + 1);

// DC_FIFO.v: with WrClk and RdClk both driven by core_clk, reset
// release takes three edges for WrClkRstGen (3 -> 2 -> 1 -> 0),
// then one edge for the registered WrClkRst to become zero.
// Wait these four edges AFTER frame_start is accepted. Recheck this
// bound if the FIFO read/write clocks or its reset pipeline change.
localparam FIFO_RESET_RELEASE_CYCLES = 4;
localparam FIFO_RESET_WAIT_CNT_WIDTH =
        $clog2(FIFO_RESET_RELEASE_CYCLES);



/*============================================================
    状态机
============================================================*/

localparam STATE_IDLE           = 3'd0;
localparam STATE_PACK           = 3'd1;
localparam STATE_PUSH_FULL      = 3'd2;
localparam STATE_PUSH_FULL_LAST = 3'd3;
localparam STATE_PUSH_LAST      = 3'd4;
localparam STATE_FIFO_RESET_WAIT = 3'd5;
localparam STATE_SOLID_READY     = 3'd6;
localparam STATE_SOLID_PUSH      = 3'd7;


reg [2:0] state;
reg [FIFO_RESET_WAIT_CNT_WIDTH-1:0] fifo_reset_wait_cnt;
reg solid_mode_r;
reg [I_VID_WIDTH-1:0] solid_color_r;
reg [23:0] solid_pixels_left;
reg [PACK_CNT_WIDTH-1:0] solid_offset;
wire [PACK_CNT_WIDTH-1:0] solid_room = PIXELS_PER_BEAT - solid_offset;
wire [PACK_CNT_WIDTH-1:0] solid_pixels_this_beat =
        (solid_pixels_left < solid_room) ?
        solid_pixels_left[PACK_CNT_WIDTH-1:0] : solid_room;
reg [AXI_STRB_WIDTH-1:0] solid_wstrb;
integer solid_lane;
always @(*) begin
    solid_wstrb = {AXI_STRB_WIDTH{1'b0}};
    for(solid_lane = 0; solid_lane < PIXELS_PER_BEAT; solid_lane = solid_lane + 1)
        if((solid_lane >= solid_offset) &&
           (solid_lane < solid_offset + solid_pixels_this_beat))
            // Preserve the legacy mapping: logical pixel 0 occupies MSBs.
            solid_wstrb[(PIXELS_PER_BEAT-1-solid_lane)*BYTES_PER_PIXEL
                        +: BYTES_PER_PIXEL] = {BYTES_PER_PIXEL{1'b1}};
end



/*============================================================
    当前AXI beat数据
============================================================*/

reg [AXI_DATA_WIDTH-1:0]
        pack_data;


/*
    与pack_data严格对应的Byte Enable

    以后不再根据：

        FULL / LAST

    临时计算WSTRB。

    而是在每接收一个pixel时，
    同时把对应Byte lane置1。
*/

reg [AXI_STRB_WIDTH-1:0]
        pack_wstrb;



/*============================================================
    当前beat逻辑pixel位置
============================================================*/

/*
    pack_cnt表示：

    当前下一个pixel应该放在
    这一AXI beat中的第几个逻辑位置。

    对128bit：

        0 1 2 3 4 5 6 7

    如果：

        start_offset = 6

    第一拍直接从位置6开始。
*/

reg [PACK_CNT_WIDTH-1:0]
        pack_cnt;



/*============================================================
    已接受的真实pixel数量
============================================================*/

reg [23:0] wr_cnt;



/*============================================================
    上游握手
============================================================*/

assign ready =
        (state == STATE_PACK) || (state == STATE_SOLID_READY);


wire pixel_fire;


assign pixel_fire =
        valid &&
        (state == STATE_PACK) &&
        i_de;



/*============================================================
    frame start
============================================================*/

assign vs_ready =
        (state == STATE_IDLE) &&
        start;


assign frame_start =
        vs_valid &&
        vs_ready;


assign fifo_rst_p =
        frame_start;



/*============================================================
    DDR长度计算

    原来：

        ceil(wr_len / 8)

    现在：

        ceil(
            (start_offset + wr_len)
            /
            PIXELS_PER_BEAT
        )

    例如：

        left  =230
        offset=6
        width =500

        6+500=506

        ceil(506/8)
        =64 beats
============================================================*/

wire [24:0] total_pixel_slots;


assign total_pixel_slots =
        {1'b0, wr_len}
        +
        start_pixel_offset;


assign ddr_len =
        (
            total_pixel_slots
            +
            PIXELS_PER_BEAT
            - 1
        )
        /
        PIXELS_PER_BEAT;



/*============================================================
    FIFO write
============================================================*/

assign fifo_wr_en =
        (state == STATE_PUSH_FULL)      ||
        (state == STATE_PUSH_FULL_LAST) ||
        (state == STATE_PUSH_LAST) ||
        ((state == STATE_SOLID_PUSH) && !fifo_full);



assign fifo_wr_data =
        (state == STATE_SOLID_PUSH) ? {PIXELS_PER_BEAT{solid_color_r}} : pack_data;


assign wstrb =
        (state == STATE_SOLID_PUSH) ? solid_wstrb : pack_wstrb;



/*
    重要修改：

    不再：

        fifo_wr_en
        ↓
        fifo <= {wstrb,pack_data}
        ↓
        下一级同一拍又写FIFO

    否则会拿到上一拍fifo。

    现在直接组合输出。
*/

assign fifo =
        {
            wstrb,
            fifo_wr_data
        };



/*============================================================
    状态机 + pixel packing
============================================================*/

always @(posedge clk or negedge rst_n)
begin

    if(!rst_n)
    begin

        state <= STATE_IDLE;
        fifo_reset_wait_cnt <= {FIFO_RESET_WAIT_CNT_WIDTH{1'b0}};
        solid_mode_r <= 1'b0;
        solid_color_r <= {I_VID_WIDTH{1'b0}};
        solid_pixels_left <= 24'd0;
        solid_offset <= {PACK_CNT_WIDTH{1'b0}};


        pack_data <=
            {AXI_DATA_WIDTH{1'b0}};


        pack_wstrb <=
            {AXI_STRB_WIDTH{1'b0}};


        pack_cnt <=
            {PACK_CNT_WIDTH{1'b0}};


        wr_cnt <=
            24'd0;

    end


    else
    begin

        case(state)


        /*====================================================
            IDLE
        ====================================================*/

        STATE_IDLE:
        begin

            fifo_reset_wait_cnt <= {FIFO_RESET_WAIT_CNT_WIDTH{1'b0}};

            pack_data <=
                {AXI_DATA_WIDTH{1'b0}};


            pack_wstrb <=
                {AXI_STRB_WIDTH{1'b0}};


            wr_cnt <=
                24'd0;


            /*
                关键：

                第一拍不再永远从0开始。

                而是：

                    pack_cnt=start_pixel_offset

                比如left=230：

                    pack_cnt=6
            */

            if(frame_start)
            begin

                pack_cnt <=
                    start_pixel_offset;

                solid_mode_r <= solid_fill_enable;
                solid_color_r <= solid_fill_color;
                solid_pixels_left <= wr_len;
                solid_offset <= start_pixel_offset;
                // Both paths must pass the same DC_FIFO reset-release wait.
                // Loading N-1 makes the Nth following WAIT edge release.
                fifo_reset_wait_cnt <= FIFO_RESET_RELEASE_CYCLES - 1;
                state <=
                    STATE_FIFO_RESET_WAIT;

            end

            else
            begin

                pack_cnt <=
                    {PACK_CNT_WIDTH{1'b0}};

                state <=
                    STATE_IDLE;

            end

        end



        /*====================================================
            PACK
        ====================================================*/

        STATE_FIFO_RESET_WAIT:
        begin
            // ready and fifo_wr_en remain low in this state. Preserve
            // pack_cnt and wr_cnt; no pixel can be accepted during reset.
            if(fifo_reset_wait_cnt == 0)
                state <= solid_mode_r ? STATE_SOLID_READY : STATE_PACK;
            else
                fifo_reset_wait_cnt <= fifo_reset_wait_cnt - 1'b1;
        end

        STATE_SOLID_READY:
        begin
            // ready acknowledges the row, not a pixel. Wait for the renderer
            // to withdraw vs_valid before writing: even a one-beat row must
            // not return to IDLE while the old row request is still asserted.
            if(!vs_valid)
                state <= (solid_pixels_left == 0) ? STATE_IDLE : STATE_SOLID_PUSH;
        end

        STATE_SOLID_PUSH:
        begin
            // One complete masked beat per accepted FIFO write. All payload
            // and counters hold under backpressure; no pixel stream is used.
            if(!fifo_full)
            begin
                solid_pixels_left <= solid_pixels_left - solid_pixels_this_beat;
                solid_offset <= {PACK_CNT_WIDTH{1'b0}};
                if(solid_pixels_left == solid_pixels_this_beat)
                    state <= STATE_IDLE;
            end
        end

        STATE_PACK:
        begin

            if(pixel_fire)
            begin

                /*
                    保持你原先的pixel顺序：

                    一拍中：

                    第0个逻辑pixel
                        → 最高16bit

                    最后一个逻辑pixel
                        → 最低16bit


                    对128bit：

                    logical position

                    0 → [127:112]
                    1 → [111:96]
                    ...
                    7 → [15:0]


                    这样不会改变你已经跑通的
                    full beat数据排列。
                */


                pack_data[
                    (
                        PIXELS_PER_BEAT
                        - 1
                        - pack_cnt
                    )
                    * I_VID_WIDTH
                    +:
                    I_VID_WIDTH
                ]
                <= wr_fifo_rddata;



                /*
                    同时打开这个pixel对应的
                    两个Byte strobe。

                    RGB565：

                        BYTES_PER_PIXEL=2
                */

                pack_wstrb[
                    (
                        PIXELS_PER_BEAT
                        - 1
                        - pack_cnt
                    )
                    * BYTES_PER_PIXEL
                    +:
                    BYTES_PER_PIXEL
                ]
                <=
                    {BYTES_PER_PIXEL{1'b1}};



                /*--------------------------------------------
                    当前是否是整个任务最后一个真实pixel
                --------------------------------------------*/

                if(
                    (wr_len != 0)
                    &&
                    (wr_cnt == wr_len - 1'b1)
                )
                begin

                    wr_cnt <=
                        wr_cnt + 1'b1;



                    /*
                        当前这个pixel同时刚好填到
                        当前beat最后一个位置。
                    */

                    if(
                        pack_cnt ==
                        PIXELS_PER_BEAT - 1
                    )
                    begin

                        pack_cnt <=
                            {PACK_CNT_WIDTH{1'b0}};


                        state <=
                            STATE_PUSH_FULL_LAST;

                    end


                    /*
                        最后一个pixel出现时
                        beat还没填满。

                        pack_wstrb自然只会打开
                        真正写过的Byte。
                    */

                    else
                    begin

                        pack_cnt <=
                            pack_cnt + 1'b1;


                        state <=
                            STATE_PUSH_LAST;

                    end

                end



                /*--------------------------------------------
                    还不是最后一个pixel
                --------------------------------------------*/

                else
                begin

                    wr_cnt <=
                        wr_cnt + 1'b1;


                    /*
                        当前beat已经到最后一个位置。
                    */

                    if(
                        pack_cnt ==
                        PIXELS_PER_BEAT - 1
                    )
                    begin

                        pack_cnt <=
                            {PACK_CNT_WIDTH{1'b0}};


                        state <=
                            STATE_PUSH_FULL;

                    end


                    else
                    begin

                        pack_cnt <=
                            pack_cnt + 1'b1;


                        state <=
                            STATE_PACK;

                    end

                end

            end

        end



        /*====================================================
            PUSH_FULL

            注意：

            名字虽然叫FULL，
            但是第一拍存在start_offset时，
            WSTRB有可能不是全1。

            所以真正是否全有效，
            完全由pack_wstrb决定。
        ====================================================*/

        STATE_PUSH_FULL:
        begin
            if(!fifo_full)
            begin
                pack_data <=
                {AXI_DATA_WIDTH{1'b0}};


                pack_wstrb <=
                    {AXI_STRB_WIDTH{1'b0}};


                pack_cnt <=
                    {PACK_CNT_WIDTH{1'b0}};


                state <=
                    STATE_PUSH_FULL;
            /*
                当前周期：

                    fifo_wr_en=1

                下游采走：

                    pack_data
                    pack_wstrb
            */


            state <=
                STATE_PACK;
            end

        end



        /*====================================================
            PUSH_FULL_LAST
        ====================================================*/

        STATE_PUSH_FULL_LAST:
        begin
             if(!fifo_full)
                begin

                    pack_data <=
                        {AXI_DATA_WIDTH{1'b0}};


                    pack_wstrb <=
                        {AXI_STRB_WIDTH{1'b0}};


                    pack_cnt <=
                        {PACK_CNT_WIDTH{1'b0}};


                    wr_cnt <=
                        24'd0;


                    state <=
                        STATE_IDLE;

                end

        end



        /*====================================================
            PUSH_LAST
        ====================================================*/

        STATE_PUSH_LAST:
        begin

            if(!fifo_full)
            begin

                /*
                    partial beat 的 WSTRB 已经准备好了。
                    FIFO 真正接受以后才能清空。
                */

                pack_data <=
                    {AXI_DATA_WIDTH{1'b0}};


                pack_wstrb <=
                    {AXI_STRB_WIDTH{1'b0}};


                pack_cnt <=
                    {PACK_CNT_WIDTH{1'b0}};


                wr_cnt <=
                    24'd0;


                state <=
                    STATE_IDLE;

            end

        end



        default:
        begin

            fifo_reset_wait_cnt <= {FIFO_RESET_WAIT_CNT_WIDTH{1'b0}};

            state <=
                STATE_IDLE;


            pack_data <=
                {AXI_DATA_WIDTH{1'b0}};


            pack_wstrb <=
                {AXI_STRB_WIDTH{1'b0}};


            pack_cnt <=
                {PACK_CNT_WIDTH{1'b0}};


            wr_cnt <=
                24'd0;

        end


        endcase

    end

end


endmodule
