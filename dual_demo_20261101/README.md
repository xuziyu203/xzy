# Ti60 两套独立 demo

**交付整理状态：RELEASE_WITH_LIMITATIONS。两套工程的实测范围及未测项目分别记录于版本清单；源码整理、板测、时序及 ZIP 验收互不替代。**

`demo_0_basic` 是原基础四分屏/全屏方块演示及测量版本；`demo_1_game` 是保留当前玩法和实际帧率遥测的摇杆修复游戏。两套 FPGA 工程独立，切换时整套加载对应 bit 与 ELF。

## 版本与验证

完整源码身份、产物 SHA-256、板测和剩余问题见 `VERSIONS.json`。证据文件与工作区备份位置的区别见 `verification/EVIDENCE_INDEX.md`；未测或待填项目不得当作通过。

- 基础源码：665157205c969c64b0de12848f598c2f94f67e62；板测：NOT_RUN。
- 游戏修复前检查点：6f20230d0b4fbb2b433bfaf0efe5bdccda070f79；修复提交：b74b29b78d5845fb2b64defe5fda4b2f16a7c0ac；板测：PASS_WITH_COVERAGE_LIMITS。
- 两套固件均已在独立离线临时副本干净编译，ELF 与当前配对一致。最终 ZIP 解压验收另行记录，不能由此前编译代替。
- 两套现有 bit 的时序结果单独记录于版本清单；保留原失败，不将生成 bit 或板上运行视为 STA PASS。

## 外部工具

Windows x64，Efinity 2026.1.132，Efinity RISC-V IDE 2026.1（GCC 13.4.0、GNU Make 4.2.1），Python 3.10+；串口工具需 pyserial，基础 Qt 面板需 PySide6>=6.10,<7。工具链、USB 驱动、许可证和虚拟环境不在包内。

PowerShell 设置安装位置和当前串口：

```powershell
$env:EFINITY_HOME='C:/Efinity/2026.1'
$env:EFINITY_ROOT=$env:EFINITY_HOME
$env:RISCV_IDE_ROOT='C:/Efinity/efinity-riscv-ide-2026.1'
$env:DEMO_COM_PORT='COM15'
```

使用普通 Python 时清除不匹配的 PYTHONHOME/PYTHONPATH。使用 Efinity 自带 Python 时设置 PYTHONHOME 为 `$env:EFINITY_HOME/python311`，以其 `python311/bin/python.exe -B` 调用；不要加 -I/-E。现有固件 makefile 要求 RISC-V IDE 安装路径不含空格。
旧版 Windows GCC 的完整未规约 include 路径必须小于 260 字符（包括 `../` 片段）；请解压到 `C:/demos` 或桌面下较短目录，避免在长工作区层层嵌套。目录名可以含空格，已在短 TEMP 含空格目录实际验证；过深路径可能将实际存在的头文件误报为找不到。

## 分别重建

从本包根目录执行（每次生成新的构建目录）：

```powershell
python -B demo_0_basic/tools/build_firmware.py --project-root demo_0_basic
python -B demo_1_game/tools/build_firmware.py --project-root demo_1_game
python -B demo_0_basic/tools/build_fpga.py --project-root demo_0_basic --profile basic --output build_basic_01
python -B demo_1_game/tools/build_fpga.py --project-root demo_1_game --profile game --output build_game_01
```

FPGA 命令默认仅检查依赖、准备独立快照和命令计划；加 --execute 才启动 Efinity。实际执行需使用未存在的 --output。两套均 seed5/I3/TIMING_1，保留各自线程、调试和 pack 配方。STA 非通过时返回 2 并保留报告。固件始终使用默认版本；不要给游戏传 VARIANT=full。生成的 IP RTL/init 已随工程保留，重建无需重新生成 IP。

## 加载与切换

先释放当前串口/JTAG，再加载目标工程的整套配对。基础 demo：

```powershell
python -B demo_0_basic/tools/load_basic.py --project-root demo_0_basic
python -B demo_0_basic/tools/load_basic.py --project-root demo_0_basic --execute
```

第一条仅校验/记录计划，第二条配置 SRAM 并加载 ELF。游戏依次执行：

```powershell
python -B demo_1_game/tools/program_bit.py demo_1_game logs/game_session_01
python -B demo_1_game/tools/load_session_pkg.py demo_1_game logs/game_session_01
python -B demo_1_game/tools/upload_single.py --check-only
python -B demo_1_game/tools/upload_single.py --port COM15
```

游戏 33 个素材必须完成 DDR 校验和注册后才开局；更换 bit 或重置后按加载流程恢复素材。各次会话使用新日志目录。`upload_assets.py` 为内部载荷模块，其旧独立入口已改为提示使用 `upload_single.py`。不要交叉配对两套 bit/ELF，也不要在独立工具之间同时占用 COM/JTAG。

## 基础串口面板与接线

