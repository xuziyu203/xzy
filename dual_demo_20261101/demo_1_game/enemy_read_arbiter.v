// Single outstanding AR/R transaction. Selection is latched BEFORE ARVALID
// and held through the final accepted R beat, including all backpressure.
// HDMI wins normally; a continuously waiting compositor wins after 8 HDMI
// bursts. An accepted transaction is never preempted.
module enemy_read_arbiter (
 input wire clk,rst_n,
 input wire [31:0] h_addr, input wire [7:0] h_len,
 input wire h_valid, output wire h_ready,
 output wire [127:0] h_data, output wire [1:0] h_resp,
 output wire [3:0] h_id, output wire h_last,h_rvalid,input wire h_rready,
 input wire [31:0] c_addr, input wire [7:0] c_len,
 input wire c_valid, output wire c_ready,
 output wire [127:0] c_data, output wire [1:0] c_resp,
 output wire [3:0] c_id, output wire c_last,c_rvalid,input wire c_rready,
 output reg [31:0] m_addr, output reg [7:0] m_len,
 output wire m_valid,input wire m_ready,
 input wire [127:0] m_data,input wire [1:0] m_resp,
 input wire [3:0] m_id,input wire m_last,m_rvalid,output wire m_rready,
 output wire idle,
 // Passive monitor: selected AR lease, before downstream ARREADY.
 output wire perf_c_selected
);
 reg [1:0] state;
 reg consumer;
 reg [8:0] remaining;
 reg [3:0] hdmi_run;
 wire choose_c = c_valid && (!h_valid || hdmi_run==8);
 assign idle=state==0;
 assign perf_c_selected=state==1 && consumer;
 assign m_valid=state==1;
 assign h_ready=state==1 && !consumer && m_ready;
 assign c_ready=state==1 && consumer && m_ready;
 assign h_rvalid=state==2 && !consumer && m_rvalid;
 assign c_rvalid=state==2 && consumer && m_rvalid;
 assign m_rready=state==2 && (consumer ? c_rready : h_rready);
 assign h_data=m_data;assign c_data=m_data;
 assign h_resp=m_resp;assign c_resp=m_resp;
 assign h_id=m_id;assign c_id=m_id;
 assign h_last=m_last;assign c_last=m_last;
 always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin state<=0;consumer<=0;m_addr<=0;m_len<=0;remaining<=0;hdmi_run<=0;end
  else case(state)
   0: if(h_valid || c_valid) begin
    consumer<=choose_c;m_addr<=choose_c?c_addr:h_addr;
    m_len<=choose_c?c_len:h_len;state<=1;
   end
   1: if(m_ready) begin
    remaining<={1'b0,m_len}+1'b1;state<=2;
    if(consumer || !c_valid) hdmi_run<=0;
    else if(hdmi_run<8) hdmi_run<=hdmi_run+1'b1;
   end
   2: if(m_rvalid && m_rready) begin
    if(remaining!=0) remaining<=remaining-1'b1;
    // Malformed early/missing RLAST cannot release another consumer's lease.
    if(m_last && remaining<=1) state<=0;
   end
   default: state<=0;
  endcase
 end
endmodule
