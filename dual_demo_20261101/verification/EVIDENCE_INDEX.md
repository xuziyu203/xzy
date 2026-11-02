# 交付证据索引与工作区备份

下表文件位于本包根目录下，均为本轮精选的小型记录。其原 JSON 内的绝对路径或 `validation/...` 路径只表示采集时工作区来源，不要求用户拥有那些目录，也不属于构建/加载依赖。完整原工程、Git 历史、测试脚本、失败尝试、大日志与逐对象依赖 .d 留在原工作区；本包不伪装为这些档案的完整副本。

原完整工作区：`C:/Users/Administrator/Desktop/1/three_renderer_async_quad_20261006`；基础原工程：`C:/Users/Administrator/Desktop/demo_0`。本轮完整记录在前者 `validation/joystick_recovery_20261101/`。原失灵 1002 行 TEL 大日志不入包，SHA-256 为 `968c982954be7185116e3ae5165f06dd42db47bb9c49948bbd305e93765ebd4c`，来源 `validation/frame_probe_20261101/board/load_GHDXP_FRAME_PULSE_20261101_161106/handoff_20261101_192941/tel_raw.txt`，同时已在本轮 evidence_audit 保存。摘要见 `incident_summary.json`。

`rc_preflight/` 仅证明旧 RC ZIP 的独立解压验收，不能替代最终 ZIP 验收。其报告中的 `preflight_verify_r1/r2`、stdout/stderr 和 .d 目录属于上述原工作区备份；本包保留简要机器结果。最终 ZIP 验收在归档 SHA 固定后生成独立旁车报告，以免将 ZIP 自身哈希写入 ZIP 形成循环依赖。

板上十轮使用受控 JTAG RAM 事件；pressure_v2 使用临时 RAM1000 命，源码/ELF 仍默认10。人工动作、采样边界与性能限制在各记录中区分，观察报告不自动表示通过。最终已完成323.9946秒无JTAG压力与原路径恢复十命；35秒固定方向仍未覆盖。具体接受范围见 PRESSURE_V2_RESULT_zh.md 和根 VERSIONS.json。

