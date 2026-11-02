module axi_control
(
    input  wire         clk,
    input  wire         rst_n,


    //==========================================================
    // S2 : HDMI Reader
    // Read Only
    // ID_WIDTH = 4
    //==========================================================

    input  wire [3:0]   hdmi_arid,
    input  wire [31:0]  hdmi_araddr,
    input  wire [7:0]   hdmi_arlen,
    input  wire [2:0]   hdmi_arsize,
    input  wire [1:0]   hdmi_arburst,
    input  wire [3:0]   hdmi_arcache,
    input  wire [2:0]   hdmi_arprot,
    input  wire [3:0]   hdmi_arqos,
    input  wire         hdmi_arvalid,
    output wire         hdmi_arready,

    output wire [3:0]   hdmi_rid,
    output wire [127:0] hdmi_rdata,
    output wire [1:0]   hdmi_rresp,
    output wire         hdmi_rlast,
    output wire         hdmi_rvalid,
    input  wire         hdmi_rready,


    //==========================================================
    // S1 : Random Write
    // Write Only
    //==========================================================

    input  wire [3:0]   rw_awid,
    input  wire [31:0]  rw_awaddr,
    input  wire [7:0]   rw_awlen,
    input  wire [2:0]   rw_awsize,
    input  wire [1:0]   rw_awburst,
    input  wire [3:0]   rw_awcache,
    input  wire [2:0]   rw_awprot,
    input  wire [3:0]   rw_awqos,
    input  wire         rw_awvalid,
    output wire         rw_awready,

    input  wire [127:0] rw_wdata,
    input  wire [15:0]  rw_wstrb,
    input  wire         rw_wlast,
    input  wire         rw_wvalid,
    output wire         rw_wready,

    output wire [3:0]   rw_bid,
    output wire [1:0]   rw_bresp,
    output wire         rw_bvalid,
    input  wire         rw_bready,


    //==========================================================
    // S0 : Block Copy
    // Read + Write
    //==========================================================

    // Write Address
    input  wire [3:0]   block_awid,
    input  wire [31:0]  block_awaddr,
    input  wire [7:0]   block_awlen,
    input  wire [2:0]   block_awsize,
    input  wire [1:0]   block_awburst,
    input  wire [3:0]   block_awcache,
    input  wire [2:0]   block_awprot,
    input  wire [3:0]   block_awqos,
    input  wire         block_awvalid,
    output wire         block_awready,

    // Write Data
    input  wire [127:0] block_wdata,
    input  wire [15:0]  block_wstrb,
    input  wire         block_wlast,
    input  wire         block_wvalid,
    output wire         block_wready,

    // Write Response
    output wire [3:0]   block_bid,
    output wire [1:0]   block_bresp,
    output wire         block_bvalid,
    input  wire         block_bready,

    // Read Address
    input  wire [3:0]   block_arid,
    input  wire [31:0]  block_araddr,
    input  wire [7:0]   block_arlen,
    input  wire [2:0]   block_arsize,
    input  wire [1:0]   block_arburst,
    input  wire [3:0]   block_arcache,
    input  wire [2:0]   block_arprot,
    input  wire [3:0]   block_arqos,
    input  wire         block_arvalid,
    output wire         block_arready,

    // Read Data
    output wire [3:0]   block_rid,
    output wire [127:0] block_rdata,
    output wire [1:0]   block_rresp,
    output wire         block_rlast,
    output wire         block_rvalid,
    input  wire         block_rready,


    //==========================================================
    // M0 : DDR3 Controller
    // 当前 DDR AXI ID_WIDTH = 4
    //==========================================================

    // Write Address
    output wire [3:0]   m_axi_awid,
    output wire [31:0]  m_axi_awaddr,
    output wire [7:0]   m_axi_awlen,
    output wire [2:0]   m_axi_awsize,
    output wire [1:0]   m_axi_awburst,
    output wire [3:0]   m_axi_awcache,
    output wire [2:0]   m_axi_awprot,
    output wire [3:0]   m_axi_awqos,

    // DDR3 Controller只有1-bit LOCK
    output wire         m_axi_awlock,

    output wire         m_axi_awvalid,
    input  wire         m_axi_awready,

    // Write Data
    output wire [127:0] m_axi_wdata,
    output wire [15:0]  m_axi_wstrb,
    output wire         m_axi_wlast,
    output wire         m_axi_wvalid,
    input  wire         m_axi_wready,

    // Write Response
    input  wire [3:0]   m_axi_bid,
    input  wire [1:0]   m_axi_bresp,
    input  wire         m_axi_bvalid,
    output wire         m_axi_bready,

    // Read Address
    output wire [3:0]   m_axi_arid,
    output wire [31:0]  m_axi_araddr,
    output wire [7:0]   m_axi_arlen,
    output wire [2:0]   m_axi_arsize,
    output wire [1:0]   m_axi_arburst,
    output wire [3:0]   m_axi_arcache,
    output wire [2:0]   m_axi_arprot,
    output wire [3:0]   m_axi_arqos,

    // DDR3 Controller只有1-bit LOCK
    output wire         m_axi_arlock,

    output wire         m_axi_arvalid,
    input  wire         m_axi_arready,

    // Read Data
    input  wire [3:0]   m_axi_rid,
    input  wire [127:0] m_axi_rdata,
    input  wire [1:0]   m_axi_rresp,
    input  wire         m_axi_rlast,
    input  wire         m_axi_rvalid,
    output wire         m_axi_rready
);


