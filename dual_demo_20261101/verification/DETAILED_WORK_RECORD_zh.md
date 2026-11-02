# 进阶游戏摇杆修复：根因、配对与验证记录

本文件为本轮进阶游戏修复与板测最终记录，已合并加载、素材上传、人工操作、十轮实际逻辑重开、超过五分钟压力及收尾恢复证据。**核心摇杆修复、编译/仿真/加载、十轮板上逻辑重开和持续压力时长/输入稳定/帧率通过，用户反馈正常；35秒固定方向保持未覆盖，默认十条生命自然存活五分钟未测。临时1000生命状态已清除，原新局显示生命恢复10且输入继续更新。交付状态为 RELEASE_WITH_LIMITATIONS，不宣称全部覆盖完成。**

## 版本与保留现场

- 进阶工程接管 HEAD：`bdd67ceb549ce56b1d0e2e768b307fe132058d4a`。
- 已保留原有修改的修复前 Git 检查点：`6f20230d0b4fbb2b433bfaf0efe5bdccda070f79`。
- 修复提交：`b74b29b78d5845fb2b64defe5fda4b2f16a7c0ac`（`fix: recover interrupted joystick reads with bounded retries`），仅四个生产文件。当前修复源在本轮 `candidate/`，没有把本次游戏修复套用到基础 demo。
- 基础 demo 接管 HEAD：`665157205c969c64b0de12848f598c2f94f67e62`；本轮仅依赖/哈希审计，功能未修改，板测未执行。
- 接管时的工作树、暂存差异、未跟踪列表及 3686 个现存跟踪文件 SHA 保存于 `before/`。用户已确认其他执行者停止修改并交接串口/JTAG。
- 接管时发现另一个基础 demo bit 加载过程，原故障寄存器现场可能已被覆盖。之后保存了 COM15 无响应、无复位 JTAG 暂停超时等接管记录，再加载已核对的游戏配对。没有将重载后的值称为原故障现场值。

## 真实故障日志与诊断界限

原 `handoff_20261101_192941` 的完整日志和加载/身份/素材记录已复制至 `evidence_audit/` 并逐文件核验，索引见 `preserved_manifest.json`、`load_evidence_manifest.json`。

原始 `tel_raw.txt` SHA-256：`968c982954be7185116e3ae5165f06dd42db47bb9c49948bbd305e93765ebd4c`。日志包含 GHDAX 身份和 1002 个一秒遥测窗口；分析见 `evidence_audit/incident_summary.json`。

| 观察 | 真实日志结果 |
|---|---|
| 首次故障 | 2026-11-01 19:19:19，第 3 次开局同一遥测窗口出现 BUS_RECOVER / RECOVERY_LINES |
| 前后输入 | 前一秒 100 组新 pair；故障窗口仍含 24 组此前样本，随后 455 个窗口新 pair 合计为 0 |
| 复现持续性 | 故障区间共 456 秒；411 次自动恢复，成功 0、失败 411；随后另 4 次开局仍未恢复 |
| 游戏运行 | 故障区间每个窗口 `fr=60`，最大完成帧间隔 16688 µs，最大摇杆服务间隔 584 µs |
| 按键/模式 | 日志可见后续重开及档位变化，支持游戏控制路径仍有响应；不能据此声称逐个实体按键均已实测 |
| 缺少的现场值 | 原遥测无原始 ADC X/Y、独立 SCL/SDA 电平和完整 I²C CSR；不能声称已直接测得当时 SDA 卡低或所有寄存器值 |

这些证据排除了“游戏整体停止”作为该段现象的解释，也不支持把它归为此前超过 5 ms 的服务饥饿。日志显示输入停更及无效/锁定，而非持续发布有效旧方向。真正物理线电平由下述模型复现提供解释，仍保留现场观测缺口。

历史有效压力运行只有生命大于 0 的连续 82 秒、40 秒两段；不能用包含 GameOver 的历史汇总替代本轮连续五分钟验收。

## 已复现的软件根因与最小修复

严格对照使用原固件 C、真实 I²C/GPIO RTL 和 ADS1115 从机模型，详见 `audit/RESULT_zh.md`。对照场景在转换值读取地址 `0x91` 的 ACK 完成、从机输出首个 0 位时，通过真实输入 gate 撤销 epoch，模拟结束/重开带来的输入授权变化。

