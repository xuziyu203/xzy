# 摇杆故障修复与交付暂存状态

状态：**PARTIAL_STAGING**。用户已确认摇杆“正常了，可以开始整理代码了”。这项体验确认及开始整理的授权，不等于尚未完成的连续五分钟压力和十轮实际重复操作已通过。

修复前检查点：`6f20230d0b4fbb2b433bfaf0efe5bdccda070f79`。
修复提交：`b74b29b78d5845fb2b64defe5fda4b2f16a7c0ac`，仅四个生产文件，40 行增加、17 行删除：`joystick_ads1115.c/.h`、`main_single.c`、`build_id.h`。基础 demo 功能源码没有套用该修复。

## 证据与根因

原 GHDAX 日志在第 3 次开局后出现 BUS_RECOVER / RECOVERY_LINES；随后 455 个窗口新 ADC pair 合计为 0，411 次恢复失败，后续 4 次开局仍未恢复。相同故障区间游戏保持每秒 60 帧，后续重开/档位变化仍有响应。旧遥测没有原始 ADC、独立 SCL/SDA 和完整 I²C CSR，因此没有将模型寄存器值冒充原故障板上实读值。原大日志完整留在工作区，交付只含精简摘要及其 SHA-256。

真实驱动 C、原 I²C/GPIO RTL 与 ADS1115 模型重现：读地址 `0x91` 的 ACK 后，从机正在输出首个 0 位时，GameOver/重开撤销输入 epoch。旧代码等待 STOP 最多 1 ms 后 DROP，但没有清理所有 TX/RX 等待；总线恢复时钟可能被遗留等待阻塞。随后控制器 10 ms 超时把 `inFrame` 清零，外部 SDA 仍可为低。旧初始化只按 `inFrame` 判定是否恢复，导致跳过恢复并反复锁定。严格基线三秒内没有新 pair，最终仿真 CSR 为 master idle/inFrame=0/SCL高/SDA低。

修复在 DROP 后完整重建 TX_DATA/TX_ACK、关闭 RX 监听并释放 override；只要 inFrame 或任一总线线为低就进入有界恢复，要求 master idle、inFrame=0 且 SCL/SDA 均高才退出。连续自动尝试最多三次，保留既有 probe/episode/attempt 上限；耗尽后停止，KEY3/显式 RETRY 可重新授权。两组新鲜 pair 才计恢复成功，保留 40 ms 过期清零及 100 ms 回中要求。新增 JOY 诊断行，原玩法、TEL 与帧率测量保留。

## 已执行与未完成

- 固件干净编译通过，独立源码副本构建 ELF 与交付配对逐字节相同。
- C/RTL 共仿真四场景通过：配置读撤销、转换读中断、永久 NACK、持续 SCL 低。后两场景均恰好三次自动尝试，耗尽后五秒无新增尝试，解除故障并 KEY3 后恢复。此五秒模型观察不是五分钟板测。
- 105 项既有主机合同和额外十次开局/模式转换模型通过。静态栈界限 1232/8192 B。
- 当前人工有效压力段 **44.013 秒**，44 个完整窗口逐窗口 60 FPS，long=0，输入 fault/停更/无效/latched/自动恢复均为 0；最大完成帧间隔 16701 µs。用户确认恢复正常。
- 连续五分钟实际压力：**未完成**。十次板上重复开局/重开与模式转换：**尚未宣告通过，主代理继续执行**。全部方向及长时间保持动作也只按实际证据计数。
- 未执行本轮 FPGA 重建/STA；本次无 RTL 修改，沿用原 FRAME_DONE_PULSE bit，继承原有时序失败，详见 `TIMING.md`。
- 基础 demo 本轮板上功能测试未执行；保留原配对和独立源码。ZIP 尚未生成，最终解压重编译/资源验收未执行。

详见同目录 `MANUAL_V1_RESULT_zh.md`、`manual_v1_final/analysis.json`、`cosim_results.json`、`OFFLINE_SUMMARY.json`、`TIMING.md`。各 demo 的源码清单与 pair SHA 在各自 `SOURCE_SHA256.json` / `delivery_pairs.json`，总体状态在根 `VERSIONS.json`。
