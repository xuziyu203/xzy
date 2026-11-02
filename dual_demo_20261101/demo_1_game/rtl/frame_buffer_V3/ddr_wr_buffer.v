module ddr_wr_buffer #(
    parameter AXI_DATA_WIDTH = 128,
    parameter AXI_ADDR_WIDTH = 33,
    parameter WR_FIFO_DEPTH  = 1024,
    parameter AXI_STRB_WIDTH = AXI_DATA_WIDTH/8,

    // 注意：
    // BURST_LEN 是 AXI AWLEN 的最大值
    // 例如 127 表示最大 128 beats
    parameter BURST_LEN      = 8'd15,
    
    parameter WR_PAYLOAD_WIDTH = AXI_DATA_WIDTH + AXI_STRB_WIDTH      // 144
)
(
    input                                       axi_clk,
    input                                       rst_n,

    //==========================================================
    // Write command
    //==========================================================
    input                                       wr_start,
    input  [AXI_ADDR_WIDTH-1:0]                start_addr,

    // 单位：AXI beat，不是 pixel
    input  [24:0]                              total_burst_len,


    //==========================================================
    // 原工程保留接口
    //==========================================================
    input                                       bank_sw_ack,
    output wire                                 bank_sw,
    output wire [7:0]                           test_cnt,


    //==========================================================
    // Write FIFO
    //==========================================================
    input                                       wr_fifo_rst_p,

    input                                       wr_fifo_wrclk,
    input                                       wr_fifo_wren,

    output                                      wr_fifo_wrfull,

    input  [WR_PAYLOAD_WIDTH-1:0]              wr_fifo_wrdata,


    //==========================================================
    // AXI AW
    //==========================================================
    output [3:0]                                awid,

    output [AXI_ADDR_WIDTH-1:0]                awaddr,

    output [7:0]                                awlen,
    output [2:0]                                awsize,
    output [1:0]                                awburst,

    output                                      awlock,
    output reg                                  awvalid,
    output                                      awcobuf,
    output                                      awapcmd,
    output                                      awallstrb,
    output                                      awqos,

    output [3:0]                                awcache,
    output [2:0]                                awprot,

    input                                       awready,


    //==========================================================
    // AXI W
    //==========================================================
    output [AXI_DATA_WIDTH-1:0]                wdata,
    output [AXI_STRB_WIDTH-1:0]                wstrb,

    output wire                                 wlast,
    output reg                                  wvalid,

    input                                       wready,


    //==========================================================
    // AXI B
    //==========================================================
    input  [3:0]                                bid,

    input                                       bvalid,

    // 新增：
    // 必须从 DDR controller 的 BRESP 接进来
    input  [1:0]                                bresp,

    output wire                                 bready,


    //==========================================================
    // 当前整个 wr_start 命令完成
    //==========================================================

    // 最后一个 burst 收到 OKAY B response 后，
    // wr_done 拉高一个 clk
    output reg                                  wr_done,

    // BRESP 非 OKAY 时拉高一个 clk
    output reg                                  wr_error,
    // Same-clock owner handoff. Include delayed start so IDLE is not mistaken
    // for retirement between the request pulse and the synchronized command.
    output wire                                 wr_idle,
    output wire                                 wr_accept,
    output wire                                 wr_fifo_empty,


    //==========================================================
    // Debug
    //==========================================================
    output [33:0]                               addr_width_test,
    // TEMP_BG_PERF: passive condition tap; never used by writer control.
    output wire                                 bg_dbg_wait_fifo_insufficient,
    output [7:0]                                test_BURST_LEN
);



/*============================================================
    参数
============================================================*/

localparam WR_ADDR_SHIFT_BITS = $clog2(AXI_STRB_WIDTH);

localparam AXSIZE_WTH =
        $clog2(AXI_STRB_WIDTH);


/*
    BURST_LEN = 127

    实际最大 beat 数：

        127 + 1 = 128 beats
*/

