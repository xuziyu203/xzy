module framebuffer_manager
#(
    parameter FB0_BASE = 32'h0000_0000,
    parameter FB1_BASE = 32'h0040_0000
)
(
    input  wire        clk,
    input  wire        rst_n,

    // 1: 双缓冲
    // 0: 单缓冲，读写都使用FB0
    input  wire        double_buffer_en,

    // Back Buffer一整帧已经绘制完成
    input  wire        render_done,

    // HDMI来到安全换帧位置
    input  wire        frame_boundary,

    output reg  [31:0] front_buffer_base,
    output reg  [31:0] back_buffer_base,

    output reg         swap_pulse
);


reg swap_pending;
reg double_buffer_en_d;


/*==============================================================
    Double Buffer Manager
==============================================================*/

always @(posedge clk or negedge rst_n)
begin
    if(!rst_n)
    begin
        // 默认地址
        front_buffer_base <= FB0_BASE;
        back_buffer_base  <= FB1_BASE;

        swap_pending      <= 1'b0;
        swap_pulse        <= 1'b0;

        double_buffer_en_d <= 1'b0;
    end
    else
    begin
        double_buffer_en_d <= double_buffer_en;

        // 默认仅保持一个周期
        swap_pulse <= 1'b0;


        /*======================================================
            单缓冲模式

            HDMI     -> FB0
            Renderer -> FB0
        ======================================================*/
        if(!double_buffer_en)
        begin
            front_buffer_base <= FB0_BASE;
            back_buffer_base  <= FB0_BASE;

            swap_pending <= 1'b0;
        end


        /*======================================================
            双缓冲刚刚开启

            Front -> FB0
            Back  -> FB1
        ======================================================*/
        else if(double_buffer_en && !double_buffer_en_d)
        begin
            front_buffer_base <= FB0_BASE;
            back_buffer_base  <= FB1_BASE;

            swap_pending <= 1'b0;
        end


        /*======================================================
            正常双缓冲模式
        ======================================================*/
        else
        begin

            // Back Buffer已经准备好
            if(render_done)
                swap_pending <= 1'b1;


            // HDMI到达安全换帧点
            //
            // render_done若刚好和frame_boundary同周期到达，
            // 也允许直接交换，避免白等一整帧。
            if(frame_boundary &&
               (swap_pending || render_done))
            begin
                front_buffer_base <= back_buffer_base;
                back_buffer_base  <= front_buffer_base;

                swap_pending <= 1'b0;
                swap_pulse   <= 1'b1;
            end

        end

    end
end


endmodule