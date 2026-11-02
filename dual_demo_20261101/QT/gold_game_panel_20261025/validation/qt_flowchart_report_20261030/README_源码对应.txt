Qt 上位机交互与串口通信流程图 — 源码对应及使用说明
日期：2026-10-30
版本：白底横向紧凑版（左侧箭头标签，保持字号与实际逻辑）
用途：报告“2.3.2 软件各模块介绍”，仅描述 PC 上位机交互和串口通信。

一、源码依据
依据：C:\Users\Administrator\Desktop\qt\QT\qt_panel_scifi_20261030.zip
包内目录：gold_game_panel_20261025/
已静态阅读 render2d_console.py、panel_view.py、keyboard_transport.py 和所复用的 gold_game_panel.py。
上述四个文件与桌面 qt/gold_game_panel_20261025 及 qt/QT/gold_game_panel_20261025 中的对应文件逐字节一致。行号以此 ZIP 内源码为准。
本轮只生成报告图与说明，未运行或导入上位机程序，未打开串口，未操作开发板，未修改上位机源码。

二、图中功能与源码对应
精简版将“提交完整按键状态”合入判断“是”出口至工作线程的交互连线。交付点同时接入界面返回线；“否”也汇入同一返回线，保持异步事件处理逻辑。具体实现说明统一放在配套正文中。
1. 启动参数、事件循环：render2d_console.py 的 make_parser():14、main():428、run_gui():24。QApplication.exec() 位于第 35 行。
2. 界面、通信队列及事件绑定：render2d_console.py 的 Controller.__init__():49；panel_view.py 的 PanelView.__init__():186。队列与 InputMailbox 在第 53–56 行初始化，QTimer 每 20 ms 调用 pump()（第 77–80 行）。
3. 连接与请求提交：render2d_console.py 的 connect_board():254、enqueue():285、submit():295；连接声明和当前状态决定是否允许操作。connect_board() 第 268–271 行创建并启动 KeyboardWorker。
4. 本地配对核验与串口连接：gold_game_panel.py 的 verify_pair():47；keyboard_transport.py 的 KeyboardWorker.run():262（第 265 行核验，第 269–272 行创建并打开串口）。
5. 功能命令执行：keyboard_transport.py 的 KeyboardWorker.perform():241；复用 gold_game_panel.py 的 run_action():298、query():267、Protocol.transact():212 和 COMMANDS:126。
6. 按键接收、组合状态和键帽高亮：render2d_console.py 的 eventFilter():172、active_keys():114、mask():117、key_change():123；panel_view.py 的 KeyCap.set_active():90。物理键与界面键帽状态取并集，单键松开只移除对应来源的对应键。
7. 接管启用与输入信箱：render2d_console.py 的 keyboard_toggle():226；keyboard_transport.py 的 InputMailbox.update():67、KeyboardProtocol.enable_keyboard():132。接管需已连接且通过运行状态检查与 PLAYER CONNECT 会话握手。
8. 控制命令生成、发送：keyboard_transport.py 的 encode_input():37、KeyboardProtocol._write_input():105、tick_input():181。W/A/S/D 分别为 01/02/04/08，空格为 20（十六进制）；完整状态通过 PLAYER INPUT 发送，INPUT 不等待 ACK。
9. Tab 护盾：render2d_console.py 第 144–145 行提交 shield_key；keyboard_transport.py 的 perform() 第 247–252 行执行 PLAYER SHIELD（定义于 gold_game_panel.py:136）。仅在按下沿提交，Tab 不入按键掩码，自动重复由 eventFilter() 第 203–204 行过滤。
10. 串口接收与解析：gold_game_panel.py 的 Protocol.read_lines():183、parse_line():75；keyboard_transport.py 的 KeyboardProtocol.read_lines():209 补充会话撤销、拒绝和输入心跳处理。
11. 结果通知与界面更新：keyboard_transport.py 的 KeyboardWorker.emit():238 写入 events 队列；render2d_console.py 的 pump():357 定时取出，decoded():312、refresh_controls():342 和 append():94 更新状态、统计、操作结果与日志。
12. 失焦与退出接管：render2d_console.py 的 suspend_keyboard():160、clear_keys():147、application_state():217；keyboard_transport.py 的 disable_keyboard():159 在会话存在且通信未发生致命错误时发送零掩码释放。
13. 关闭窗口：render2d_console.py 的 eventFilter() 第 174–181 行、disconnect_board():275 设置停止请求；keyboard_transport.py 的 run() 第 305–318 行尽可能释放并关闭串口、日志；GUI 在 pump() 第 413–415 行确认线程结束后，由 dispose():417 清理并关闭窗口。

