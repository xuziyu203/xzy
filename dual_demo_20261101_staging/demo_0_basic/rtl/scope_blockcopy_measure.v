// Passive core-clock observer. Never drives ready, start, grant or pipeline state.
// Workload contract: FULL BLIT, one background RECT + 64 palette BLITs, each 64x64 RGB565.
module scope_blockcopy_measure #(
    parameter integer PULSE_CYCLES=1000
)(
    input wire clk, rst_n,
    input wire [11:0] reg_addr, input wire reg_write,
    input wire [31:0] reg_wdata, output reg [31:0] reg_rdata,
    input wire config_idle,
    input wire frame_start, frame_eligible, frame_abort,
    input wire task_start, task_done, task_valid, last_command,
    input wire arvalid, arready, input wire [7:0] arlen,
    input wire rvalid, rready, rlast, input wire [1:0] rresp, input wire [3:0] rid,
    input wire awvalid, awready, input wire [7:0] awlen,
    input wire wvalid, wready, wlast,
    input wire bvalid, bready, input wire [1:0] bresp, input wire [3:0] bid,
    output wire window_enabled,
    output reg scope_out
);
localparam [31:0] ABI=32'h53430101;
reg enabled, busy_mode, selected, batch_active, task_active, frame_bad;
assign window_enabled=enabled;
reg [6:0] started, finished;
reg [8:0] read_left, write_left;
reg write_pending;
reg [9:0] task_reads, task_writes;
reg [15:0] pulse_left;
reg [31:0] completed, rises, aborted, faults, last_cycles;
reg [31:0] elapsed, last_interval, prev_done_tick, task_cycles, last_task_cycles;
reg have_done;
reg [63:0] ticks;
reg [31:0] r_beats, w_beats;
reg [7:0] max_arlen, max_awlen;
reg [31:0] snap [0:21];
integer i;
wire ar_fire=arvalid&&arready, r_fire=rvalid&&rready;
wire aw_fire=awvalid&&awready, w_fire=wvalid&&wready, b_fire=bvalid&&bready;
wire cfg_write=reg_write&&reg_addr==12'h204&&reg_wdata[31:16]==16'h5343;
wire disable_now=cfg_write&&!reg_wdata[0];
wire arm_now=cfg_write&&reg_wdata[0]&&config_idle&&pulse_left==0;
wire bus_bad_now=(ar_fire && read_left!=0) ||
 (r_fire && (read_left==0 || rid!=0 || rresp!=0 || rlast!=(read_left==1))) ||
 (aw_fire && write_pending) ||
 (w_fire && (!write_pending || write_left==0 || wlast!=(write_left==1))) ||
 (b_fire && (!write_pending || write_left!=0 || bid!=0 || bresp!=0));
wire done_ok=enabled&&selected&&!frame_bad&&batch_active&&task_active&&task_done&&
 finished==63&&started==64&&last_command&&read_left==0&&!write_pending&&
 !frame_abort&&!bus_bad_now && task_reads==512 && task_writes==512;
wire sequence_bad=(task_start && (!task_valid||task_active||started>=64)) ||
 (task_done && (!task_active || read_left!=0 || write_pending ||
                task_reads!=512 || task_writes!=512 ||
               (last_command!=(finished==63))));