旧流程最多等待 STOP 1 ms，随后 DROP 并撤销 I/O 权限。DROP 未清完整 TX/RX 通道等待；恢复时钟可能被遗留等待阻塞。控制器 10 ms 帧超时随后清除 `inFrame`，但从机仍可将 SDA 拉低。旧初始化只按 `inFrame` 决定是否恢复总线，于是跳过恢复，进入 `IDLE_LINES → RECOVERY_LINES → LATCH`，后续开局也重复这一失败路径。

基线 `audit/runs/20261101T114108Z_1793533268966194100` 的场景 62 在三秒内无新 pair，未恢复，无 SERVICE_GAP；**仿真**最终 CSR `0x40=0x400`、`0x44=0x04`，即 master 停止、inFrame=0、SCL 高、SDA 低。这是可重复的软件漏洞，与保存的板上故障顺序一致；上述 CSR 不是原故障板卡实读值。

候选固件的修复只涉及摇杆驱动和诊断输出/版本号，共四个生产文件：`joystick_ads1115.c/.h`、`main_single.c`、`build_id.h`。

1. DROP 后重建 TX_DATA/TX_ACK 通道，关闭 RX 监听并释放 override，消除遗留通道等待。
2. `inFrame` 或任一总线线低均触发有界恢复；要求 master idle、无 inFrame、SCL/SDA 均高才退出恢复。
3. 连续自动尝试最多三次。每次仍受三次 probe、2 秒故障 episode、2.5 秒 attempt 上限约束；耗尽后停止自动重试，KEY3/显式 RETRY 可重新授权，不需整机复位。
4. 两组新鲜 pair 才认定恢复成功。保留 40 ms 过期拒绝、过期方向清零及 100 ms 回中要求。
5. 遥测新增每秒 JOY 原始输入与恢复状态；原 TEL 和帧率测量保留。新增输出使用有界缓冲与非等待串口泵，未改变原玩法。

## 已执行的构建与离线验证

| 层次 | 已执行结果 | 证据 |
|---|---|---|
| 基线源码核对 | candidate 初始干净构建与原 GHDAX ELF 逐字节相同 | `PROGRESS.md`、构建记录 |
| 修复固件 | release 干净编译通过；stack 分析构建 ELF 与 release 相同 | `build/`、`offline_regression/stack/` |
| C + RTL 共仿真 | 52 配置读撤销、62 转换读中断、72 永久 NACK、73 持续 SCL 低，四项通过 | `audit/runs/20261101T114247Z_1793533367729472600/results.json` |
| 有限恢复 | NACK/SCL 低场景均恰好三次自动尝试；耗尽后五秒不再尝试；解除故障并 KEY3 后恢复新 pair | 同上；模型结果，不是五分钟板测 |
| 新鲜度语义 | 旧 pair 不算恢复、两组新鲜 pair 清预算、保持方向不直接 armed、回中及过期保护、KEY3 重新授权通过 | `audit/freshness_result.json` |
| 主机契约 | 73 控制/生命周期、13 单 ELF、3 HUD、16 G10，共 105 项通过 | `offline_regression/RESULTS.json` |
| 重复模型 | 额外十次开局/重开、九次密度变化、十次轻量转压力及开火/护盾/结束通过 | `offline_regression/RESULTS_zh.txt`；不替代实际板上次数 |
| 遥测与栈 | 保守 TEL+JOY 651 B，768 B 局部缓冲和 2048 B 环形缓冲哨兵完整；五秒 TX 背压模型继续运行；最坏静态栈 1232/8192 B | `offline_regression/`、`offline_regression/stack/reports/stack_joy_recovery.json` |
| 加载工具清理 | 五个离线资源清理用例通过；异常仅关闭自己创建的 GDB/OpenOCD/串口句柄，无全局杀进程 | `tools/lifecycle_test_results.json` |

固件 text/data/bss 合计增加 680 B，驱动状态 BSS 增加 8 B。JOY 130 B/s 上界对应约 1.13% 的 115200 8N1 线速带宽，是推算值，不能作为板上 CPU/FPS 性能结论。

初始测试模型曾仅推进虚拟 CLINT、未让 RTL 经过 10 ms 超时，掩盖真实失败分支；无效基线结果已保留并由上述严格基线替代。测试适配和初始失败详情见各报告，不把失败日志删除后仅保留通过记录。

## bit / ELF 配对与时序