三、与初始绘图思路的差异
• 源码解析的是命令行启动参数，未读取独立的 GUI 配置文件，因此图中使用“解析启动参数”。
• 启动时 worker=None；先初始化队列与输入信箱，点击连接后才创建工作线程。
• KeyboardWorker 是 threading.Thread。后台 emit() 实际向 Python 事件队列写数据，GUI 用 QTimer 读取；没有跨线程 Qt 信号。图中虚线表示异步交互，实线返回 Qt 事件循环。
• 连接只完成本地配对文件 SHA/身份核验和端口打开，并不等于已回读核实板端身份。实际接收到 BUILD: 时，Protocol.read_lines() 第 196–202 行才比对板端标识，不匹配则停止发送。
• 普通命令的匹配回复等待只发生在串口线程；INPUT 静默无回复。界面提交请求后继续处理事件，稍后从事件队列读取结果。
• 图中的“已连接且已启用键盘接管”是功能概括。源码键盘分支判断 keyboard_enabled，它在连接及握手成功后才启用；启用接管时关闭离线演示。离线演示默认开启；未接管且关闭离线演示时不捕获新按下事件。
• Tab 仅触发 PLAYER SHIELD，图中没有将其画成方向位或护盾开关。
• 关闭时通过定时回调确认工作线程结束；不在界面线程阻塞等待串口。释放命令是有条件的零掩码输入，并非无条件成功，也不会自动发送游戏 EXIT 或复位。

四、交付与插入建议
qt_flowchart_report_20261030.svg：可编辑矢量图；文字保留为 text/tspan，节点、箭头、连线均为独立矢量对象，没有内嵌位图。
qt_flowchart_report_20261030.png：白底，3780 × 3200 像素，600 dpi。
generate_flowchart.mjs：完整生成脚本；修改顶部 nodes、routes、labels 数组可调整文字和布局。
preview.png：聊天预览用缩略图；报告插入请使用主 SVG 或主 PNG。
配套正文_2.3.2.txt：报告可用正文，集中说明按键映射、组合与离线状态、线程创建、队列与定时器、串口协议及释放策略。
render_check.json：渲染尺寸、实际字体和自动布局检查结果。
source_manifest.json：本次依据源码的 SHA-256，方便核对版本。
caption.txt：可直接复制到 WPS 的独立题注。

在 WPS 中锁定纵横比，建议宽度 16.0 cm（高度约 13.55 cm）。节点文字为 10.4 磅，分支标签为 9.4 磅。本版图面全部为中文，使用宋体；脚本保留英文及数字的 Times New Roman 字体映射。图内不含可见大标题或解释性旁注。画布及两线程分区均为纯白色，节点使用浅米黄色 #FBF5E5；四类事件名称放在左侧水平箭头上方。

复现：安装 Node.js 和 playwright、sharp（npm install playwright sharp）。系统需有宋体、Times New Roman 和可供无头渲染的 Chrome/Chromium；可通过 CHROME_PATH 指定浏览器路径。运行 node generate_flowchart.mjs；或使用 --outdir 指定输出目录。仅生成 SVG 可运行 node generate_flowchart.mjs --svg-only，不需要渲染依赖。脚本不读取、导入或执行上位机源码。

建议题注：图 2-X Qt 上位机交互与串口通信流程图

保存位置：
Qt 项目：C:/Users/Administrator/Desktop/qt/QT/gold_game_panel_20261025/validation/qt_flowchart_report_20261030
当前工作区：C:\Users\Administrator\Documents\Codex\2026-10-30\qt-2-3-2-qt-pc\validation\qt_flowchart_report_20261030
聊天交付副本：C:\Users\Administrator\Documents\Codex\2026-10-30\qt-2-3-2-qt-pc\outputs\qt_flowchart_report_20261030
