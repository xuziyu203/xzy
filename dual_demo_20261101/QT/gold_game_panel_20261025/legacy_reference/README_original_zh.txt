金盾游戏 · 简易按钮面板（独立 PC 工具）
======================================

本工具不修改 bit、ELF、素材或冻结目录。只配套：
GOLD_SHIELD_FLOW_20261025_CANDIDATE_PENDING_BOARD
不需要为安装面板重载固件或重新上传已有效的素材。

一、启动

将整个 gold_game_panel_20261025 文件夹解压到工程 validation 目录。
关闭 miniterm（Ctrl+]）、旧串口窗口及采集器，然后在 PowerShell 执行：

powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Users\Administrator\Desktop\1\three_renderer_async_quad_20261006\validation\gold_game_panel_20261025\RUN_GOLD_PANEL.ps1' -Port COM15

可双击目录中的 START_PANEL.cmd，默认 COM15、默认工程路径。
Python314、Tkinter 和 pyserial 必须已安装；沿用你现有 Python 环境。
若 Python 或工程不在默认位置，可给启动器 -Python、-GoldSol 参数。
只做本地配对核验：给上面的命令追加 -VerifyOnly。不会导入 serial 或打开串口。

二、点击顺序

1. 确认目标板已运行匹配 GOLD bit 和 ELF，且没有其它串口工具及待处理回复。
   勾选声明，点击“连接”。连接前只验证本地 manifest、bit、ELF、map 固定 SHA/长度。
   不会自动发送查询、复位、清缓冲或更改游戏状态。
2. 点击“刷新全部”。逐项读取 STATUS、素材、游戏、JOY、DIAG、FLOW、EFFECTS。
   任何错误立即停止这一组，不自动重试。也可单独点击“基础状态”等按钮。
3. 松开摇杆，在游戏停止时点击“用新 XY 校准中心”。使用两次有效、新序列且较稳定的
   实测读数，不套用历史中心。它不能判断你是否确实松手，按按钮前务必松手。
   若 X/Y 方向相反，停止后勾选相应反向位，再校准一次。
4. 点击“配置轻量→压力”，再点击“开始 / 继续”。
   配置前检查安全 idle、素材 ID1..27 完整、游戏 inactive，并查询 FLOW 支持情况。
   等待 ENEMY SHOWN / ENEMY MOVING。回中至少 100 ms，点“XY 读数”查看 ARM=1。
5. KEY1 按住开火，松开停火；KEY2 或面板“开启永久金盾”开启盾。
   取得 10 个确认击杀后，由固件执行淡出、黑场、压力首帧确认和淡入。
   本面板不伪造击杀，不强制跳转压力，不设置弹幕数量。
6. 点击“游戏”“转场”“护盾”查看实际状态。普通弹数量显示 MEMBERS_VISIBLE，
   不把当前饱和到 999 的 HUD OBJECTS 当成 2000+ 证据。
7. “暂停”等待 STOPPED；“开始 / 继续”恢复；“退出游戏”等待 EXITED 再读 STATUS。
   关闭面板或“断开串口”仅关闭 PC 串口，游戏不会自动退出。

没有周期性自动轮询；状态栏显示最近一次回包。后台持续保存设备主动发来的字节。
暂停/退出进行中请等待结果，不要启动第二个串口程序。

三、当前 UNKNOWN_COMMAND 的处理

你已观测到 STATUS 正常，但 PLAYER JOY DIAG 得到 ERR UNKNOWN_COMMAND。
这不是 UART 零输出。面板会立刻显示“板端拒绝哪条命令”，不会再只写 Reply timeout。
错误会中止当前按钮的后续步骤，仍可手动查询基础状态及其它单项命令。
面板不改变固件解析器，不能据它安装成功宣称 UNKNOWN_COMMAND 已修复。
如果仍出现，保存 board_logs 中的新会话；不要反复点开始、重新烧录或清除记录来掩盖。

若等待真正超时或只写入部分命令，停止发送并关闭连接，保留日志；不自动补发或重连。
重新打开连接前仍需确认没有旧命令待回复。协议没有 request ID，不能严格证明逐行关联。
自然收到 BUILD 时检查身份；未收到时保持“操作者声明”，不为获取 BUILD 重启。

四、记录与边界

日志只写面板旁 board_logs/panel_<UTC时间>/：
- identity.json：本地身份、端口与操作者声明。
- rx.raw.bin：实际收到的全部字节。
- tx.accepted.bin：驱动确认接受的字节；不等同物理线路已发送。
- events.jsonl：收发、命令、解码及错误的主机 monotonic_ns 时间。
- partial_rx.bin：断开时尚未完成的接收行。

TX 写入异常另记 TX_OUTCOME_UNKNOWN，不猜测实际发出了多少字节。
DTR/RTS 在打开前设为 False，软硬流控关闭；不发 reset、RETRY、CLEAR、上传或键盘输入包。
本工具不测试硬件复位，不测量 DTR/RTS 的物理瞬态。
局部回包与软件计数不能代替屏幕视觉、全帧连续性、性能窗口或完整 EXIT 双读验收。
首故障槽可能是历史记录；累计计数不会在本面板中被清零。
原整体 STA FAIL、历史性能 FAIL、当前 GOLD 实板 PENDING 均保持。

五、离线测试

python -E -B test_gold_game_panel.py

测试使用假串口，覆盖明确错误回包、分段读取、延迟/超时、部分写入、身份不符、
校准门禁、素材缺失、操作顺序和退出/关闭行为，不访问 COM/JTAG。
具体结果与测试范围见 TEST_RESULTS.json。
