module block_copy #(
parameter RUNTIME_BURST = 0,
    parameter integer FRAME_WIDTH     = 1920,
    parameter integer PIXEL_WIDTH     = 16,

    parameter integer WR_FIFO_DEPTH   = 1024,
    parameter integer RD_FIFO_DEPTH   = 1024,

    // AXI LEN = 15 表示一次最多 16 beats
    parameter [7:0]   BURST_LEN       = 8'd15,

    parameter integer AXI_DATA_WIDTH  = 128,
    parameter integer AXI_ADDR_WIDTH  = 32,
    parameter integer AXI_STRB_WIDTH  = AXI_DATA_WIDTH / 8
)
(
input wire single_beat_mode,
    input  wire                         clk,
    input  wire                         rst_n,


    /*==========================================================
        Block Copy Command

        block_en:
            请求启动一次 block copy

        block_req / block_ack:
            给未来总线仲裁器使用

        src_addr / dst_addr:
            当前矩形左上角的绝对 byte address

        width / height:
            pixel 单位
    ==========================================================*/

    input  wire                         block_en,

    output reg                          block_req,
    input  wire                         block_ack,

    input  wire [31:0]                  src_addr,
    input  wire [31:0]                  dst_addr,
    input  wire [31:0]                  src_stride,
    input  wire [31:0]                  dst_stride,

    input  wire [11:0]                  width,
    input  wire [11:0]                  height,

    output reg                          done,


    /*==========================================================
        DDR Read Bank Switch Interface
        保留你现有 ddr_rd_buffer 接口
    ==========================================================*/

    output wire                         rd_sw,
    input  wire                         rd_sw_ack,


    /*==========================================================
        DDR Write Bank Switch Interface
    ==========================================================*/

    output wire                         wr_sw,
    input  wire                         wr_sw_ack,

    output wire                         ddr_wr_error,


    /*==========================================================
        AXI Write Address Channel
    ==========================================================*/

    output wire [3:0]                   awid,
    output wire [AXI_ADDR_WIDTH-1:0]    awaddr,
    output wire [7:0]                   awlen,
    output wire [2:0]                   awsize,
    output wire [1:0]                   awburst,
    output wire [3:0]                   awcache,
    output wire [2:0]                   awprot,
    output wire                         awlock,
    output wire                         awvalid,
    output wire                         awcobuf,
    output wire                         awapcmd,
    output wire                         awallstrb,
    output wire                         awqos,

    input  wire                         awready,


    /*==========================================================
        AXI Read Address Channel
    ==========================================================*/

    output wire [3:0]                   arid,
    output wire [AXI_ADDR_WIDTH-1:0]    araddr,
    output wire [7:0]                   arlen,
    output wire [2:0]                   arsize,
    output wire [1:0]                   arburst,
    output wire                         arlock,
    output wire                         arvalid,
    output wire                         arapcmd,
    output wire                         arqos,
    output wire [3:0]                   arcache,
    output wire [2:0]                   arprot,

    input  wire                         arready,


    /*==========================================================
        AXI Write Data Channel
    ==========================================================*/

    output wire [AXI_DATA_WIDTH-1:0]    wdata,
    output wire [AXI_STRB_WIDTH-1:0]    wstrb,
    output wire                         wlast,
    output wire                         wvalid,

    input  wire                         wready,


    /*==========================================================
        AXI Read Data Channel
    ==========================================================*/

    input  wire [3:0]                   rid,
    input  wire [AXI_DATA_WIDTH-1:0]    rdata,
    input  wire                         rlast,
    input  wire                         rvalid,

    output wire                         rready,

    input  wire [1:0]                   rresp,


    /*==========================================================
        AXI Write Response Channel
    ==========================================================*/

    input  wire [3:0]                   bid,
    input  wire                         bvalid,
    input  wire [1:0]                   s_axi_bresp,

    output wire                         bready,


    /*==========================================================
        Debug
    ==========================================================*/

    output wire [31:0]                  test_wdata,
    output wire [31:0]                  test_rdata,
    output reg error,

    output wire [31:0]                  wr_fifo_rd_data_test
);