串口 115200、8N1、无流控；COM15 只是当前机器的端口名。CLI：`python -m serial.tools.miniterm COM15 115200 --eol CRLF`，Ctrl+] 退出。
Qt：在独立环境安装 `demo_0_basic/serial_panel/requirements.txt` 后运行 `python demo_0_basic/serial_panel/ti60_qt_panel.py --port COM15`；面板默认不自动连接。CLI 与 Qt 二选一占用串口。

基础功能命令：`MODE QUAD` 回到四分屏；`FULL CPU`、`FULL BLIT`、`FULL RECT` 切换全屏渲染；`NUM 1/2/4/8/16/20/32/64` 选择数量，实际发送时只取一个数，例如 `NUM 20`；`STATUS` 查询状态。启动不需要外部素材，20 色图集由固件生成。
基础固定搬运测量：依次发送 `SCOPE PULSE`、`SCOPE START` 并等待 `SCOPE ARMED`；`BURST OFF/ON` 与 `DBUF OFF/ON` 组成四组配置；`SCOPE STATUS` 读完成计数/耗时，`SCOPE BUSY` 改为忙区间，`SCOPE STOP` 恢复原场景。PULSE 代表每批 64 个 64×64 RGB565 BLOCKCOPY（512 KiB）真正完成一次，不能当作整幅 1080p 显示 FPS。

按既有项目接线记录保持连接：

| 信号 | 现有连接/操作 |
|---|---|
| ADS1115 摇杆电源 | 3.3 V，并与 FPGA 共地；原 10 月 31 日交接记录已纠正过误接 5 V，本轮未改接线 |
| I²C SCL | C5，扩展排针32 |
| I²C SDA | A4，扩展排针34 |
| ADC | 7 位地址 0x48（总线 0x90/0x91）；固件 0xC3/0xD3 配置读取 AIN0/AIN1 |
| KEY1 | P14；游戏内按住开火；菜单中摇杆回中并长按 2 秒校准中心 |
| KEY2 | N14；游戏内护盾开/关；菜单或 GameOver 界面切换密度档位 |
| KEY3 | A3；开始，结束后重开 |
| KEY0 | C4；复位 |
| 帧测量 | F3/排针08 已确认，1.8 V LVCMOS 输出；参考地排针06 |

游戏 FRAME_DONE_PULSE 每完成一帧输出 10 us 脉冲；基础口按上述测量命令控制。按键低有效。接线资料取自原 CMDJ_JOYFIX README 硬件段和 10 月 31 日工程交接，ADC/操作语义同时对照当前驱动与 main_single.c；本轮未重新开展 F3 原理图或选脚排查。

## 本次修复与实测记录

游戏修复提交 `b74b29b78d5845fb2b64defe5fda4b2f16a7c0ac`，仅四个固件文件；基础 demo 功能源码保持原样。根因、明确超时/三次恢复上限与旧值清零见 `verification/ROOT_CAUSE.md`。105 项既有合同、十次模型重开/转换、四种 C/RTL 故障共仿真通过；保守静态栈界限 1232/8192 B。

板上实际逻辑重开 **10/10 通过**，每轮经原 GameOver/KEY3 路径回 LIGHT、十命与新鲜输入；使用受控 JTAG RAM 事件注入，不等于十次实体按键消抖或自然碰撞判死。连续无 JTAG 压力 **323.995 秒**，排除首个转换窗口后 324 个完整窗口全部 60 FPS、long=0、输入 fault/停更=0，峰值 2439 弹/850 组，覆盖左右、四斜向、回中、开火和护盾吸收。原有爆炸效果路径亦有实际 spawn 计数，收尾计数的时点/容量丢弃另有披露。未见完成帧率明显下降。

**保留的覆盖缺口：35 秒固定方向未完成**，最长同向压力快照跨度 6.992 秒；每秒快照也不能证明中间绝无释放。延长压力仅临时将 RAM 生命改为 1000；交付源码/ELF 默认仍为 10，测试后已走原重开路径恢复显示十命且输入继续，COM/JTAG 已释放。见 `verification/PRESSURE_V2_RESULT_zh.md`、`BOARD_RESTARTS_RESULT_zh.md` 和 `EVIDENCE_INDEX.md`。

基础 demo 本轮板测 **NOT_RUN**。两套现有 bit 继承已知 STA **FAIL**，本轮无 RTL 变更、未重综合，详见 `verification/TIMING.md`。RC 的两套独立解压干净编译、真实依赖扫描、FPGA 依赖与离线加载资源检查均通过。最终 ZIP 的准确 SHA-256 与独立解压验收结果见压缩包旁的 `dual_demo_20261101.verification.json`；不能把 RC 结果当作最终验收。

各 demo 自带 `SHA256SUMS.txt`，根目录另有总体 `SHA256SUMS.txt` 与 `MANIFEST_SHA256.json`。源码树摘要包含必要源/config/IP/init；运行资源和工具有独立逐文件哈希。
