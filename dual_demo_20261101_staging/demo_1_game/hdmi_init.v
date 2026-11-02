module hdmi_init #(
    parameter WIDTH  = 1920,
    parameter HEIGHT = 1080
)
(
    input  wire        clk,
    input  wire        rst_n,
    input wire wr_done,
    // 初始化颜色 RGB565
    input  wire [15:0] init_color,

    // 启动一次整帧初始化
    input  wire        init_start,

    // 下游 ready
    input  wire        ready,

    // 像素流输出
    output wire        vs,
    output wire        hs,
    output wire        de,
    output wire [15:0] vin,
    output reg init_bank_sel,
    // 一帧初始化完成
    output reg         init_done,
    output reg init_hdmi_wr_start,
    // 本次写入像素数
    output wire [23:0] wr_len
);


/*============================================================
    一帧像素数量

    1920 * 1080 = 2,073,600 pixel
============================================================*/

localparam [23:0] FRAME_PIXELS = WIDTH * HEIGHT;

assign wr_len = FRAME_PIXELS;


/*============================================================
    start 上升沿检测

    防止 init_start 长时间保持高电平时重复初始化
============================================================*/

reg init_start_d;

always @(posedge clk or negedge rst_n)
begin
    if(!rst_n)
        init_start_d <= 1'b0;
    else
        init_start_d <= init_start;
end

wire init_start_pulse;

assign init_start_pulse = init_start & ~init_start_d;


/*============================================================
    扫描状态
============================================================*/

reg [11:0] x_cnt;
reg [11:0] y_cnt;


reg [2:0] state;


localparam S_IDLE          = 3'd0;
localparam S_INIT_FB0      = 3'd1;
localparam S_WAIT_FB0_DONE = 3'd2;
localparam S_INIT_FB1      = 3'd3;
localparam S_WAIT_FB1_DONE = 3'd4;
localparam S_DONE          = 3'd5;

wire active;

assign active =
        (state == S_INIT_FB0) ||
        (state == S_INIT_FB1);

/*============================================================
    整帧初始化

    active = 0:
        等待 init_start

    active = 1:
        输出 init_color
        只有 ready=1 时坐标才前进

    最后一个 pixel 成功传输后:
        active    = 0
        init_done = 1
============================================================*/
always @(posedge clk or negedge rst_n)
begin
    if(!rst_n)
    begin
        x_cnt         <= 12'd0;
        y_cnt         <= 12'd0;

        init_done     <= 1'b0;
        init_bank_sel <= 1'b0;
        init_hdmi_wr_start <= 'd0;
        state         <= S_IDLE;
    end

    else
    begin
        case(state)

            /*================================================
                等待初始化启动
            =================================================*/
            S_IDLE:
            begin
                x_cnt         <= 12'd0;
                y_cnt         <= 12'd0;

                init_done     <= 1'b0;
                init_bank_sel <= 1'b0;     // 首先初始化 FB0
               
                if(init_start_pulse)
                begin
                    state <= S_INIT_FB0;
                    init_hdmi_wr_start <= 'd1;
                end
                else
                    init_hdmi_wr_start <= 'd0;
            end


            /*================================================
                初始化 FB0
            =================================================*/
            S_INIT_FB0:
            begin
                init_bank_sel <= 1'b0;
                init_done     <= 1'b0;
                init_hdmi_wr_start <= 'd0;
                if(ready)
                begin
                    // 整帧最后一个 pixel
                    if((x_cnt == WIDTH - 1) &&
                       (y_cnt == HEIGHT - 1))
                    begin
                        x_cnt <= 12'd0;
                        y_cnt <= 12'd0;

                        state <= S_WAIT_FB0_DONE;
                    end

                    // 当前行最后一个 pixel
                    else if(x_cnt == WIDTH - 1)
                    begin
                        x_cnt <= 12'd0;
                        y_cnt <= y_cnt + 1'b1;
                    end

                    // 当前行继续
                    else
                    begin
                        x_cnt <= x_cnt + 1'b1;
                    end
                end
            end


            /*================================================
                等待 FB0 真正写入 DDR 完成
            =================================================*/
            S_WAIT_FB0_DONE:
            begin
                init_done <= 1'b0;

                if(wr_done)
                begin
                    init_bank_sel <= 1'b1;     // 下一块 FB1
                    x_cnt         <= 12'd0;
                    y_cnt         <= 12'd0;
                    init_hdmi_wr_start <= 'd1;
                    state <= S_INIT_FB1;
                end
                else init_hdmi_wr_start <= 'd0;
            end


            /*================================================
                初始化 FB1
            =================================================*/
            S_INIT_FB1:
            begin
                init_bank_sel <= 1'b1;
                init_done     <= 1'b0;
                init_hdmi_wr_start <= 'd0;
                if(ready)
                begin
                    // 整帧最后一个 pixel
                    if((x_cnt == WIDTH - 1) &&
                       (y_cnt == HEIGHT - 1))
                    begin
                        x_cnt <= 12'd0;
                        y_cnt <= 12'd0;

                        state <= S_WAIT_FB1_DONE;
                    end

                    // 当前行最后一个 pixel
                    else if(x_cnt == WIDTH - 1)
                    begin
                        x_cnt <= 12'd0;
                        y_cnt <= y_cnt + 1'b1;
                    end

                    // 当前行继续
                    else
                    begin
                        x_cnt <= x_cnt + 1'b1;
                    end
                end
            end


            /*================================================
                等待 FB1 真正写入 DDR 完成
            =================================================*/
            S_WAIT_FB1_DONE:
            begin
                init_hdmi_wr_start <= 'd0;
                init_done <= 1'b0;

                if(wr_done)
                begin
                    init_done <= 1'b1;
                    state     <= S_DONE;
                end
            end


            /*================================================
                两个 framebuffer 均初始化完成
            =================================================*/
            S_DONE:
            begin
                init_done <= 1'b1;
                init_hdmi_wr_start <= 'd0;

                // 如果以后希望允许再次初始化，
                // 可以等 init_start 拉低后重新回 IDLE
                if(!init_start)
                begin
                    state <= S_IDLE;
                end
            end


            default:
            begin
                x_cnt         <= 12'd0;
                y_cnt         <= 12'd0;
                init_done     <= 1'b0;
                init_bank_sel <= 1'b0;
                init_hdmi_wr_start <= 'd0;
                state         <= S_IDLE;
            end

        endcase
    end
end

/*============================================================
    视频流输出
============================================================*/

// active 期间所有 pixel 都有效
assign de = active;


// 第一个 pixel 产生 VS
//
// 如果下游 ready=0，x/y保持0，
// 因此 VS 也会保持，直到下游开始接收
assign vs =
        active &&
        (x_cnt == 12'd0) &&
        (y_cnt == 12'd0);


// 每一行第一个 pixel
assign hs =
        active &&
        (x_cnt == 12'd0);


// 整帧全部填充指定颜色
assign vin =
        active ?
        init_color :
        16'h0000;


endmodule