| 包内路径 | 字节数 | SHA-256 |
|---|---:|---|
| `verification/ROOT_CAUSE.md` | 4866 | `10f219756c3035f47242f82dc88aa2ac9d4b1fe06199824927d8827b59ae68e7` |
| `verification/TIMING.md` | 1001 | `e648940caa46f7bc4aff9e68a5d6fcea69c02d78af8c047eb8d18f2b79dd58b3` |
| `verification/OFFLINE_SUMMARY.json` | 2775 | `6b9dde89a15b5a8b4c8208ed3fb72d39003a73c979a03f1a38369264fbd845bd` |
| `verification/BOARD_RESTARTS_RESULT_zh.md` | 2614 | `a77c725bcf7a8a00d72d69096a3de6631af6d8cab29b2871335000a6e4313f69` |
| `verification/restarts/results.json` | 176973 | `481edd5281816a0d0a6ee26050162ae66cf3bbdefd267ac4e913bb87e6b05061` |
| `verification/restarts/independent.json` | 25256 | `582ff5ab7cd9e768f3168e79be140e18e1c03c81cbbe335336fb91e14c261a89` |
| `verification/MANUAL_V1_RESULT_zh.md` | 1782 | `13a980445963e07259b75440508d52b9fb28c38f51cfb5e99d984db3a274e64c` |
| `verification/manual_v1/analysis.json` | 10983 | `468d34da188d84fcb9a5b363e9b276215ffd223e6d2bb5c5911a36f3211c300e` |
| `verification/incident_summary.json` | 3025 | `8e9ace4d281dc36f417a6977a624b57a4b8e7ded8d29d83fb0f38156fedc4bba` |
| `verification/pressure_v2/analysis.json` | 25581 | `439010631f418410ac7235c776dcee86dded534bbd8d8d49dd26a957271da50a` |
| `verification/pressure_v2/OBSERVATIONS.md` | 2035 | `f85a5da6114deaad417a7dd541f23981214bd56e237042fa456711bad80f59df` |
| `verification/pressure_v2/ram_lives_setup.json` | 3941 | `6e202fefa31eb5c27f5d3e3715e8f58f91a72dc257d25336f4456f16e7c92a38` |
| `verification/pressure_v2/TEST_LIVES_AUDIT_zh.md` | 2149 | `cf8837ce6ee593274feade167c744f5f65575aa1b7cfa36e84451dab36afff5a` |
| `verification/LOAD_ASSET_SUMMARY.json` | 4845 | `f9cd03b81dd0d680d34b6cb8541c4225a89ae7e281bb1d45f7d8ff904ea7047e` |
| `verification/load_result.json` | 8362 | `046e52ffd08c3312ff79c945a0e054bb8ca0578b0702750a2a031786e58d145b` |
| `verification/cosim_results.json` | 2159 | `927ab320c5e55cad72cdefc6f4239ca32d00bcd4e9391fdaa255ab63a87cb61f` |
| `verification/cosim_source_hashes.json` | 2171 | `bada0832d1b5288d2c81f299f83ed1d11264d342fdf50ec2c2f18103333d8df7` |
| `verification/basic_timing_summary.json` | 7534 | `20880cea1fdf6c37d1a9a161c753afd7489f5b516e0fbc1f6b5c418062845837` |
| `verification/game_timing_comparison.json` | 3311 | `d198bf00750192cf8b390a92b95dc5a3b52c31b596f1c88e916eb23c6a89c781` |
| `verification/rc_preflight/README.md` | 1887 | `ceed78ba09eb878fc5b418dbcf946b70bd04e188785456ea29442f9ff112b4bb` |
| `verification/rc_preflight/long_path_failure.json` | 1871 | `fe23d913c7f4fc358d2fdb7e97a8dc05de600e988fcce1d4a1b71fcfa7b4954b` |
| `verification/rc_preflight/RESULTS.json` | 63819 | `c9546e87c897e663411c15f6e85b703e1e066f7e43b744e218997e646f327e5b` |
| `verification/manual_v1/raw.txt` | 44711 | `5a19aafa211c5f79fcfff7e63fe2447909db401347aa1e77c003569a9450a95f` |
| `verification/manual_v1/rx.jsonl` | 84129 | `d8e4c9a9fe5c5f82150da3895ab443fe0004c3e49997ff3326b44292deb114c4` |
| `verification/manual_v1/summary.json` | 1872 | `8ca8bfdc8cb34045b9f19cab604fe2c6006ea1e2e5c6c549ebde252a8c5a9979` |
| `verification/pressure_v2/raw.txt` | 133676 | `10c118b00b3016c77ac103a66151b1e8655d89952b6cc118d836b844d3f32ec8` |
| `verification/pressure_v2/rx.jsonl` | 248129 | `b65088a73a2df5f35e644fa1efa4f1b816b80ce05f495bd5239deb64e70b6cc9` |
| `verification/pressure_v2/summary.json` | 1920 | `9309d60e9f49e43c42fd138b1ab42ee9ed8827df10d243e34f246afb965cc695` |
| `verification/PRESSURE_V2_RESULT_zh.md` | 5346 | `298a762c46f6ad70345d591c0f77945ec3b842397c025cdc96dcbfb95b117c58` |
| `verification/DETAILED_WORK_RECORD_zh.md` | 19446 | `e6c1b78e5406f06ae66ac5e781dcaa66d5577ac4a533d0d48ff45e284da2c978` |
| `verification/pressure_v2/restore_normal_lives.json` | 24000 | `f70a9e2440c71cb59e023ad1513fd8c12fd62a55bcb6f2e0962e7b0889ba7d40` |
| `verification/pressure_v2/post_snapshot.json` | 11586 | `3507627b97b79d436ec7c02fda13e024087799c1c5f399d9be1d24faef4c4414` |
