// Diagnostic counterfactual only. Not applied to supplied production sources.
reg vsync_core_d0, vsync_core_d1, vsync_core_d2;
reg rd_quiescent_core_d0, rd_quiescent_core_d1;
reg scanout_descriptor_release;
reg scanout_boundary_pending;
wire surface_frame_boundary = vsync_core_d1 && !vsync_core_d2;
wire surface_commit_boundary = scanout_boundary_pending && vsync_core_d1 &&
                               rd_quiescent_core_d1 && framebuffer_rd_quiescent &&
                               !framebuffer_read_error;
always @(posedge core_clk or negedge test_rst_n) begin
  if(!test_rst_n) begin
    vsync_core_d0<=0; vsync_core_d1<=0; vsync_core_d2<=0;
    rd_quiescent_core_d0<=0;rd_quiescent_core_d1<=0;
    scanout_descriptor_release<=0;scanout_boundary_pending<=0;
  end else begin
    vsync_core_d0<=ch0_vs;vsync_core_d1<=vsync_core_d0;vsync_core_d2<=vsync_core_d1;
    rd_quiescent_core_d0<=framebuffer_rd_quiescent;rd_quiescent_core_d1<=rd_quiescent_core_d0;
    if(surface_frame_boundary) scanout_boundary_pending<=1;
    else if(!vsync_core_d1 || surface_commit_boundary) scanout_boundary_pending<=0;
    scanout_descriptor_release<=surface_commit_boundary;
  end
end
