# 两套 FPGA 时序结果

两套均使用既有 bit；本轮摇杆修复没有 RTL 改动，没有新 FPGA 综合、布局布线或 STA 结果。固件编译通过和实际 60 FPS 不能当作时序通过。

| 工程 | 所用 bit 的既有结果 | 与之前版本的关系 |
|---|---|---|
| 基础 scope demo | **FAIL**；183 个负 setup 路径、0 个负 hold；core→HDMI -1.711 ns，HDMI→core -0.724 ns | 既有负时钟对仍在，先前分别 -1.260/-0.671 ns，数值有所退化；这是本轮接管前的 scope bit 构建结果 |
| 游戏 GHDPP FRAME_DONE_PULSE | **FAIL**；19 个负 setup 端点，WNS -1.139 ns，TNS -11.469 ns；最差 hold +0.027 ns | 相对更早 GHDAB 的端点集合相同；其 WNS -0.792 ns、TNS -10.356 ns。数值变化来自先前 FRAME_DONE_PULSE 构建，不由本次固件修复引入 |

原始精简结构化数据：`basic_timing_summary.json`、`game_timing_comparison.json`。未放宽时钟或约束，也未把这些 FAIL 隐藏或改成 PASS。
