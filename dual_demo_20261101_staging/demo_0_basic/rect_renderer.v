module rect_renderer #(
    parameter WIDTH          = 1920,
    parameter HEIGHT         = 1080,
    parameter AXI_ADDR_WIDTH = 33,
    parameter FB_BASE_ADDR   = 0,
    parameter SOLID_FILL_ENABLE = 0
)
(
    input  wire                         clk,
    input  wire                         rst_n,

    // 启动整个矩形绘制
    input  wire                         start,

    // packer / framebuffer 可以接收 pixel
    input  wire                         ready,

    // 当前这一行 DDR 已经真正写完
    // 后面应由 ddr_wr_buffer 的写完成信号提供
    input  wire                         wr_done,
    output wire [2:0] wr_start_offset,

    //========================================================
    // 矩形参数
    //========================================================

    input  wire [11:0]                  center_x,
    input  wire [11:0]                  center_y,

    input  wire [11:0]                  rect_width,
    input  wire [11:0]                  rect_height,

    input  wire [15:0]                  rect_color,
    input  wire [11:0]                  surface_width,
    input  wire [11:0]                  surface_height,
    input  wire [31:0]                  surface_stride_bytes,


    //========================================================
    // 当前这一行 DDR 写参数
    //========================================================

    output wire [AXI_ADDR_WIDTH-1:0]    wr_start_addr,
    output wire [23:0]                  wr_len,

    // 当前行写任务启动
    output reg                          wr_start,


    //========================================================
    // pixel stream
    //========================================================

    output wire                         vs,
    output wire                         hs,
    output wire                         de,

    output wire [15:0]                  vin,


    //========================================================
    // 整个矩形完成
    //========================================================

    output reg                          done
);

