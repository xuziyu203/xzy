# 实际板上十次逻辑重开：通过

执行记录：`board/restarts_event_v5_run1/results.json`，时间 2026-11-01 20:10:23.875–20:11:21.102，SHA-256 `481edd5281816a0d0a6ee26050162ae66cf3bbdefd267ac4e913bb87e6b05061`。另一个执行者已只读独立复核，`evidence_audit/restarts_v5_independent.json` 无未满足断言。

分类：真实 FPGA 上 **JTAG 注入 GameOver / 逻辑 KEY3 的十轮重开**，通过原游戏消费/状态机/显示确认/I²C/输入路径。不是十次实体 KEY3 消抖试验，不是自然碰撞判死，也不是五分钟压力。

| 轮次 | flow.round | 帧增长 | 新 pair 增长 | 无 JTAG 的 UART 首末跨度 |
|---|---|---:|---:|---:|
| 1 | 2→3 | 283 | 437 | 3.007717 s |
| 2 | 3→4 | 312 | 496 | 3.007711 s |
| 3 | 4→5 | 251 | 372 | 2.000458 s |
| 4 | 5→6 | 313 | 489 | 2.991739 s |
| 5 | 6→7 | 313 | 500 | 2.991752 s |
| 6 | 7→8 | 304 | 479 | 2.993161 s |
| 7 | 8→9 | 306 | 465 | 2.991779 s |
| 8 | 9→10 | 254 | 391 | 2.000291 s |
| 9 | 10→11 | 304 | 461 | 3.007590 s |
| 10 | 11→12 | 253 | 409 | 2.000063 s |

每轮均确认：原显示 GameOver 后，flow.round 恰增 1；APP_GAME、生命及显示生命恢复 10、两种 over 清零、模式回 LIGHT；帧/新 pair/序号前进；app_errors/en_error 无新增。恢复后 UART 连续至少两秒有 hp>0、valid/armed=1、latched=0、jp>=2、jf=0，JOY 序号增长且 cap 未耗尽。随后 RAM 健康/恢复状态再次符合条件。37 个选定 TEL 窗口共 3667 新 pair，全部 fault=0，未自动耗尽。首轮从保留 PRESSURE phase 的 GameOver 回到 LIGHT，后九轮为 LIGHT 重开。

调试扰动必须保留：第一次 pre 健康快照累计 fault=5，最后 post=33；各轮 pre→post 增量合计 21，其余 7 位于轮间快照/扰动区间。累计 stale 保持 1，没有新增。恢复后选定窗口 fault 全为 0，不代表整个 JTAG 测试期间零 fault。pause/读取可能中断 I²C；不能拿这些扰动窗口作为无扰动性能回归。

UART 行报告的是前一窗口：首轮第一个选定行 fr=58、lf=2、im=35494 µs，第六轮首行 fr=59；它们是恢复/边界窗口。每轮去掉首个可能混合的窗口后，余下 27 个窗口均 fr=60、lf=0。五分钟性能应单独用无 JTAG 压力段判定。

运行最终 CPU running，自有 OpenOCD exit=0。之前两个 0/10 的自动化失败完整保留，未覆盖。新固件/bit 未改变：GHDB1 ELF `0fac4deabec6a794316c8672ec37bbe3db7201c74c6fd5c3fc0880a976101087`，FRAME_DONE_PULSE bit `72d4f01497b5599bc633a35162388f312a0631ea17b348fdf97a5efea335bd54`。
