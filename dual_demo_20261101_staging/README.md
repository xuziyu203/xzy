# Ti60 两套独立 demo

**交付整理状态：PARTIAL_STAGING。这是代码与资源暂存目录；尚未完成的板测、5 分钟压力和 ZIP 解压验收仍按版本清单记录，整理代码不代表这些项目通过。**

`demo_0_basic` 是原基础四分屏/全屏方块演示及测量版本；`demo_1_game` 是保留当前玩法和实际帧率遥测的摇杆修复游戏。两套 FPGA 工程独立，切换时整套加载对应 bit 与 ELF。

## 版本与验证

完整源码身份、产物 SHA-256、板测和剩余问题见 `VERSIONS.json`。本 README 中的未填状态不得当作通过。

- 基础源码：665157205c969c64b0de12848f598c2f94f67e62；板测：NOT_RUN。
- 游戏修复前检查点：6f20230d0b4fbb2b433bfaf0efe5bdccda070f79；修复提交：b74b29b78d5845fb2b64defe5fda4b2f16a7c0ac；板测：PARTIAL。
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
F3/排针08 的使用已确认；测量口为 1.8 V LVCMOS 输出，参考地排针06。游戏 FRAME_DONE_PULSE 每完成一帧输出 10 us 脉冲；基础口按基础固件测量开关控制。其他接线和实际验收说明由最终记录补充。

## 本次修复与实测记录

修复提交 b74b29b78d5845fb2b64defe5fda4b2f16a7c0ac 已记录。用户已确认摇杆正常并授权整理。本目录仍为 PARTIAL_STAGING：人工连续有效压力为 44.013 秒，44 个完整窗口均 60 FPS、long=0、输入 fault=0；因 GameOver 尚未完成连续五分钟，十轮实际重复开局/模式转换也尚未宣告通过。详见 verification/ROOT_CAUSE.md、MANUAL_V1_RESULT_zh.md、cosim_results.json、TIMING.md。基础本轮板测未执行；ZIP 与最终解压验收未执行，后续真实结果另行补齐。

各 demo 自带 `SHA256SUMS.txt`，根目录另有总体 `SHA256SUMS.txt` 与 `MANIFEST_SHA256.json`。源码树摘要包含必要源/config/IP/init；运行资源和工具有独立逐文件哈希。