//==============================================================
// Interconnect S端打包总线
//
// 3 ports × ID_WIDTH(4)
//==============================================================

wire [2:0]   s_axi_awvalid;
wire [95:0]  s_axi_awaddr;
wire [5:0]   s_axi_awlock;
wire [2:0]   s_axi_awready;

wire [2:0]   s_axi_arvalid;
wire [95:0]  s_axi_araddr;
wire [5:0]   s_axi_arlock;
wire [2:0]   s_axi_arready;

wire [2:0]   s_axi_wvalid;
wire [2:0]   s_axi_wlast;
wire [11:0]  s_axi_wid;
wire [2:0]   s_axi_wready;

wire [2:0]   s_axi_bready;
wire [5:0]   s_axi_bresp;
wire [11:0]  s_axi_bid;
wire [2:0]   s_axi_bvalid;

wire [2:0]   s_axi_rready;
wire [11:0]  s_axi_rid;
wire [383:0] s_axi_rdata;
wire [5:0]   s_axi_rresp;
wire [2:0]   s_axi_rvalid;
wire [2:0]   s_axi_rlast;

wire [383:0] s_axi_wdata;
wire [47:0]  s_axi_wstrb;

wire [8:0]   s_axi_awprot;
wire [11:0]  s_axi_awcache;
wire [11:0]  s_axi_awqos;
wire [8:0]   s_axi_awuser;

wire [11:0]  s_axi_arqos;
wire [11:0]  s_axi_arcache;

wire [11:0]  s_axi_arid;
wire [8:0]   s_axi_arsize;
wire [23:0]  s_axi_arlen;
wire [5:0]   s_axi_arburst;
wire [8:0]   s_axi_arprot;
wire [8:0]   s_axi_aruser;

wire [11:0]  s_axi_awid;
wire [5:0]   s_axi_awburst;
wire [23:0]  s_axi_awlen;
wire [8:0]   s_axi_awsize;

wire [8:0]   s_axi_wuser;

wire [8:0]   s_axi_buser_unused;
wire [8:0]   s_axi_ruser_unused;


//==============================================================
// Interconnect M端中不被DDR使用的信号
//==============================================================

wire [1:0] ip_m_axi_awlock;
wire [1:0] ip_m_axi_arlock;

wire [2:0] ip_m_axi_awuser;
wire [2:0] ip_m_axi_aruser;
wire [2:0] ip_m_axi_wuser;

wire [3:0] ip_m_axi_awregion;
wire [3:0] ip_m_axi_arregion;


// DDR3 Controller没有 USER 返回通道
wire [2:0] ip_m_axi_ruser;
wire [2:0] ip_m_axi_buser;

assign ip_m_axi_ruser = 3'b000;
assign ip_m_axi_buser = 3'b000;


// DDR不使用LOCK事务
assign m_axi_awlock = 1'b0;
assign m_axi_arlock = 1'b0;


//==============================================================
// AW CHANNEL
//
// S2 = HDMI Reader（不写）
// S1 = Random Write
// S0 = Block Copy
//==============================================================

assign s_axi_awvalid = {
    1'b0,
    rw_awvalid,
    block_awvalid
};

assign s_axi_awaddr = {
    32'd0,
    rw_awaddr,
    block_awaddr
};

assign s_axi_awid = {
    4'd0,
    rw_awid,
    block_awid
};

assign s_axi_awlen = {
    8'd0,
    rw_awlen,
    block_awlen
};

assign s_axi_awsize = {
    3'd0,
    rw_awsize,
    block_awsize
};

assign s_axi_awburst = {
    2'd0,
    rw_awburst,
    block_awburst
};

assign s_axi_awcache = {
    4'd0,
    rw_awcache,
    block_awcache
};

assign s_axi_awprot = {
    3'd0,
    rw_awprot,
    block_awprot
};