| 文件 | 版本 | 完整 SHA-256 |
|---|---|---|
| `board/candidate_pkg/pair/ti60f225_oob.bit` | 原 GHDPP，FRAME_DONE_PULSE | `72d4f01497b5599bc633a35162388f312a0631ea17b348fdf97a5efea335bd54` |
| `board/candidate_pkg/pair/apbTest.elf` | 新 `GHDB1_20261101` | `0fac4deabec6a794316c8672ec37bbe3db7201c74c6fd5c3fc0880a976101087` |
| 原 GHDAX ELF（备份基线） | `GHDAX_20261101` | `d191d7a00c0cae3393027eed7589e131be95d75ae3b93cf7a4b1dc61bd4d7c18` |

本次没有修改 RTL，没有重建 bit。新 ELF 对原 bit 的 HUD/DDRCRC ABI 兼容性已通过实际加载身份门控，应用 RAM 90164 B 回读一致，144 KiB RAM/bootloader 边界和代码指纹检查通过。用户已确认 F3/排针 08 的使用，本轮未重做原理图或选脚排查。

所用 GHDPP bit 的历史整体 STA **FAIL**：19 个既有负 setup 端点，WNS **−1.139 ns**、TNS **−11.469 ns**；hold 最小 +0.027 ns。相对更早 GHDAB（WNS −0.792 ns、TNS −10.356 ns）端点集合相同，但数值有变化。该变化来自此前 FRAME_DONE_PULSE 构建，不是本次固件修复引入。此次没有新 FPGA STA 结果，不能称时序通过。原表见 `../frame_probe_20261101/RESULT_frame_probe_zh.md`。

## 已执行的板上加载及待完成验收

`board/load_GHDB1_r3` 已实际完成 PROGRAM PASS 和 LOAD_SESSION PASS：90164 B RAM 逐字节回读一致；bootloader 代码一致；PC/SP/GP、断点代码与 ELF 一致；CPU 恢复运行后及退出 OpenOCD 后两次 `ASSET BUILD` 均返回 `GHDB1_20261101 / HUD=48440900 / DDRCRC=44435201 / VERIFY=1`；清理无错误。运行时 bootloader 栈字与静态初始化不同，已按原代码区/运行时数据区规则单独核验，没有把整个 1 KiB 静态区强行判为相等。

保留的加载工具失败：首次 Python `-I` 未显式加入同目录模块路径，在接触硬件前失败；r2 的厂商 GDB `dump binary memory` 将引号保留为文件名，导致已 load/halt 后读回失败。前者已补脚本路径，后者已固定 GDB 工作目录并使用裸相对文件名；实际厂商 GDB 在含空格目录的离线 dump 试验通过，r3 实际读回通过。失败现场未覆盖。

素材上传记录 `board/candidate_pkg/board_logs/upload_20261101_194748_445051/summary.json` 为 PASS：ID 1–33 全部成功且均一次尝试，逐项 DDR CRC 通过；最终 `VALID_MASK=FFFFFFFE`、`VALID_MASK_HI=00000003`、ACTIVE=0、ERROR=0、STAGE=0，身份仍为 GHDB1。素材大小 6,823,296 B。

第一轮默认十条生命人工板测 `board/capture_manual_v1`：用户反馈“正常了，可以开始整理”。日志确认有效压力 20:04:25.846–20:05:09.859，首末边界 **44.013 秒**，随后 GameOver。排除首个转换混合窗口后的 44 个压力窗口均 60 帧，长间隔 0、最大完成帧间隔 16701 µs、实弹最大 2427/组 850；输入新 pair 合计 2805，停采/invalid/latched/fault 均 0。观察到左右、左下、回中、fire 请求，护盾 abs 达 1577、score 达 11700。最长同方向连续快照跨度约 4 秒，不能据此称长时间保持或全部方向已测。详见 `evidence_audit/MANUAL_V1_RESULT_zh.md`。此段**没有达到五分钟**。

先前两次自动重开试验的失败均保留，各自合格轮数 **0/10**：

- `board/restarts_soft_v3_run1`：首次菜单开局成功；第一个重开未在两秒内命中软件断点，停止。最终原 16 B 代码恢复、CPU running、自有 OpenOCD 正常退出均核验成功。
- `board/restarts_event_v4_run1`：只写事件的替代脚本在第一个重开注入前，健康检查读到 `joy_faults=3`（累计）、valid=0，按守卫中止，没有把未完成轮次计为通过。最终恢复 running。该次没有进入实际重开事件注入。

