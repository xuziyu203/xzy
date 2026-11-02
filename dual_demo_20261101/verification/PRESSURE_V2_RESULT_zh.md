# 实际持续压力运行：超过五分钟，长时间固定方向仍未覆盖

实际板测使用最终 GHDB1 ELF 和原 FRAME_DONE_PULSE bit。为避免默认十条生命因机体碰撞提前 GameOver，单轮正常重开后仅在 RAM 将生命 5→1000，随后关闭 JTAG，以只接收 COM15 的方式记录；固件/玩法默认值未修改。必须归类为**测试生命扩展压力**，不是默认十条生命自然生存五分钟。

`board/capture_pressure_v2` 共 418 TEL/JOY 对，零解析错误、无未配对 JOY、无不完整末行。先前 93 个 LIGHT 窗口不计压力。真实 S 且 hp>0 的连续段：

**2026-11-01 20:14:23.852–20:19:47.846，325 个样本，主机单调时钟跨度 323.9946136 秒。**

用户已实际操作，不依靠“ok”回复推断测试完成。首个 S 窗口跨轻量→压力转换，只有 24 完成帧，性能统计排除该窗口；其余 324 个完整压力窗口结果如下。

| 项目 | 实测 |
|---|---:|
| 显示完成帧 | 19440，逐窗口均 60 帧 |
| >25 ms 完成帧间隔 | 0 |
| 最长完成帧间隔 | 16703 µs |
| 最大 compose / pipelined prep | 15625 / 12759 µs |
| 实际弹数 / 组数峰值 | 2439 / 850 |
| glow 峰值 | 15 |
| 新 ADC pair | 20538 |
| 停采 / invalid / unarmed / latched / fault 窗口 | 全部 0 |
| 自动重试 / bus recover 命令 | 0 / 0 |
| 最长驱动服务间隔 | 5184 µs（不是 ADC pair 最大间隔） |
| 最新 pair 年龄的每秒快照最大值 | 16 ms（不是每个 pair 的精确最大年龄） |
| 压力段生命 | 919→860，全程活跃 |
| 压力段护盾吸收 / 分数增长 | +9027 / +36700 |

与历史同 M-144 稳定压力 120 个完整窗口对照：历史也逐窗 60 帧、长间隔 0、最大间隔 16697 µs，实际弹数峰值 2422/组数 850。本次未见完成帧率下降；最长间隔多 6 µs，场景内容不完全相同，不能据此量化为严格 CPU 开销对比。

压力期间有效输入快照覆盖左右、四种斜向、回中；有 89 个有效 fire 请求快照和 5 次从方向回中的相邻快照。方向原始数值、发布序号和显示坐标均有相应变化；护盾吸收持续增长，玩家击杀分数持续增长，结合下面的收尾 effect 计数确认特效路径实际运行。

**长时间固定方向未完成**：要求追加的 35 秒固定方向未出现在遥测中。压力最长同方向相邻快照仅 6.992 秒（左下 8 个采样点），LIGHT 阶段最长 7.999 秒（左 9 点）。每秒 JOY 不能证明两点之间没有短暂释放；不能将这些快照改写为 35 秒保持通过，也不能用用户回复代替数据。

压力记录停止后，20:20:18–20:20:19 的 `board/post_pressure_snapshot_v1` 普通 RAM 只读检查 PASS：fx_enabled=1、fx_session=13；先读取 fx_spawned=376，稍后读取已显示累计 spawned=377、dropped=60、merged=2、evicted=0；shield_toggles=1、absorbed=9879、combat_pb_kills=337。已显示 flags=65539，即 MANUAL(3) 且可见，证明人工护盾切换/吸收和特效显示路径确实运行。配置变量 shield_mode=0 是新局默认配置，不等于当前 shield.mode；两者含义不同。

这些是**测试结束后的本局累计计数，各组不是同一时刻的原子快照**。因此 spawned 376 与稍后显示 377 不矛盾，不能把377解释为五分钟内精确爆炸数量（其中包括火花，且还含轻量阶段及停止采集后运行）。组2显示 active=1，稍后组5的分类 counts=0，亦可能在两组之间自然过期；不能拼成同一帧。dropped=60/merged=2 如实保留。读取时输入 valid/armed=1、unlatched，CPU 恢复 running。所有收尾 JTAG 扰动都排除在上述压力统计之外。

随后 `board/restore_normal_lives_v6` 实际 PASS1/1：从 pressure round13、生命850，经原 GameOver/逻辑 KEY3 路径进入 round14/LIGHT；RAM 及原显示确认均为生命10、over=0。之后连续 2.991832 秒 UART 健康窗口和 RAM 健康检查通过，CPU running、自有 OpenOCD exit0。`capture_restore_normal` 最后两秒再次看到 jp100/fault0/valid armed1/fr60，之后双方采集/JTAG资源已释放。生命因正常游戏受伤继续减少，不是被固定在10；临时1000生命设置已通过原新局路径清除，没有修改生产 ELF。

结论：核心摇杆修复、十轮逻辑重开、五分钟测试生命扩展压力/输入稳定/帧率通过；**35秒固定方向保持未覆盖，默认十条生命自然存活五分钟未测**。不得把这一有明确范围的通过写成所有动作和全部自然玩法验收完成。

原始证据 SHA-256：

- `board/capture_pressure_v2/raw.txt`：`10c118b00b3016c77ac103a66151b1e8655d89952b6cc118d836b844d3f32ec8`
- `board/capture_pressure_v2/rx.jsonl`：`b65088a73a2df5f35e644fa1efa4f1b816b80ce05f495bd5239deb64e70b6cc9`
- `board/post_pressure_snapshot_v1/results.json`：`3507627b97b79d436ec7c02fda13e024087799c1c5f399d9be1d24faef4c4414`
- `board/restore_normal_lives_v6/results.json`：`f70a9e2440c71cb59e023ad1513fd8c12fd62a55bcb6f2e0962e7b0889ba7d40`
- `board/capture_restore_normal/raw.txt`：`40c7698ac91fad0fee24ee5a4b20fda13d84058e045c4ba06e525eef89c0f1ed`
- 详细分析：`evidence_audit/pressure_v2_final/analysis.json`。已独立以原始 JSONL 再次统计行数、跨度、全部帧/输入判据，与分析一致。