/*============================================================
    参数计算
============================================================*/

localparam integer BYTES_PER_PIXEL =
        PIXEL_WIDTH / 8;


// 128bit / 16bit = 8 pixel / AXI beat
localparam integer PIXELS_PER_BEAT =
        AXI_DATA_WIDTH / PIXEL_WIDTH;


// 一帧 framebuffer 每行的 byte stride
//
// 1920 * 2 = 3840 byte
localparam integer FRAME_STRIDE_BYTES =
        FRAME_WIDTH * BYTES_PER_PIXEL;



/*============================================================
    状态机
============================================================*/

localparam [2:0]
    S_IDLE          = 3'd0,
    S_REQ           = 3'd1,
    S_ROW_START     = 3'd2,
    S_COPY          = 3'd3,
    S_WAIT_WR_DONE  = 3'd4,
    S_DONE          = 3'd5,
    S_WAIT_RELEASE  = 3'd6,
    S_ERROR = 3'd7;


reg [2:0] state;



/*============================================================
    Command 参数锁存
============================================================*/

reg [31:0] src_base_r;
reg [31:0] dst_base_r;
reg [31:0] src_stride_r;
reg [31:0] dst_stride_r;

reg [11:0] width_r;
reg [11:0] height_r;


// 当前处理到第几行
reg [11:0] row_cnt;


// 当前行真正使用的 DDR 地址
reg [31:0] src_row_addr;
reg [31:0] dst_row_addr;


// 每行需要复制多少个 128-bit beat
//
// RGB565 + 128bit AXI：
//      1 beat = 8 pixel
//
// 例如 width=1920：
//      row_beats = 1920 / 8
//                = 240
reg [24:0] row_beats_r;



/*============================================================
    DDR Engine Start

    每一行只产生一个单周期 start。

    Read Engine:
        从 src_row_addr 读取 row_beats_r

    Write Engine:
        向 dst_row_addr 写 row_beats_r
============================================================*/

reg block_start;



/*============================================================
    Read FIFO Interface
============================================================*/

wire                            rd_fifo_rdvalid;
wire [AXI_DATA_WIDTH-1:0]                        rd_fifo_rddata;
wire                            rd_fifo_rdempty;

wire                            rd_fifo_rden;



/*============================================================
    Write FIFO Interface
============================================================*/

wire                            wr_fifo_wrfull;

wire                            wr_fifo_wren;
wire [AXI_DATA_WIDTH + AXI_STRB_WIDTH-1:0]       wr_fifo_wrdata;



/*============================================================
    中间一级 128bit 暂存

    为什么需要它：

    Read FIFO 的数据不能直接“猜测”有效。

    先：
        rden

    等：
        rdvalid

    再：
        stage_data

    最后：
        Write FIFO

    这样可以兼容普通同步 FIFO。
============================================================*/

reg [AXI_DATA_WIDTH-1:0] stage_data;

reg                      stage_valid;


// 已经对 Read FIFO 发出了一个 pop，
// 正在等 rdvalid 返回
reg                      rd_pop_pending;



/*============================================================
    当前行计数

    rd_issue_cnt:
        已经从 Read FIFO 请求过多少个 beat

    moved_beats:
        已经真正成功写进 Write FIFO 多少 beat
============================================================*/

reg [31:0] rd_issue_cnt;
reg [31:0] moved_beats;



/*============================================================
    DDR Write Done
============================================================*/

wire wr_done;

wire test_cnt;



/*============================================================
    Read FIFO POP

    必须满足：

        1. 当前处于 COPY
        2. stage 为空
        3. 没有旧读请求等待
        4. 当前行还没读完
        5. Read FIFO 非空
============================================================*/

wire rd_pop_fire;

assign rd_pop_fire =
        (state == S_COPY)
        &&
        (!stage_valid)
        &&
        (!rd_pop_pending)
        &&
        (rd_issue_cnt < row_beats_r)
        &&
        (!rd_fifo_rdempty)
        &&
        (!ddr_wr_error);


assign rd_fifo_rden = rd_pop_fire;