随后 `board/restarts_event_v5_run1` 在 20:10:23.875–20:11:21.102 实际完成 **10/10**，经独立只读复核：flow.round 连续 2→12，每轮显示 GameOver 后真实重开，生命/显示生命回 10、两种 over 清零、模式回 LIGHT，帧与新 pair 前进，应用无新增错误。每轮恢复后在无 JTAG 读写的至少两秒 UART 边界跨度中，hp>0、valid/armed=1、latched=0、jf=0、seq 增长、cap 未耗尽；37 个选定窗口共 3667 新 pair。最终 CPU running、自有 OpenOCD exit=0。详情见 `BOARD_RESTARTS_RESULT_zh.md` 和 `evidence_audit/restarts_v5_independent.json`。

这十轮是 **JTAG 注入 GameOver/逻辑 KEY3**，不冒充十次物理按键消抖或自然判死。首轮由保留 PRESSURE phase 的 GameOver 回 LIGHT，后九轮为 LIGHT 重开。调试段累计 fault 从首 pre 的 5 到末 post 的 33（各轮 pre→post 合 21，轮间另 7），stale 保持 1；恢复后的选定 UART 窗口 fault 全为 0，不能声称整个 JTAG 阶段零 fault。首轮首个 UART 行 fr58/lf2/im35494 µs、第六轮首行 fr59，是可能混入先前暂停/恢复的边界窗口；每轮剔除首行后 27 个窗口均 fr60/lf0，不将它们替代独立五分钟压力性能测量。

第二次试验提供了真实板上恢复旁证：20:06:38.340–38.360 的调试读取窗口看到 seq=112659、valid=0；20:06:38.861 接收的 TEL/JOY 已为 valid/armed=1、latched=0、seq=112705，同一窗口 `jf=2:2x2,18x1`、`jb=1`、`bok` 从 0 到 1；下一窗口新 pair=100、fault=0、seq=112805、fr=60。这里原因 2 为 BYTE_TIMEOUT，18 为 RECOVERY_LINES。从最早无效读取时间到收到有效遥测不超过约 **521 ms**，这是主机观察包围区间，不是精确 I²C/ADC 恢复用时。没有 reset/reload 即恢复更新，且后续持续更新。扰动与 JTAG halt 同时发生，必须归类为**人为调试暂停诱发的控制器恢复**，不是自然游戏故障；hp 始终为 0，不算压力验收。`jr=0/0/0` 表明这是内部总线恢复，不是“耗尽三次后”的自动重试板测。原始片段/哈希和 JTAG 记录快照保存于 `evidence_audit/debug_recovery_v4/`。

| 板测项目 | 当前状态 | 最终需要记录 |
|---|---|---|
| bit/ELF 编程、读回和身份 | **已通过** | `board/load_GHDB1_r3/load_session_result.json` |
| 33 项素材上传及 DDR CRC | **已通过** | 33/33 一次成功、VALID_MASK/ERROR/身份核验 |
| 至少十次开局/重开 | **10/10 板上逻辑重开通过** | v5 真正流转/显示/新输入验证，明确 JTAG 注入分类；不是十次物理按键试验 |
| 实际轻量→压力及重复转换 | **2次L→S、2次压力GameOver→L，另有LIGHT重开** | manual_v1与pressure_v2各一次L→S；v5首轮与恢复默认生命各一次S→L，未声称十次完整L→S |
| 连续五分钟实际压力 | **323.9946 秒测试生命扩展压力通过** | 93 个 L 窗口排除，324 个完整 S 窗口逐窗60帧；默认十条生命自然存活五分钟未测 |
| 原始输入、超时/恢复和 FPS | **人工长段输入稳定、无明显 FPS 退化；另有 JTAG 扰动恢复证据** | 压力完整窗口20538新pair、fault/停采/invalid/latched全0，所有调试扰动另列 |
| 方向保持、实体按键覆盖 | **左右/四斜向/回中/fire/护盾有证据；35秒固定方向未完成** | 最长同向快照跨度压力6.992秒、轻量7.999秒，不用用户回复替代数据；十次物理KEY3消抖未独立验收 |
| 收尾与默认状态恢复 | **只读特效/输入快照、原GameOver/KEY3重开恢复通过** | flow13→14，生命/显示生命10，输入继续；临时1000生命清除，自有COM/JTAG已释放 |
| 基础 demo 板上运行 | **本轮未测** | 保持原功能的独立配对及验证状态 |

