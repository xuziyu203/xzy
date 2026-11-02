// Oscilloscope frame probe (core_clk, 100 MHz). Passive observer of the enemy compositor frame state; drives nothing but
// the probe pin. Output is registered and reset low (same asynchronous reset as the compositor).
//   MODE 0  FRAME_BUSY        high while the compositor composes one frame: from the accepted claim (IDLE -> RESET: all commands
//                             and descriptor tables of the frame already submitted) to compose completion (the only entry into
//                             PRESENT: all 2073600 pixels committed and the DDR writes drained = the whole frame is in the back
//                             buffer, usable for display). An aborted frame (DRAIN/CLEAN/IDLE before PRESENT) drops it without a
//                             completion. Excludes the CPU preparation (in pressure mode it overlaps the previous frame's compose),
//                             the wait for the display present and the release / drain after it.
//   MODE 1  FRAME_DONE_PULSE  one high pulse of exactly PULSE_CYCLES core_clk cycles on every compose completion (the falling edge of
//                             MODE 0). Rising-edge interval = interval between completed frames. Each completed frame is presented
//                             exactly once (the next claim waits for present + release), so the pulse rate = completed-frame FPS.
// The probe is one cycle later than the compositor state (registered); both edges are delayed alike.
module frame_probe #(parameter MODE=0, parameter integer PULSE_CYCLES=1000)(
 input wire clk, rst_n,
 input wire composing,      // compositor state is a compose state (not IDLE / PRESENT / SHOWN / DRAIN / CLEAN)
 input wire in_present,     // compositor state == PRESENT
 output reg probe
);
 reg present_q;
 reg [15:0] left;
 wire done=in_present && !present_q;   // one cycle: compose completion (entry into PRESENT)
 always @(posedge clk or negedge rst_n) begin
  if(!rst_n) begin probe<=1'b0;present_q<=1'b0;left<=16'd0; end
  else begin
   present_q<=in_present;
   if(MODE==0) probe<=composing;
   else begin
    if(done) begin left<=PULSE_CYCLES-1;probe<=1'b1; end
    else if(left!=16'd0) left<=left-16'd1;
    else probe<=1'b0;
   end
  end
 end
endmodule