/*============================================================
    Write FIFO PUSH

    stage_data 有效
    +
    Write FIFO 还有空间

    才真正提交这个128bit beat。
============================================================*/

wire wr_push_fire;

assign wr_push_fire =
        (state == S_COPY)
        &&
        stage_valid
        &&
        (!wr_fifo_wrfull)
        &&
        (!ddr_wr_error);


assign wr_fifo_wren =
        wr_push_fire;



assign wr_fifo_wrdata =
{
    {AXI_STRB_WIDTH{1'b1}},
    stage_data
};


/*============================================================
    Block Copy 主状态机
============================================================*/

always @(posedge clk or negedge rst_n)
begin

    if(!rst_n)
    begin
        error <= 1'b0;
        state <=
            S_IDLE;


        block_req <=
            1'b0;


        block_start <=
            1'b0;


        done <=
            1'b0;


        src_base_r <=
            32'd0;

        dst_base_r <=
            32'd0;


        width_r <=
            12'd0;

        height_r <=
            12'd0;


        row_cnt <=
            12'd0;


        src_row_addr <=
            32'd0;

        dst_row_addr <=
            32'd0;


        row_beats_r <=
            25'd0;


        stage_data <=
            {AXI_DATA_WIDTH{1'b0}};

        stage_valid <=
            1'b0;


        rd_pop_pending <=
            1'b0;


        rd_issue_cnt <=
            32'd0;

        moved_beats <=
            32'd0;

    end


    else
    begin

        /*----------------------------------------------------
            默认脉冲拉低
        ----------------------------------------------------*/

        done <=
            1'b0;


        block_start <=
            1'b0;



        case(state)


        /*====================================================
            IDLE
        ====================================================*/

        S_IDLE:
        begin

            block_req <=
                1'b0;


            row_cnt <=
                12'd0;


            stage_valid <=
                1'b0;


            rd_pop_pending <=
                1'b0;


            rd_issue_cnt <=
                32'd0;


            moved_beats <=
                32'd0;


            /*
                block_en 一旦被接受，
                立即锁存所有参数。

                后续 copy 过程中，
                外部即使修改输入，
                也不会影响当前任务。
            */

            if(block_en)
            begin

                src_base_r <=
                    src_addr;


                dst_base_r <=
                    dst_addr;

                src_stride_r <= (src_stride == 32'd0) ? FRAME_STRIDE_BYTES : src_stride;
                dst_stride_r <= (dst_stride == 32'd0) ? FRAME_STRIDE_BYTES : dst_stride;


                width_r <=
                    width;


                height_r <=
                    height;


                /*
                    第一版要求：

                    width % 8 == 0

                    因为：
                    128bit / RGB565
                    = 8 pixel / beat
                */

                row_beats_r <=
                    width / PIXELS_PER_BEAT;


                block_req <=
                    1'b1;


                state <=
                    S_REQ;

            end

        end



        /*====================================================
            请求 DDR / 总线使用权

            block_req 保持高，
            直到整个 Block Copy 结束。
        ====================================================*/

        S_REQ:
        begin

            block_req <=
                1'b1;


            if(block_ack)
            begin

                /*
                    0宽/0高直接结束
                */

                if(
                    (width_r == 0)
                    ||
                    (height_r == 0)
                )
                begin

                    state <=
                        S_DONE;

                end


                else
                begin

                    row_cnt <=
                        12'd0;


                    src_row_addr <=
                        src_base_r;


                    dst_row_addr <=
                        dst_base_r;


                    state <=
                        S_ROW_START;

                end

            end

        end



        /*====================================================
            启动当前一行 DDR Read + DDR Write

            block_start 只拉高一个 clk。

            两个 DDR Engine 同时收到任务：

            READ:
                addr = src_row_addr
                len  = row_beats_r

            WRITE:
                addr = dst_row_addr
                len  = row_beats_r
        ====================================================*/

        S_ROW_START:
        begin

            block_req <=
                1'b1;


            block_start <=
                1'b1;


            rd_issue_cnt <=
                32'd0;


            moved_beats <=
                32'd0;


            stage_valid <=
                1'b0;


            rd_pop_pending <=
                1'b0;


            state <=
                S_COPY;

        end



        /*====================================================
            COPY

            Read FIFO
                ↓
            stage_data
                ↓
            Write FIFO
        ====================================================*/

        S_COPY:
        begin

            block_req <=
                1'b1;



            /*----------------------------------------------
                向 Read FIFO 请求一个 beat
            ----------------------------------------------*/

            if(rd_pop_fire)
            begin

                rd_issue_cnt <=
                    rd_issue_cnt + 1'b1;


                rd_pop_pending <=
                    1'b1;

            end



            /*----------------------------------------------
                Read FIFO 返回有效数据

                锁存到 stage_data
            ----------------------------------------------*/

            if(
                rd_pop_pending
                &&
                rd_fifo_rdvalid
            )
            begin

                stage_data <=
                    rd_fifo_rddata;


                stage_valid <=
                    1'b1;


                rd_pop_pending <=
                    1'b0;

            end



            /*----------------------------------------------
                stage_data 成功进入 Write FIFO
            ----------------------------------------------*/

            if(wr_push_fire)
            begin

                stage_valid <=
                    1'b0;


                moved_beats <=
                    moved_beats + 1'b1;


                /*
                    当前已经把这一行最后一个 beat
                    送入 Write FIFO。

                    注意：

                    这里不能直接认为 DDR 写完成！

                    后面必须等：
                        AW/W
                        BVALID/BREADY
                        wr_done
                */

                if(
                    moved_beats
                    ==
                    row_beats_r - 1'b1
                )
                begin

                    state <=
                        S_WAIT_WR_DONE;

                end

            end


            if(ddr_wr_error)
            begin

                error <=
                    1'b1;


                stage_valid <=
                    1'b0;


                rd_pop_pending <=
                    1'b0;


                state <=
                    S_ERROR;

            end

        end



        /*====================================================
            等待这一行真正写入 DDR 完成

            wr_done 的语义：

            当前 write task 的最后 burst
            收到成功 B response。
        ====================================================*/

        S_WAIT_WR_DONE:
        begin

            block_req <=
                1'b1;
            if(ddr_wr_error)
            begin
                error <= 1'b1;
                state <= S_ERROR;
            end

            else if(wr_done)
            begin

                /*
                    整个矩形最后一行
                */

                if(
                    row_cnt
                    ==
                    height_r - 1'b1
                )
                begin

                    state <=
                        S_DONE;

                end


                /*
                    继续下一行
                */

                else
                begin

                    row_cnt <=
                        row_cnt + 1'b1;


                    /*
                        Framebuffer stride：

                        1920 * 2
                        = 3840 byte

                        所以下一行不是：

                            + width*2

                        而是：

                            + framebuffer stride
                    */

                    src_row_addr <=
                        src_row_addr
                        +
                        src_stride_r;


                    dst_row_addr <=
                        dst_row_addr
                        +
                        dst_stride_r;


                    state <=
                        S_ROW_START;

                end

            end

        end



        /*====================================================
            整个 Block Copy 完成
        ====================================================*/

        S_DONE:
        begin

            block_req <=
                1'b0;


            done <=
                1'b1;


            state <=
                S_WAIT_RELEASE;

        end



        /*====================================================
            防止 block_en 长时间为1导致重复执行

            等调用者把 block_en 拉低，
            才重新武装。
        ====================================================*/

        S_WAIT_RELEASE:
        begin

            block_req <=
                1'b0;


            if(!block_en)
            begin

                state <=
                    S_IDLE;

            end

        end
        S_ERROR:
        begin
            block_req <= 1'b0;

            if(!block_en)
                state <= S_IDLE;
        end


        default:
        begin

            state <=
                S_IDLE;


            block_req <=
                1'b0;


            block_start <=
                1'b0;


            done <=
                1'b0;


            stage_valid <=
                1'b0;


            rd_pop_pending <=
                1'b0;

        end


        endcase

    end

end



/*============================================================
    DDR Read Buffer

    一次只读取“一行”。

    row_beats_r 单位：
        128-bit AXI beat
============================================================*/

ddr_rd_buffer #(
    .RUNTIME_BURST(RUNTIME_BURST),
    .AXI_DATA_WIDTH    (AXI_DATA_WIDTH),
    .AXI_ADDR_WIDTH    (AXI_ADDR_WIDTH),
    .RD_FIFO_DEPTH     (RD_FIFO_DEPTH),
    .BURST_LEN         (BURST_LEN)
)
u_ddr_rd_buffer
(
    .single_beat_mode(single_beat_mode),
    .axi_clk           (clk),
    .axi_clk_rst_n     (rst_n),

    .start             (block_start),

    .start_addr        (src_row_addr),

    .burst_len         (row_beats_r),


    /*
        不再自己制造 rd_fifo_clk。

        整个 Block Copy 都使用同一个 clk。
    */

    .rd_fifo_rdclk     (clk),

    .rd_fifo_rst_p     (~rst_n),

    .rd_fifo_rdvalid   (rd_fifo_rdvalid),

    .rd_fifo_rddata    (rd_fifo_rddata),

    .rd_fifo_rdempty   (rd_fifo_rdempty),

    .rd_fifo_rden      (rd_fifo_rden),


    .bank_sw_ack       (rd_sw_ack),

    .bank_sw           (rd_sw),


    .arid              (arid),

    .araddr            (araddr),

    .arlen             (arlen),

    .arsize            (arsize),

    .arburst           (arburst),

    .arlock            (arlock),

    .arvalid           (arvalid),

    .arapcmd           (arapcmd),

    .arqos             (arqos),

    .arready           (arready),

    .arprot            (arprot),

    .arcache           (arcache),


    .rid               (rid),

    .rdata             (rdata),

    .rlast             (rlast),

    .rvalid            (rvalid),

    .rready            (rready),

    .rresp             (rresp),


    .wr_fifo_rd_data_test
                       (wr_fifo_rd_data_test)
);



/*============================================================
    DDR Write Buffer

    同样一次只写当前一行。
============================================================*/

ddr_wr_buffer #(
    .RUNTIME_BURST(RUNTIME_BURST),
    .AXI_DATA_WIDTH    (AXI_DATA_WIDTH),
    .AXI_ADDR_WIDTH    (AXI_ADDR_WIDTH),
    .WR_FIFO_DEPTH     (WR_FIFO_DEPTH),
    .BURST_LEN         (BURST_LEN)
)
u_ddr_wr_buffer
(
    .single_beat_mode(single_beat_mode),
    .axi_clk           (clk),

    .rst_n             (rst_n),


    .wr_start          (block_start),

    .start_addr        (dst_row_addr),

    .total_burst_len   (row_beats_r),


    .bank_sw_ack       (wr_sw_ack),

    .bank_sw           (wr_sw),


    .test_cnt          (test_cnt),


    /*
        同样不要生成新的 FIFO 时钟。
    */

    .wr_fifo_rst_p     (~rst_n),

    .wr_fifo_wrclk     (clk),

    .wr_fifo_wren      (wr_fifo_wren),

    .wr_fifo_wrfull    (wr_fifo_wrfull),

    .wr_fifo_wrdata    (wr_fifo_wrdata),


    .awid              (awid),

    .awaddr            (awaddr),

    .awlen             (awlen),

    .awsize            (awsize),

    .awburst           (awburst),

    .awcache           (awcache),

    .awlock            (awlock),

    .awvalid           (awvalid),

    .awcobuf           (awcobuf),

    .awapcmd           (awapcmd),

    .awallstrb         (awallstrb),

    .awprot            (awprot),

    .awqos             (awqos),

    .awready           (awready),


    .wdata             (wdata),

    .wstrb             (wstrb),

    .wlast             (wlast),

    .wvalid            (wvalid),

    .wready            (wready),


    .bid               (bid),

    .bvalid            (bvalid),

    .bresp             (s_axi_bresp),

    .bready            (bready),


    .wr_done           (wr_done),

    .wr_error          (ddr_wr_error)
);



/*============================================================
    Debug
============================================================*/

assign test_wdata =
        wdata[31:0];


assign test_rdata =
        rdata[31:0];


endmodule