自动重开脚本的性质是 JTAG 注入 GameOver/逻辑 KEY3。独立审查发现 CPU 的 `hardwareBreakpointCount: 0` 后，先评审限定软件断点，继而评审只写三项 RAM 的 event-only v4/v5；v5 改为恢复后的 UART 无 JTAG 窗口验证并完成十轮。v6 仅增加 `--rounds 1..10`（默认 10），用于之后单轮开始/恢复，不改变门控或写入范围。各版本/离线测试及失败见 `evidence_audit/RESTART_INDEPENDENT_REVIEW_zh.md`。调试 halt 引入 SERVICE_GAP、BYTE_TIMEOUT 或 I²C 超时必须单列为测试扰动，不能混入无扰动五分钟压力性能数据。

为避免默认十条生命很快 GameOver，已评审“正常开局后仅将 game_lives 设为 1000”的一次性 RAM 测试设置。game_lives 无数组索引用途，HUD2 钳位至 10，受击无敌期和全部运行负载路径保留；结束后通过原 GameOver/KEY3 路径恢复默认十条生命。它必须明确标为测试生命扩展压力，不改变交付 ELF，也不能冒充默认生命自然生存。第一次 `board/pressure_lives_setup_v1` 因执行前等待期间自然 GameOver，被 `not_normal_active_game` 守卫拒绝，**未写生命**，CPU 已恢复 running，失败记录保留。

随后同 shell 的 `board/pressure_start_v6_run1` 单轮重开 PASS1/1，`board/pressure_lives_setup_v2` 在 20:12:35.372 实际将生命 5→1000（phase LIGHT），身份与读回核验成功，CPU running、自有 OpenOCD exit0。之后的 receive-only `board/capture_pressure_v2` 全程无 JTAG，已停止并保存。93 个 L 窗口不计入压力；实际 S/hp>0 从 20:14:23.852 连续至 20:19:47.846，跨度 **323.9946136 秒**。首个转换窗口排除后，324 个完整压力窗口均 fr60/lf0，总19440完成帧、20538新pair，invalid/unarmed/latched/fault/停采均0。最大完成帧间隔16703 µs，对比历史同档稳定压力16697 µs，未见完成帧率下降；实际弹数峰值2439、组数850。详细证据及对比见 `PRESSURE_V2_RESULT_zh.md`。

该段包含实际左右/四种斜向/回中、89个有效fire快照、护盾吸收增长9027、分数增长36700。要求追加的35秒固定方向未出现在数据中，最长压力同方向快照跨度只有6.992秒，故此项仍标未完成。每秒JOY不能声称高频ADC的每个间隔或秒间持续物理动作。用户确认正常不替代这项未观察到的覆盖。

压力停止后 `board/post_pressure_snapshot_v1` 只读收尾 PASS：fx_enabled1/session13；分组先读 spawned376，稍后已显示累计 spawned377、dropped60、merged2、evicted0；shield_toggles1/absorbed9879、combat_pb_kills337，已显示flags65539（MANUAL3+可见），输入valid/armed且未latched。各组非同一时刻，且计数包含本局轻量阶段和停止采集后的运行，不能把377当五分钟内精确爆炸次数；特效包含火花、drop/merge均保留。随后 `board/restore_normal_lives_v6` PASS1/1，flow13→14，经原GameOver/逻辑KEY3真正新开局，RAM和原显示生命均恢复10/over0；2.991832秒UART健康窗口通过。收尾的JTAG扰动全部排除在压力数据之外，最后无扰动窗口仍jp100/fault0/fr60，CPU运行。临时1000生命状态已清除，自有串口/JTAG资源已释放。详见 `PRESSURE_V2_RESULT_zh.md`；测试设置审查在 `evidence_audit/TEST_LIVES_AUDIT_zh.md`。

## 联合精简交付与重新构建说明状态

`board/candidate_pkg` 仅为本轮板测 fixture，不是最终源码交付包。它有独立素材/配对/加载依赖，但未复制完整重建源码。最终应分别提供 `demo_0_basic/`、`demo_1_game/`，保留各自 FPGA 工程；根目录 README 和版本清单需给出独立构建、工具版本、上板和切换步骤。

当前已确认工具基线为 Efinity 2026.1 和 Efinity RISC-V IDE 2026.1；外部安装目录应由配置指定，不纳入交付。工具已采用包内相对工程根目录及外部工具根配置。最终包仍必须完成：两套必要源码/IP/初始化/素材依赖检查、各自干净编译、一个小于 200,000,000 B 的 ZIP、解压至新目录后重新验证不依赖原工作区。

联合 ZIP 的路径、字节数、SHA-256 及两套工程解压重建结果由根交付清单另行记录。本文件记录已经冻结的进阶修复与板测事实，不将打包状态或基础 demo 未执行的板测写为通过。
