# Qt 键盘控制对接记录

本次范围为 PC 上位机，未打开用户 COM15，未改 bit / ELF / 硬件工程。

## 复用与来源

- `gold_game_panel.py` 和 `test_gold_game_panel.py` 保留上传包原文件；身份、命令应答、校准保护继续使用。
- 新视图 `panel_view.py`，应用与键盘事件 `render2d_console.py`，单线程串口与持续输入 `keyboard_transport.py`。
- 原包明确提供 `PLAYER SHIELD` → `PLAYER SHIELD ACCEPTED`。
- 历史项目主机脚本：`PLAYER CONNECT`，严格匹配 `PLAYER CONNECTED ([0-9A-Fa-f]{8})`，首包为 `PLAYER INPUT {session:08X} 00000000 00`。
- 历史 `player_input.c` 片段：`x=((mask&8u)!=0)-((mask&2u)!=0), y=((mask&4u)!=0)-((mask&1u)!=0)`；据此 W=01、A=02、S=04、D=08。
- Space fire=20、INPUT静默无应答、完整键态心跳沿用先前工具交接记录。上传 ZIP 未附固件源码，不能将历史记录写成本轮候选板测。
- 历史超时定义 `PLAYER_INPUT_TIMEOUT_TICKS (((u64)BSP_CLINT_HZ+4u)/5u)` 为200ms；当前候选是否保持需本地源码核对。

## 输入合同

`PLAYER INPUT <8位十六进制session> <8位十六进制seq> <2位十六进制mask>`，换行结尾。
首包seq=0、mask=00；随后32位递增回绕；30ms完整键态心跳；INPUT不等待ACK。
Tab为独立边沿触发命令，不进入mask、不随心跳重复；键盘自动重复被过滤。
失焦、编辑参数、Esc、停止、断开时清空本地按键并尽可能发送零掩码。
GUI租约150ms，超时撤销并要求显式重新接管，不自动建立新会话。
连接、心跳、护盾、状态查询共用一个线程和串口，避免多个程序抢读回包。
部分写入/写入异常不补发，防止半行后拼接成其他命令。板端拒绝或超时不得记为按键已生效。

## 接当前固件前由本地负责人核对

1. 配对目录、manifest、bit/ELF/map SHA、BUILD、JOY STATUS/DIAG ABI；保持精确身份匹配，不删除校验绕过。
2. 当前解析器是否保留上述握手、方向位、fire=0x20、序号/超时语义及静默应答规则。
3. APP所有权和摇杆/主机输入仲裁，什么状态允许接管；不自动关闭APP或摇杆来掩盖拒绝。
4. STOP、GAME OVER、RESTART、EXIT会话撤销与状态字段；发现撤销后用户重新接管。
5. `PLAYER SHIELD`是开启还是已改为切换；Qt仅触发命令，不自行推断护盾实际状态。

## 最小实测

匹配固件游戏运行后：CONNECT→首包00→W按下/松开→W+D→Space持续/松开→Tab按下/长按/再次按下→失焦→Esc→停止后重新接管。
对照高亮、tx.accepted.bin中的mask/seq、板端坐标/开火/护盾实际变化。
不把本地高亮等同板端执行；不为测试改变击毁阈值、飞机数量、弹池、炮塔弹或渲染模式。