always @* begin
 reg_rdata=0;
 if(reg_addr==12'h200)reg_rdata=ABI;
 else if(reg_addr==12'h204)reg_rdata={16'h5343,11'd0,config_idle,frame_bad,batch_active,busy_mode,enabled};
 else if(reg_addr==12'h258)reg_rdata=100000000;
  else if(reg_addr==12'h25C)reg_rdata=PULSE_CYCLES;
  else if(reg_addr==12'h260)reg_rdata=524288;
  else if(reg_addr>=12'h20C && reg_addr<=12'h260 && reg_addr[1:0]==0)
   reg_rdata=snap[(reg_addr-12'h20C)>>2];
end
always @(posedge clk or negedge rst_n) begin
 if(!rst_n)begin
  enabled<=0;busy_mode<=0;scope_out<=0;selected<=0;batch_active<=0;task_active<=0;frame_bad<=0;
  started<=0;finished<=0;read_left<=0;write_left<=0;write_pending<=0;pulse_left<=0;task_reads<=0;task_writes<=0;
  completed<=0;rises<=0;aborted<=0;faults<=0;last_cycles<=0;
  elapsed<=0;last_interval<=0;prev_done_tick<=0;have_done<=0;ticks<=0;
  task_cycles<=0;last_task_cycles<=0;r_beats<=0;w_beats<=0;max_arlen<=0;max_awlen<=0;
  for(i=0;i<22;i=i+1)snap[i]<=0;
 end else begin
  // Atomic, non-clearing snapshot; all counters have the same sample edge.
  if(reg_write&&reg_addr==12'h208&&reg_wdata==1)begin
   snap[0]<=completed;snap[1]<=rises;snap[2]<=aborted;snap[3]<=last_cycles;
   snap[4]<=0;snap[5]<=0;snap[6]<=last_interval;
   snap[7]<=ticks[31:0];snap[8]<=ticks[63:32];snap[9]<=0;snap[10]<=0;
   snap[11]<={27'd0,config_idle,frame_bad,batch_active,busy_mode,enabled};snap[12]<=faults;
   snap[13]<=0;snap[14]<=0;snap[15]<=r_beats;snap[16]<=w_beats;
   snap[17]<={16'd0,max_awlen,max_arlen};snap[18]<=last_task_cycles;
   // 19..21 are constant capability registers, not snapshot flops.
  end
  // A completed pulse keeps its full width even if software disarms next.
   if(!busy_mode&&pulse_left!=0)begin
    pulse_left<=pulse_left-1;
    if(pulse_left==1)scope_out<=0;
   end
   if(arm_now)begin
   enabled<=1;busy_mode<=reg_wdata[1];scope_out<=0;selected<=0;batch_active<=0;task_active<=0;frame_bad<=0;
   started<=0;finished<=0;read_left<=0;write_left<=0;write_pending<=0;pulse_left<=0;task_reads<=0;task_writes<=0;
   completed<=0;rises<=0;aborted<=0;faults<=0;last_cycles<=0;
   elapsed<=0;last_interval<=0;prev_done_tick<=0;have_done<=0;ticks<=0;
   task_cycles<=0;last_task_cycles<=0;r_beats<=0;w_beats<=0;max_arlen<=0;max_awlen<=0;
  end else if(disable_now)begin
   enabled<=0;selected<=0;batch_active<=0;task_active<=0;
    if(busy_mode)scope_out<=0;
   if(batch_active)aborted<=aborted+1;
  end else if(enabled)begin
   ticks<=ticks+1;
   if(batch_active)elapsed<=elapsed+1;
   if(task_active)task_cycles<=task_cycles+1;
   if(frame_start)begin
    selected<=frame_eligible;frame_bad<=0;batch_active<=0;task_active<=0;
    started<=0;finished<=0;elapsed<=0;task_cycles<=0;read_left<=0;write_left<=0;write_pending<=0;
    if(batch_active)begin aborted<=aborted+1;faults<=faults+1;end
    if(busy_mode)scope_out<=0;
   end else if(selected&&!frame_bad)begin
    if(ar_fire)begin read_left<={1'b0,arlen}+1;if(arlen>max_arlen)max_arlen<=arlen;end
    if(r_fire)begin read_left<=read_left-1;r_beats<=r_beats+1;task_reads<=task_reads+1;end
    if(aw_fire)begin write_pending<=1;write_left<={1'b0,awlen}+1;if(awlen>max_awlen)max_awlen<=awlen;end
    if(w_fire)begin write_left<=write_left-1;w_beats<=w_beats+1;task_writes<=task_writes+1;end
    if(b_fire)write_pending<=0;
    if(frame_abort||bus_bad_now||sequence_bad)begin
     frame_bad<=1;batch_active<=0;task_active<=0;
     if(batch_active)aborted<=aborted+1;
     faults<=faults+1;
     if(busy_mode)scope_out<=0;
    end else begin
     if(task_start)begin
      started<=started+1;task_active<=1;task_reads<=0;task_writes<=0;
      if(!batch_active)begin
       batch_active<=1;elapsed<=0;task_cycles<=0;
       if(busy_mode)begin scope_out<=1;rises<=rises+1;end
      end
     end
     if(task_done)begin finished<=finished+1;task_active<=0;end
     if(done_ok)begin
      batch_active<=0;selected<=0;
      // A pulse cannot collide for this workload: >=32768 accepted R beats
      // per frame at 1 beat/cycle >> 1000 pulse cycles. Fail visibly if violated.
      if(!busy_mode&&pulse_left!=0)begin frame_bad<=1;faults<=faults+1;aborted<=aborted+1;end
      else begin
       completed<=completed+1;last_cycles<=elapsed+1;last_task_cycles<=task_cycles+1;
       if(have_done)last_interval<=ticks[31:0]-prev_done_tick;
       have_done<=1;prev_done_tick<=ticks[31:0];
       if(busy_mode)scope_out<=0;
       else begin scope_out<=1;pulse_left<=PULSE_CYCLES;rises<=rises+1;end
      end
     end
    end
   end
  end
 end
end
endmodule