assign s_axi_awqos = {
    4'd0,
    rw_awqos,
    block_awqos
};


// 当前所有源均不使用LOCK事务
assign s_axi_awlock = 6'd0;


// USER不用
assign s_axi_awuser = 9'd0;


//==============================================================
// W CHANNEL
//==============================================================

assign s_axi_wvalid = {
    1'b0,
    rw_wvalid,
    block_wvalid
};

assign s_axi_wlast = {
    1'b0,
    rw_wlast,
    block_wlast
};

assign s_axi_wdata = {
    128'd0,
    rw_wdata,
    block_wdata
};

assign s_axi_wstrb = {
    16'd0,
    rw_wstrb,
    block_wstrb
};


// AXI4没有WID。
// Efinix wrapper虽然保留端口，但固定为0。
assign s_axi_wid = 12'd0;


// USER不用
assign s_axi_wuser = 9'd0;


//==============================================================
// B CHANNEL
//==============================================================

assign s_axi_bready = {
    1'b0,
    rw_bready,
    block_bready
};


//==============================================================
// AR CHANNEL
//
// S2 = HDMI Reader
// S1 = Random Write（不读）
// S0 = Block Copy
//==============================================================

assign s_axi_arvalid = {
    hdmi_arvalid,
    1'b0,
    block_arvalid
};

assign s_axi_araddr = {
    hdmi_araddr,
    32'd0,
    block_araddr
};

assign s_axi_arid = {
    hdmi_arid,
    4'd0,
    block_arid
};

assign s_axi_arlen = {
    hdmi_arlen,
    8'd0,
    block_arlen
};

assign s_axi_arsize = {
    hdmi_arsize,
    3'd0,
    block_arsize
};

assign s_axi_arburst = {
    hdmi_arburst,
    2'd0,
    block_arburst
};

assign s_axi_arcache = {
    hdmi_arcache,
    4'd0,
    block_arcache
};

assign s_axi_arprot = {
    hdmi_arprot,
    3'd0,
    block_arprot
};

assign s_axi_arqos = {
    hdmi_arqos,
    4'd0,
    block_arqos
};

assign s_axi_arlock = 6'd0;

assign s_axi_aruser = 9'd0;


//==============================================================
// R CHANNEL
//==============================================================

assign s_axi_rready = {
    hdmi_rready,
    1'b0,
    block_rready
};


//==============================================================
// S2 HDMI RETURN
//==============================================================

assign hdmi_arready =
    s_axi_arready[2];

assign hdmi_rid =
    s_axi_rid[11:8];

assign hdmi_rdata =
    s_axi_rdata[383:256];

assign hdmi_rresp =
    s_axi_rresp[5:4];

assign hdmi_rvalid =
    s_axi_rvalid[2];

assign hdmi_rlast =
    s_axi_rlast[2];


//==============================================================
// S1 RANDOM WRITE RETURN
//==============================================================

assign rw_awready =
    s_axi_awready[1];

assign rw_wready =
    s_axi_wready[1];

assign rw_bid =
    s_axi_bid[7:4];

assign rw_bresp =
    s_axi_bresp[3:2];

assign rw_bvalid =
    s_axi_bvalid[1];


//==============================================================
// S0 BLOCK COPY WRITE RETURN
//==============================================================

assign block_awready =
    s_axi_awready[0];

assign block_wready =
    s_axi_wready[0];

assign block_bid =
    s_axi_bid[3:0];

assign block_bresp =
    s_axi_bresp[1:0];

assign block_bvalid =
    s_axi_bvalid[0];


//==============================================================
// S0 BLOCK COPY READ RETURN
//==============================================================

assign block_arready =
    s_axi_arready[0];

assign block_rid =
    s_axi_rid[3:0];

assign block_rdata =
    s_axi_rdata[127:0];

assign block_rresp =
    s_axi_rresp[1:0];

assign block_rvalid =
    s_axi_rvalid[0];

assign block_rlast =
    s_axi_rlast[0];


//==============================================================
// AXI INTERCONNECT IP
//
// 3 Slave Ports
// 1 Master Port
// AXI4
// DATA_WIDTH = 128
// ADDR_WIDTH = 32
// ID_WIDTH   = 4
//==============================================================

