# 双 demo RC 预验收结果

状态：**PASS（RC 预验收，非最终 ZIP 验收）**。

- ZIP：`dual_demo_rc.zip`
- 实际字节数：10,749,690
- SHA-256：`7e47acbf85af1b2f381989ae109d8ebf31641870ee3c2e2eb3e8324c43053021`
- 成功解压根：`C:/Users/Administrator/AppData/Local/Temp/dual demo rc verify`
- 证据：`preflight_verify_r2/RESULTS.json` 及所有逐步 stdout/stderr、dependency_proof.json、.d。

逐文件 manifest、根和两 demo 的 SHA256SUMS、两套源码清单全部通过。基础干净编译 ELF 为 `4a481583cb33e41a5478d10a9012aa53b44ffca055950293527f30c10f2efb3a`；游戏为 `0fac4deabec6a794316c8672ec37bbe3db7201c74c6fd5c3fc0880a976101087`，均与压缩包内配对完整一致，源码没有漂移。

基础的 15 个对象文件对应 15 次真实编译参数预处理依赖扫描，共 80 个唯一源码/头文件依赖；游戏为 30 个对象、112 个唯一依赖。所有路径均属于相应新解压工程或已声明的 RISC-V IDE 安装根，未发现回读原工作区的依赖。每个对象都有 .d 和解析记录。

两套 FPGA 只执行 prepare/依赖检查，不综合。基础 loader 仅计划，游戏 program check-only、load_session prepare-only、upload_single check-only 全部通过。没有串口/JTAG/FPGA编程操作。原暂存目录未编译、未修改；原 ZIP hash 复核不变。

首次 r1 长路径尝试保留在 `preflight_verify_r1`：头文件真实存在，但 GCC 未规约 include 完整路径达到 260 字符而失败。问题是旧 Windows 工具链路径上限，不是漏文件；未改变构建工具或源码。最终用户建议解压到 `C:/demos` 或桌面短目录，目录名可以含空格。

最终修复/板测报告和 README 更新后的 ZIP 仍须独立重新执行整套解压编译验收。当前 RC 结果不改变 PARTIAL 的板测状态，不补算未完成的测试。
