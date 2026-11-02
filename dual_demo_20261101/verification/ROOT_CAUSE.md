# 摇杆故障修复、证据与验证范围

状态：**RELEASE_WITH_LIMITATIONS**。游戏板测为 **PASS_WITH_COVERAGE_LIMITS**：十轮实际逻辑重开与超过五分钟持续压力通过，35 秒固定方向未覆盖；基础 demo 本轮板测未执行。两套 FPGA 均保留既有时序失败。

修复前检查点：`6f20230d0b4fbb2b433bfaf0efe5bdccda070f79`。
修复提交：`b74b29b78d5845fb2b64defe5fda4b2f16a7c0ac`，仅四个生产文件，40 行增加、17 行删除：`joystick_ads1115.c/.h`、`main_single.c`、`build_id.h`。基础 demo 功能源码没有套用该修复。

## 证据与根因

原 GHDAX 日志在第 3 次开局后出现 BUS_RECOVER / RECOVERY_LINES；随后 455 个窗口新 ADC pair 合计为 0，411 次恢复失败，后续 4 次开局仍未恢复。相同故障区间游戏保持每秒 60 帧，后续重开/档位变化仍有响应。旧遥测没有原始 ADC、独立 SCL/SDA 和完整 I²C CSR，因此没有将模型寄存器值冒充原故障板上实读值。原大日志完整留在工作区，交付只含精简摘要及其 SHA-256。

真实驱动 C、原 I²C/GPIO RTL 与 ADS1115 模型重现：读地址 `0x91` 的 ACK 后，从机正在输出首个 0 位时，GameOver/重开撤销输入 epoch。旧代码等待 STOP 最多 1 ms 后 DROP，但没有清理所有 TX/RX 等待；总线恢复时钟可能被遗留等待阻塞。随后控制器 10 ms 超时把 `inFrame` 清零，外部 SDA 仍可为低。旧初始化只按 `inFrame` 判定是否恢复，导致跳过恢复并反复锁定。严格基线三秒内没有新 pair，最终仿真 CSR 为 master idle/inFrame=0/SCL高/SDA低。

修复在 DROP 后完整重建 TX_DATA/TX_ACK、关闭 RX 监听并释放 override；只要 inFrame 或任一总线线为低就进入有界恢复，要求 master idle、inFrame=0 且 SCL/SDA 均高才退出。连续自动尝试最多三次，保留既有 probe/episode/attempt 上限；耗尽后停止，KEY3/显式 RETRY 可重新授权。两组新鲜 pair 才计恢复成功，保留 40 ms 过期清零及 100 ms 回中要求。新增 JOY 诊断行，原玩法、TEL 与帧率测量保留。

## 已执行与未完成

- 固件编译：两套 RC ZIP 在全新短 TEMP 含空格目录干净编译，与配对 ELF 完整哈希相同。15/30 个对象对应的真实依赖扫描全部限定于解压工程或声明的工具链，未回读原工作区。
- C/RTL 共仿真：配置读撤销、转换读中断、永久 NACK、持续 SCL 低四场景通过；三次尝试上限/耗尽等待与显式 KEY3 恢复见记录，模型结果不替代物理电气故障注入。
- 主机模型：105 项既有合同和额外十次开局/模式转换通过。静态保守栈界限 1232/8192 B，最坏链含遥测；新增 JOY 序列保留原 TEL/帧率功能。
- 板上重复操作：实际 FPGA 受控 GameOver/逻辑 KEY3 十轮 PASS10/10，每轮原路径回到 LIGHT、十命、显示与新鲜输入。不是十次实体按键，不是自然碰撞判死；调试扰动 fault 单列，未混作无扰动性能。
- 人工 v1：有效压力 44.013 秒，44 个完整窗口逐窗口 60 FPS，long=0，输入 fault/无效/latched/自动恢复均为 0，最大完成帧间隔 16701 µs。用户确认恢复正常。
- pressure_v2：无 JTAG 连续压力 323.9946136 秒，324 个完整窗口共 19440 帧、逐窗 60 FPS、long=0、输入 fault/停更=0；20538 新 ADC pair。峰值 2439 弹/850 组，左右/四斜向/回中/开火/护盾吸收均有记录。最大完成间隔 16703 µs，历史稳定窗口 16697 µs；内容不完全一致，未见明显帧率退化，但不作为严格 CPU 开销对比。只用 RAM 单变量暂扩 1000 命防碰撞提前结束，未改源码/ELF。收尾 effect spawn377/drop60、吸收9879/击杀337 为测试后顺序读取，非压力末秒原子数。
- 恢复：通过原 GameOver/KEY3 单轮 PASS1/1，flow13→14、生命/显示10、新输入继续，COM/JTAG 已释放。
- 覆盖缺口：35 秒固定方向未完成；压力最长同向快照仅6.992秒，不能宣称长保持通过。十轮是逻辑事件注入；实体按键消抖及真实电气故障注入未覆盖。
- FPGA：未执行本轮重建/STA，无 RTL 修改，沿用已核对的 FRAME_DONE_PULSE bit，继承既有时序失败。基础 demo 本轮板测未执行。
- 打包：RC 离线验收通过。最终归档在 SHA 固定后独立解压验收，结果与最终 ZIP 哈希记录于压缩包旁的 dual_demo_20261101.verification.json，避免循环哈希；RC 不替代最终验收。

具体证据文件、采集记录的 SHA-256、原工作区完整备份位置见 `EVIDENCE_INDEX.md`。原记录中的绝对路径/validation 相对路径是采集时来源，不是运行或构建依赖；交付工程自带 SOURCE_SHA256.json、delivery_pairs.json 和完整逐文件清单。