wire [11:0] surface_width_i = (surface_width == 12'd0) ? WIDTH : surface_width;
wire [11:0] surface_height_i = (surface_height == 12'd0) ? HEIGHT : surface_height;
wire [31:0] surface_stride_i = (surface_stride_bytes == 32'd0) ?
                                (WIDTH * 2) : surface_stride_bytes;



/*============================================================
    start 上升沿检测
============================================================*/

reg start_d;


always @(posedge clk or negedge rst_n)
begin

    if(!rst_n)
        start_d <= 1'b0;

    else
        start_d <= start;

end


wire start_pulse;

assign start_pulse = start & ~start_d;



/*============================================================
    矩形边界计算
============================================================*/

wire [11:0] half_width;
wire [11:0] half_height;


assign half_width  = rect_width  >> 1;
assign half_height = rect_height >> 1;



/*
    对奇数宽高：

    左边使用 floor(width/2)
    右边使用 ceil(width/2)

    保证正常情况下：

        right - left = rect_width
*/

wire [12:0] right_half;
wire [12:0] bottom_half;


assign right_half =
        (rect_width + 1'b1) >> 1;


assign bottom_half =
        (rect_height + 1'b1) >> 1;



/*============================================================
    左边界 / 上边界

    防止 unsigned 下溢
============================================================*/

wire [11:0] calc_left;
wire [11:0] calc_top;


assign calc_left =
        (center_x >= half_width) ?
        center_x - half_width :
        12'd0;


assign calc_top =
        (center_y >= half_height) ?
        center_y - half_height :
        12'd0;



/*============================================================
    右边界 / 下边界
============================================================*/

wire [12:0] right_temp;
wire [12:0] bottom_temp;


assign right_temp =
        center_x + right_half;


assign bottom_temp =
        center_y + bottom_half;



wire [11:0] calc_right;
wire [11:0] calc_bottom;


assign calc_right =
        (right_temp >= surface_width_i) ?
        surface_width_i :
        right_temp[11:0];


assign calc_bottom =
        (bottom_temp >= surface_height_i) ?
        surface_height_i :
        bottom_temp[11:0];



/*============================================================
    裁剪后的实际矩形宽高
============================================================*/

wire [11:0] calc_width;
wire [11:0] calc_height;


assign calc_width =
        calc_right - calc_left;


assign calc_height =
        calc_bottom - calc_top;



/*============================================================
    启动后锁存矩形参数

    防止绘制过程中外部参数变化
============================================================*/

reg [11:0] left_r;
reg [11:0] top_r;

reg [11:0] width_r;
reg [11:0] height_r;
reg [11:0] surface_width_r;
reg [11:0] surface_height_r;
reg [31:0] surface_stride_r;

reg [15:0] color_r;

// Constant geometry: widen BOTH operands before multiplication.
// 1920 * 1080 = 2,073,600 pixels, safely represented by wr_len[23:0].
localparam [31:0] FULL_FRAME_WIDTH  = WIDTH;
localparam [31:0] FULL_FRAME_HEIGHT = HEIGHT;
localparam [63:0] FULL_FRAME_PIXELS_WIDE =
        {32'd0, FULL_FRAME_WIDTH} * {32'd0, FULL_FRAME_HEIGHT};
localparam [23:0] FULL_FRAME_PIXELS = FULL_FRAME_PIXELS_WIDE[23:0];

// Use clipped, latched geometry only; partial/viewport RECTs stay per-row.
wire full_screen_solid;
assign full_screen_solid =
        SOLID_FILL_ENABLE &&
        (FULL_FRAME_PIXELS_WIDE <= 64'd16777215) &&
        (left_r == 12'd0) && (top_r == 12'd0) &&
        (width_r == surface_width_r) && (height_r == surface_height_r);



/*============================================================
    行 / pixel 计数
============================================================*/

// 当前是矩形内部第几行
reg [11:0] row_cnt;

// 当前这一行第几个 pixel
reg [11:0] pixel_cnt;



/*============================================================
    状态机

    IDLE
        等待整个矩形启动

    ROW_CMD
        当前行地址、长度已经准备好
        wr_start=1
        等待 packer 真正接受这一行

    ROW_WRITE
        连续发送当前行 pixel

    WAIT_WR_DONE
        pixel 已经全部送入 packer/FIFO
        但不能马上修改 row_cnt
        必须等 DDR 当前行真正写完
============================================================*/

localparam S_IDLE         = 2'd0;
localparam S_ROW_CMD      = 2'd1;
localparam S_ROW_WRITE    = 2'd2;
localparam S_WAIT_WR_DONE = 2'd3;


reg [1:0] state;



/*============================================================
    主状态机
============================================================*/

always @(posedge clk or negedge rst_n)
begin

    if(!rst_n)
    begin

        state <= S_IDLE;


        left_r   <= 12'd0;
        top_r    <= 12'd0;

        width_r  <= 12'd0;
        height_r <= 12'd0;
        surface_width_r <= WIDTH;
        surface_height_r <= HEIGHT;
        surface_stride_r <= WIDTH * 2;

        color_r  <= 16'd0;


        row_cnt   <= 12'd0;
        pixel_cnt <= 12'd0;


        wr_start <= 1'b0;

        done <= 1'b0;

    end


    else
    begin

        case(state)


        /*====================================================
            IDLE

            等待新的矩形任务
        ====================================================*/

        S_IDLE:
        begin

            wr_start <= 1'b0;

            row_cnt   <= 12'd0;
            pixel_cnt <= 12'd0;
            done <= 1'b0;
            if(start_pulse &&
               (calc_width  != 0) &&
               (calc_height != 0))
            begin

                // 锁存这次矩形参数
                left_r   <= calc_left;
                top_r    <= calc_top;

                width_r  <= calc_width;
                height_r <= calc_height;
                surface_width_r <= surface_width_i;
                surface_height_r <= surface_height_i;
                surface_stride_r <= surface_stride_i;

                color_r  <= rect_color;


                


                // 准备启动第0行
                state <= S_ROW_CMD;

            end

        end



        /*====================================================
            ROW_CMD

            当前：

                row_cnt
                wr_start_addr
                wr_len

            都已经稳定。

            wr_start保持为1，
            直到packer真正进入PACK并返回ready。

            不能只打一拍，否则可能被packer错过。
        ====================================================*/

        S_ROW_CMD:
        begin

            wr_start <= 1'b1;

            pixel_cnt <= 12'd0;


            /*
                ready=1 说明 packer 已进入 PACK，
                当前行启动成功。

                下一拍正式发送pixel。
            */

            if(ready)
            begin

                wr_start <= 1'b0;

                // Fast packer owns pixel generation after accepting the task.
                // Both task paths wait for the final OKAY B response.
                state <= SOLID_FILL_ENABLE ? S_WAIT_WR_DONE : S_ROW_WRITE;

            end

        end



        /*====================================================
            ROW_WRITE

            连续发送当前行 pixel。

            ready=0：
                pixel_cnt保持

            ready=1：
                当前pixel成功被接受
        ====================================================*/

        S_ROW_WRITE:
        begin

            wr_start <= 1'b0;


            if(ready)
            begin

                /*
                    当前是这一行最后一个pixel
                */

                if(pixel_cnt == width_r - 1'b1)
                begin

                    pixel_cnt <= 12'd0;


                    /*
                        注意：

                        到这里仅代表：

                        当前行pixel
                            ↓
                        packer
                            ↓
                        FIFO

                        已经全部送完。

                        并不代表DDR已经写完。

                        所以不能row_cnt++。

                        必须进入WAIT_WR_DONE。
                    */

                    state <= S_WAIT_WR_DONE;

                end


                else
                begin

                    pixel_cnt <= pixel_cnt + 1'b1;

                end

            end

        end



        /*====================================================
            WAIT_WR_DONE

            等当前这一行真正完成DDR AXI写。

            理想wr_done来源：

                最后一burst
                +
                BVALID
                +
                BREADY
                +
                BRESP == OKAY
        ====================================================*/

        S_WAIT_WR_DONE:
        begin

            wr_start <= 1'b0;


            if(wr_done)
            begin

                /*
                    整个矩形最后一行
                */

                // A full-screen task covers every row; never launch a second task.
                if(full_screen_solid || (row_cnt == height_r - 1'b1))
                begin

                    row_cnt <= 12'd0;

                    done <= 1'b1;

                    state <= S_IDLE;

                end


                /*
                    进入下一行
                */

                else
                begin

                    /*
                        只有现在才允许地址变化

                        row_cnt改变后：

                        current_y
                        row_pixel_index
                        wr_start_addr

                        才全部跳到下一行。
                    */

                    row_cnt <= row_cnt + 1'b1;

                    state <= S_ROW_CMD;

                end

            end

        end



        default:
        begin

            state    <= S_IDLE;

            wr_start <= 1'b0;

        end


        endcase

    end

end



/*============================================================
    当前屏幕 Y 坐标

    current_y =
        矩形top + 当前矩形内部行号
============================================================*/

wire [12:0] current_y;


assign current_y =
        top_r + row_cnt;



/*============================================================
    当前行第一个pixel在framebuffer中的pixel index

    index =
        y * WIDTH + x
============================================================*/

wire [31:0] row_pixel_index;
localparam PIXELS_PER_BEAT = 8;

wire [2:0] pixel_offset;

assign pixel_offset =
        left_r[2:0];


wire [11:0] aligned_left;

assign aligned_left =
        left_r - pixel_offset;
assign row_pixel_index =
        current_y * surface_width_r + aligned_left;
assign wr_start_offset =
        pixel_offset;



/*============================================================
    RGB565：

        1 pixel = 2 Byte

    DDR byte address：

        BASE
        +
        (y * WIDTH + x) * 2
============================================================*/

assign wr_start_addr =
        FB_BASE_ADDR +
        (full_screen_solid ? 32'd0 :
         (current_y * surface_stride_r + (aligned_left << 1)));



/*============================================================
    完整全屏Solid Fill使用单任务；其余DDR写任务仍只写一行

    单位：pixel

    packer后续再换算：

        ddr_len =
        ceil(wr_len / 8)
============================================================*/

assign wr_len =
        full_screen_solid ?
            (surface_width_r * surface_height_r) : {12'd0, width_r};



/*============================================================
    VS

    这里已经不是传统视频VS语义。

    当前意义：

        当前出现一个新的“行写任务”。

    在ROW_CMD期间保持高，
    直到packer进入PACK。
============================================================*/

assign vs =
        (state == S_ROW_CMD);



/*============================================================
    HS

    当前暂时与行命令一致。

    后面如果不用，可以去掉。
============================================================*/

assign hs =
        (state == S_ROW_CMD);



/*============================================================
    DE

    只有ROW_WRITE状态pixel才有效。
============================================================*/

assign de =
        (state == S_ROW_WRITE);



/*============================================================
    Pixel data

    当前整个矩形填充一种颜色。
============================================================*/

assign vin =
        (state == S_ROW_WRITE) ?
        color_r :
        16'h0000;



endmodule