axi_interconnect_3to1 axi_interconnect_3to1_inst
(
    .rst_n              (rst_n),
    .clk                (clk),


    //==========================================================
    // Slave side
    //==========================================================

    .s_axi_awvalid      (s_axi_awvalid),
    .s_axi_awaddr       (s_axi_awaddr),
    .s_axi_awlock       (s_axi_awlock),
    .s_axi_awready      (s_axi_awready),

    .s_axi_arvalid      (s_axi_arvalid),
    .s_axi_araddr       (s_axi_araddr),
    .s_axi_arlock       (s_axi_arlock),
    .s_axi_arready      (s_axi_arready),

    .s_axi_wvalid       (s_axi_wvalid),
    .s_axi_wlast        (s_axi_wlast),
    .s_axi_wid          (s_axi_wid),
    .s_axi_wready       (s_axi_wready),

    .s_axi_bready       (s_axi_bready),
    .s_axi_bresp        (s_axi_bresp),
    .s_axi_bid          (s_axi_bid),
    .s_axi_bvalid       (s_axi_bvalid),

    .s_axi_rready       (s_axi_rready),
    .s_axi_rid          (s_axi_rid),
    .s_axi_rdata        (s_axi_rdata),
    .s_axi_rresp        (s_axi_rresp),
    .s_axi_rvalid       (s_axi_rvalid),
    .s_axi_rlast        (s_axi_rlast),

    .s_axi_wdata        (s_axi_wdata),
    .s_axi_wstrb        (s_axi_wstrb),

    .s_axi_awprot       (s_axi_awprot),
    .s_axi_awcache      (s_axi_awcache),
    .s_axi_awqos        (s_axi_awqos),
    .s_axi_awuser       (s_axi_awuser),

    .s_axi_arqos        (s_axi_arqos),
    .s_axi_arcache      (s_axi_arcache),

    .s_axi_arid         (s_axi_arid),
    .s_axi_arsize       (s_axi_arsize),
    .s_axi_arlen        (s_axi_arlen),
    .s_axi_arburst      (s_axi_arburst),
    .s_axi_arprot       (s_axi_arprot),

    .s_axi_awid         (s_axi_awid),
    .s_axi_awburst      (s_axi_awburst),
    .s_axi_awlen        (s_axi_awlen),
    .s_axi_awsize       (s_axi_awsize),

    .s_axi_aruser       (s_axi_aruser),
    .s_axi_wuser        (s_axi_wuser),

    .s_axi_buser        (s_axi_buser_unused),
    .s_axi_ruser        (s_axi_ruser_unused),


    //==========================================================
    // Master side -> DDR3
    //==========================================================

    .m_axi_awvalid      (m_axi_awvalid),
    .m_axi_awaddr       (m_axi_awaddr),
    .m_axi_awlock       (ip_m_axi_awlock),
    .m_axi_awready      (m_axi_awready),

    .m_axi_arvalid      (m_axi_arvalid),
    .m_axi_araddr       (m_axi_araddr),
    .m_axi_arlock       (ip_m_axi_arlock),
    .m_axi_arready      (m_axi_arready),

    .m_axi_wvalid       (m_axi_wvalid),
    .m_axi_wlast        (m_axi_wlast),
    .m_axi_wready       (m_axi_wready),

    .m_axi_bready       (m_axi_bready),
    .m_axi_bresp        (m_axi_bresp),
    .m_axi_bid          (m_axi_bid),
    .m_axi_bvalid       (m_axi_bvalid),

    .m_axi_rready       (m_axi_rready),
    .m_axi_rid          (m_axi_rid),
    .m_axi_rdata        (m_axi_rdata),
    .m_axi_rresp        (m_axi_rresp),
    .m_axi_rvalid       (m_axi_rvalid),
    .m_axi_rlast        (m_axi_rlast),

    .m_axi_wdata        (m_axi_wdata),
    .m_axi_wstrb        (m_axi_wstrb),

    .m_axi_awprot       (m_axi_awprot),

    .m_axi_awid         (m_axi_awid),
    .m_axi_awburst      (m_axi_awburst),
    .m_axi_awlen        (m_axi_awlen),
    .m_axi_awsize       (m_axi_awsize),
    .m_axi_awcache      (m_axi_awcache),
    .m_axi_awqos        (m_axi_awqos),

    .m_axi_awuser       (ip_m_axi_awuser),
    .m_axi_awregion     (ip_m_axi_awregion),

    .m_axi_arprot       (m_axi_arprot),
    .m_axi_arburst      (m_axi_arburst),
    .m_axi_arlen        (m_axi_arlen),
    .m_axi_arsize       (m_axi_arsize),
    .m_axi_arcache      (m_axi_arcache),
    .m_axi_arqos        (m_axi_arqos),

    .m_axi_aruser       (ip_m_axi_aruser),
    .m_axi_arregion     (ip_m_axi_arregion),
    .m_axi_arid         (m_axi_arid),

    .m_axi_wuser        (ip_m_axi_wuser),

    .m_axi_ruser        (ip_m_axi_ruser),
    .m_axi_buser        (ip_m_axi_buser)
);


endmodule