localparam [8:0] MAX_BURST_BEATS =
        {1'b0, BURST_LEN} + 9'd1;



/*============================================================
    状态机
============================================================*/

localparam S_IDLE      = 3'd0;
localparam S_WAIT_FIFO = 3'd1;
localparam S_AW        = 3'd2;
localparam S_W         = 3'd3;
localparam S_WAIT_B    = 3'd4;

reg [2:0] state;



/*============================================================
    wr_start 上升沿同步

    保留原模块的两级方式
============================================================*/

reg [1:0] start_sync;


always @(posedge axi_clk or negedge rst_n)
begin

    if(!rst_n)
        start_sync <= 2'b00;

    else
        start_sync <= {start_sync[0], wr_start};

end


wire pos_sync;

assign wr_idle = (state == S_IDLE) && (start_sync == 0) && !wr_start;
assign wr_accept = (state == S_IDLE) && pos_sync;

assign pos_sync =
        (start_sync == 2'b01);



/*============================================================
    FIFO
============================================================*/

localparam WR_USEDW_WIDTH =
        $clog2(WR_FIFO_DEPTH);


wire [WR_USEDW_WIDTH:0] wr_fifo_rdusedw;

wire [WR_PAYLOAD_WIDTH-1:0]
        wr_fifo_rddata;

wire wr_fifo_rdempty;

wire wr_fifo_rden;

// The FIFO's Gray-count bus can be transiently unknown during the vendor
// reset handshake.  The synchronized empty flag is the authoritative
// ownership-release indication; the burst engine still uses rdusedw for its
// launch watermark below.
assign wr_fifo_empty = wr_fifo_rdempty;


DC_FIFO
#(
    .FIFO_MODE   ("Normal"),
    .DATA_WIDTH  (WR_PAYLOAD_WIDTH),
    .FIFO_DEPTH  (WR_FIFO_DEPTH)
)
u_wr_fifo
(
    .Reset      (wr_fifo_rst_p),

    .WrClk      (wr_fifo_wrclk),
    .WrEn       (wr_fifo_wren),
    .WrDNum     (),
    .WrFull     (wr_fifo_wrfull),
    .WrData     (wr_fifo_wrdata),

    .RdClk      (axi_clk),
    .RdEn       (wr_fifo_rden),
    .RdDNum     (wr_fifo_rdusedw),
    .RdEmpty    (wr_fifo_rdempty),
    .RdData     (wr_fifo_rddata)
);



/*============================================================
    当前写命令状态
============================================================*/

// 当前还剩多少 AXI beats 没有发送
reg [24:0] remaining_beats;


// 当前 burst 的 AXI 地址
reg [AXI_ADDR_WIDTH-1:0]
        current_addr;


// 当前 burst 实际 beat 数
// 范围 1~256
reg [8:0] burst_beats_r;


// AXI AWLEN
// AWLEN = beats - 1
reg [7:0] ddr_awlen;


// 当前 burst 是否为这个命令的最后一个 burst
reg last_burst_r;



/*============================================================
    当前地址到下一个 4 KiB 边界还有多少 byte
============================================================*/

/*
    特别注意：

    这里一定写成 13'd4096。

    不能写：

        12'd4096

    因为 12 bit 根本表示不了 4096。
*/

wire [12:0] bytes_to_4k;


assign bytes_to_4k =
        13'd4096 -
        {1'b0, current_addr[11:0]};



/*============================================================
    当前地址到 4 KiB 边界还能容纳多少 AXI beat
============================================================*/

wire [12:0] beats_to_4k;


assign beats_to_4k =
        bytes_to_4k >> WR_ADDR_SHIFT_BITS;



/*============================================================
    统一 burst 长度计算

    burst_beats = min
    (
        remaining_beats,
        MAX_BURST_BEATS,
        beats_to_4k
    )
============================================================*/

wire [24:0] max_burst_beats_ext;

wire [24:0] beats_to_4k_ext;


assign max_burst_beats_ext =
        {{16{1'b0}}, MAX_BURST_BEATS};


assign beats_to_4k_ext =
        {{12{1'b0}}, beats_to_4k};



wire [24:0] burst_limit_0;


assign burst_limit_0 =
        (remaining_beats < max_burst_beats_ext) ?
            remaining_beats :
            max_burst_beats_ext;



wire [24:0] burst_beats_calc;


assign burst_beats_calc =
        (burst_limit_0 < beats_to_4k_ext) ?
            burst_limit_0 :
            beats_to_4k_ext;



/*============================================================
    当前 burst 写完后地址增加量
============================================================*/

wire [AXI_ADDR_WIDTH-1:0]
        burst_addr_inc;


assign burst_addr_inc =
        (
            {{(AXI_ADDR_WIDTH-9){1'b0}},
              burst_beats_r}
        )
        << WR_ADDR_SHIFT_BITS;



/*============================================================
    写数据 beat 计数

    write_cnt表示当前正在发送第几个beat

        0
        1
        ...
        burst_beats_r-1
============================================================*/

reg [8:0] write_cnt;



/*============================================================
    WLAST

    只有：

        当前是W状态
        &&
        WVALID
        &&
        当前是最后一个beat

    才拉高
============================================================*/

assign wlast =
        (state == S_W) &&
        wvalid &&
        (write_cnt == burst_beats_r - 1'b1);



/*============================================================
    FIFO读取

    Normal FIFO：

    AW握手时：
        读取第一个数据

    每成功发送一个非最后beat：
        读取下一个数据
============================================================*/

// BEGIN TEMP_BG_PERF_TAP: observation only, no control consumers.
assign bg_dbg_wait_fifo_insufficient =
        (state == S_WAIT_FIFO) &&
        (remaining_beats != 25'd0) && (burst_beats_calc != 25'd0) &&
        (wr_fifo_rdusedw < burst_beats_calc);
// END TEMP_BG_PERF_TAP

assign wr_fifo_rden =

        (
            (state == S_AW) &&
            awvalid &&
            awready
        )

        ||

        (
            (state == S_W) &&
            wvalid &&
            wready &&
            !wlast
        );



/*============================================================
    AXI输出
============================================================*/

assign awaddr =
        current_addr;


assign awlen =
        ddr_awlen;


assign wdata =
        wr_fifo_rddata[127:0];
assign wstrb =
        wr_fifo_rddata[143:128];

assign awsize =
        AXSIZE_WTH;


assign awburst =
        2'b01;


assign awid =
        4'd0;


assign awlock =
        1'b0;


assign awapcmd =
        1'b0;


assign awcobuf =
        1'b0;


assign awallstrb =
        1'b0;


assign awqos =
        1'b0;


assign awcache =
        4'd3;


assign awprot =
        3'd2;



/*
    只有真的等待B response时，
    才告诉DDR：

        我可以收B响应。

    这样B响应的事务归属更明确。
*/

assign bready =
        (state == S_WAIT_B);



/*============================================================
    bank_sw

    暂时保留原含义：

    当前正在处理整个写任务的最后一个burst。
============================================================*/

assign bank_sw =
        last_burst_r;



/*============================================================
    Debug
============================================================*/

assign test_cnt =
        remaining_beats[7:0];


assign addr_width_test =
        AXI_ADDR_WIDTH;


assign test_BURST_LEN =
        BURST_LEN;



/*============================================================
    主状态机
============================================================*/

always @(posedge axi_clk or negedge rst_n)
begin

    if(!rst_n)
    begin

        state <= S_IDLE;


        remaining_beats <= 25'd0;

        current_addr <=
                {AXI_ADDR_WIDTH{1'b0}};


        burst_beats_r <= 9'd0;

        ddr_awlen <= 8'd0;


        last_burst_r <= 1'b0;

        write_cnt <= 9'd0;


        awvalid <= 1'b0;
        wvalid  <= 1'b0;


        wr_done  <= 1'b0;
        wr_error <= 1'b0;

    end


    else
    begin

        /*
            默认都是单周期脉冲
        */

        wr_done  <= 1'b0;
        wr_error <= 1'b0;



        case(state)


        /*====================================================
            IDLE

            等新的写命令
        ====================================================*/

        S_IDLE:
        begin

            awvalid <= 1'b0;
            wvalid  <= 1'b0;

            last_burst_r <= 1'b0;

            write_cnt <= 9'd0;


            if(pos_sync)
            begin

                /*
                    空任务
                */

                if(total_burst_len == 25'd0)
                begin

                    wr_done <= 1'b1;

                end


                else
                begin

                    current_addr <=
                            start_addr;


                    remaining_beats <=
                            total_burst_len;


                    state <=
                            S_WAIT_FIFO;

                end

            end

        end



        /*====================================================
            WAIT_FIFO

            根据：

                当前地址
                剩余beat数
                4KB边界
                最大burst长度

            动态算出当前burst。

            FIFO里面必须真正已经拥有完整burst，
            才允许发AW。
        ====================================================*/

        S_WAIT_FIFO:
        begin

            awvalid <= 1'b0;
            wvalid  <= 1'b0;


            if(
                (remaining_beats != 25'd0)
                &&
                (burst_beats_calc != 25'd0)
                &&
                (wr_fifo_rdusedw >= burst_beats_calc)
              )
            begin

                /*
                    锁存当前burst长度
                */

                burst_beats_r <=
                        burst_beats_calc[8:0];


                /*
                    AXI：

                        AWLEN = beats - 1
                */

                ddr_awlen <=
                        burst_beats_calc - 1'b1;


                /*
                    是否最后一个burst
                */

                last_burst_r <=
                        (remaining_beats ==
                         burst_beats_calc);


                /*
                    发AW
                */

                awvalid <= 1'b1;

                state <= S_AW;

            end

        end



        /*====================================================
            AW

            地址握手
        ====================================================*/

        S_AW:
        begin

            wvalid <= 1'b0;


            if(awvalid && awready)
            begin

                awvalid <= 1'b0;

                /*
                    AW握手的同时，
                    FIFO已经通过wr_fifo_rden
                    读第一个word。

                    下一拍进入W状态。
                */

                write_cnt <= 9'd0;

                wvalid <= 1'b1;

                state <= S_W;

            end


            else
            begin

                /*
                    AXI规则：

                    AWREADY没来以前，
                    AWVALID和AWADDR/AWLEN必须保持。
                */

                awvalid <= 1'b1;

            end

        end



        /*====================================================
            W

            发送当前burst所有beat
        ====================================================*/

        S_W:
        begin

            awvalid <= 1'b0;


            if(wvalid && wready)
            begin

                /*
                    当前就是最后一个beat
                */

                if(
                    write_cnt ==
                    burst_beats_r - 1'b1
                  )
                begin

                    /*
                        本拍同时：

                        WVALID=1
                        WREADY=1
                        WLAST=1

                        当前burst最后一个数据beat
                        成功完成。
                    */

                    wvalid <= 1'b0;

                    write_cnt <= 9'd0;


                    /*
                        不能直接开始下一burst。

                        必须等B response。
                    */

                    state <= S_WAIT_B;

                end


                else
                begin

                    write_cnt <=
                            write_cnt + 1'b1;

                end

            end

        end



        /*====================================================
            WAIT_B

            等DDR controller返回写响应
        ====================================================*/

        S_WAIT_B:
        begin

            awvalid <= 1'b0;
            wvalid  <= 1'b0;


            if(bvalid && bready)
            begin

                /*
                    OKAY
                */

                if(bresp == 2'b00)
                begin

                    /*
                        当前就是整个命令最后一个burst
                    */

                    if(last_burst_r)
                    begin

                        remaining_beats <=
                                25'd0;


                        last_burst_r <=
                                1'b0;


                        /*
                            整个wr_start命令真正完成
                        */

                        wr_done <= 1'b1;


                        state <= S_IDLE;

                    end


                    /*
                        当前命令还有后续burst
                    */

                    else
                    begin

                        /*
                            剩余beat
                        */

                        remaining_beats <=
                                remaining_beats -
                                burst_beats_r;


                        /*
                            地址向后推进：

                                burst_beats
                                *
                                bytes per beat
                        */

                        current_addr <=
                                current_addr +
                                burst_addr_inc;


                        last_burst_r <=
                                1'b0;


                        /*
                            重新计算下一burst
                        */

                        state <= S_WAIT_FIFO;

                    end

                end


                /*
                    AXI写响应错误
                */

                else
                begin

                    wr_error <= 1'b1;

                    last_burst_r <= 1'b0;

                    state <= S_IDLE;

                end

            end

        end



        default:
        begin

            state <= S_IDLE;

            awvalid <= 1'b0;
            wvalid  <= 1'b0;

            wr_done  <= 1'b0;
            wr_error <= 1'b0;

        end


        endcase

    end

end



endmodule
