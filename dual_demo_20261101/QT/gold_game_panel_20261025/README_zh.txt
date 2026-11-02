2D 图形渲染与弹幕交互平台 · Qt 科幻界面
=====================================

在上传的 gold_game_panel_20261025 基础上修改的 PC 工具。
原包使用 Tkinter；新入口 render2d_console.py 使用 Qt / PySide6。
本次只修改 PC 界面和输入发送，不改 bit、ELF、固件、素材、击毁阈值或弹池容量。

一、Windows 启动（先解压整个文件夹）
1. 首次使用双击 INSTALL_QT.cmd，安装 PySide6-Essentials 和 pyserial。
2. 双击 START_PANEL.cmd。
3. 不接开发板也能打开。默认“离线演示”开启，点击右侧捕获键盘区域，
   按 WASD / 空格 / Tab 就能看到高亮；离线演示不会发送键盘串口命令。

依赖：Python 3.11 或更新；默认沿用原来的 Python314 位置。
若不在该位置，启动器尝试 PATH 中的 python.exe，也可显式指定：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RUN_QT_PANEL.ps1 -Python "C:\你的Python路径\python.exe" -Port COM15
安装时同样可加 -Python 参数和 -Install。
也可直接执行 python -m pip install -r requirements.txt，再运行 python render2d_console.py。
Windows 启动脚本已静态检查；本次 Qt 界面实际测试平台是 Linux offscreen，未在用户 Windows 上启动。

二、修改内容
- 居中标题：2D 图形渲染与弹幕交互平台。
- 深色科幻风、青色与紫色强调、状态卡与串口日志。
- 右侧 W/A/S/D 方向键、SPACE 开火键、TAB 护盾键；按下高亮，松开恢复。
- 键盘或鼠标按住键帽均可操作；支持移动与开火组合输入。
- 高亮表示本地主机键态；“写入串口”与“板端已执行”分开：
  INPUT 协议没有逐包应答，界面不伪造已执行状态。
- 保留原包查询、摇杆校准、轻量→压力配置、开始/继续、护盾、暂停、退出。
- 保留本地配对身份校验、日志及原协议拒绝/超时处理。
- 指标来自最近实际回包；帧率由相邻 RUN_US / RUN_FRAMES 差值计算（有对应字段时），
  不是验收工具的滚动帧率门。普通弹数沿用 MEMBERS_VISIBLE，不冒充包含炮塔弹的总量。

三、实际串口控制
1. 关闭其他占用 COM15 的程序。确认板上运行原包匹配的 GOLD 配对。
2. 在连接设置中选择候选目录和串口，勾选加载声明，点击连接。
   连接不会自动开始游戏、重新烧录、上传素材或重置板卡。
3. 沿用原包的查看状态、配置和开始流程；游戏运行后勾选“键盘接管”。
4. 面板查询游戏状态、发送 PLAYER CONNECT，收到合法会话号后才启用输入。
5. W/A/S/D 移动；空格按住开火；Tab 每次按下发送一次 PLAYER SHIELD。
   Tab 不是输入包中的一个虚构位，也不发送不存在的 SHIELD OFF 命令。
   护盾持续时间和再次触发效果由固件决定。
6. Esc、取消接管、窗口失焦、编辑连接参数或断开都会释放键态。
   再次接管需主动开启；重新获得焦点时不会恢复旧按键。
7. 暂停、退出、死亡等导致会话撤销时，恢复游戏后重新勾选接管。

输入模式说明
- WASD/空格采用完整键态包，30ms 心跳，状态变化尽快发送；松开更新为新键态。
- 相反方向可同时按下，由原协议的方向差值抵消。
- Tab 长按不连发；操作系统的自动重复不会触发多次护盾。
- 键盘接管期间每秒读取一次 ENEMY STATUS；其他慢查询/校准/配置按钮暂不可用，
  释放接管后恢复。护盾、暂停、退出仍可用。
- GUI 租约为150ms；界面停止更新时撤销输入，防后台无限重复旧键态。
- 失联时真正释放仍依赖固件原有超时机制，主机不能证明断线后命令送达。
- 完整原始收发保存在 board_logs；高频 INPUT 心跳不逐条刷满界面日志。

四、固件兼容范围
保留上传包固定的 GOLD 配对校验：GOLD_SHIELD_FLOW_20261025_CANDIDATE_PENDING_BOARD。
不会绕过校验冒充支持 CMDM16 / WAVE40 或其他新固件。
bit / ELF / map 的固定哈希仍在 gold_game_panel.py；本地配对文件不随面板打包。
默认候选目录仍为：
C:\Users\Administrator\Desktop\1\three_renderer_async_quad_20261006\validation\gold_shield_flow_20261025\sol

键盘适配使用项目历史的 PLAYER CONNECT / PLAYER INPUT 会话协议。
当前 ZIP 没有固件源码，所以本次只验证主机假串口与真实 Qt 事件，未接实板。
若目标固件禁用主机输入、APP 菜单独占或不支持此命令，界面显示拒绝，
不会自动 ENEMY APP OFF、关闭摇杆、改场景或切固件。
原“配置轻量→压力”命令保留；触发击毁数由板端决定，不强改为5或10。
原 GOLD 专用摇杆 ABI 解析不自动兼容后续固件。
若要对接当前新候选，将 PROTOCOL_HANDOFF_zh.md 交给本地 Claude/Codex 核对。

五、验证与文件
python -E -B -m unittest test_gold_game_panel test_keyboard_transport test_qt_console -v
无显示环境运行测试可设置 QT_QPA_PLATFORM=offscreen。
界面预览：python render2d_console.py --preview
截图（不连接设备）：python render2d_console.py --screenshot preview.png
仅原包本地配对校验：python render2d_console.py --check-only --root "候选目录"
测试结果见 TEST_RESULTS.json / OFFLINE_TESTS.txt；BOARD_TESTED=false。
旧 README 和旧测试说明在 legacy_reference，仅供追溯，不是本次结果。
