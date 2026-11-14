# 2026-09-10 LVGL 内存分配器修复（主界面崩溃）

## 现象

实板首次运行 `b6c534f` 构建产物：启动动画正常播放，进入主界面（launcher）闪现后死机重启。两次开机崩溃点不同但同相位：

- `lv_inv_area`（lv_refr.c:274）
- `lv_draw_add_task`（lv_draw.c:95），`Store access fault`，MTVAL=0x8、A0=0x0

MTVAL=0x8 与 `new_task->area = *coords` 的字段偏移 8 吻合：`lv_malloc_zeroed` 返回 NULL 后直接解引用。

## 根因

LVGL 9.2.2 的 Kconfig 中不存在旧版 `CONFIG_LV_MEM_CUSTOM` / `CONFIG_LV_MEMCPY_MEMSET_STD` 符号（LVGL 8 时代遗留）。`sdkconfig.defaults` 里写了这两行被静默忽略，实际生效为默认的内置分配器 + `LV_MEM_SIZE_KILOBYTES=64` + 0 扩容，即内部 RAM BSS 里一块固定 64 KiB 池（map 中 `.bss.work_mem_int.4`，0x10000）。启动动画 UI 轻量可以在池内运行；launcher 全套对象创建时池耗尽，`lv_malloc` 返回 NULL，绘制线程空指针崩溃。

官方 M5Tab5-UserDemo 0.4 生效的 sdkconfig（随仓库提交）是人工调过的：`# CONFIG_LV_USE_BUILTIN_MALLOC is not set` + `CONFIG_LV_USE_CLIB_MALLOC=y`，走 ESP-IDF 堆（内部不足可回退 PSRAM），因此官方原版不会触发。我们首次从 defaults 重新生成 sdkconfig，差异即暴露。

## 修复

- `platforms/tab5/sdkconfig.defaults`：删除 5 个死符号（`ESP_BROOKESIA_MEMORY_USE_CUSTOM`、`LV_COLOR_SCREEN_TRANSP`、`LV_DISP_DEF_REFR_PERIOD`、`LV_MEM_CUSTOM`、`LV_MEMCPY_MEMSET_STD`），新增 `CONFIG_LV_USE_CLIB_MALLOC=y` 并注释说明。刷新周期保持 Kconfig 默认 33 ms，与官方实际生效值一致。
- 重新生成 `platforms/tab5/sdkconfig`。

## 验证

- 完整重建通过（ESP-IDF 5.4.4 / ESP32-P4），无 unknown Kconfig 警告。
- 新 map 中 `work_mem_int` 0 处，`lv_malloc_core` 来自 `lv_mem_core_clib.c`。
- `vibranode_tab5.bin` 2,867,088 字节（0x2BBF90），最小应用分区余量约 73%。
- 实板验证待做：烧录后确认主界面稳定进入、无重启。

## 实板第二轮反馈（8e48d1c）

- 内存池修复已生效：heap_init RAM 堆从 167 KiB 增至 231 KiB（64KB 池让位给堆），`work_mem_int` 消失。
- 原崩溃不再复现，但暴露出真正的底层问题：`lv_inv_area: Asserted ... !disp->rendering_in_progress (Invalidate area is not allowed during rendering.)`——Mooncake 在 app_main 任务直接操作 LVGL，与 esp_lvgl_port 渲染任务并发、无锁。首轮 64KB 池崩溃本质也是该竞态打崩非线程安全的内置分配器。
- 修复：`app::Update()` 中 Mooncake update 循环外套 `LvglLockGuard`（esp_lvgl_port 为递归互斥锁，桌面平台空实现）。提交随本文档同批。
- 遗留观察：launcher 触发 `startWifiAp()` 后，板载 C6（esp_hosted SDIO 从机）初始化超时（`send_op_cond returned 0x107`，日志提示 reboot），随后 Core 1 跳转 0 地址崩溃（esp_hosted 错误路径）。与 LVGL 无关，先按提示断电重启观察；若每次开机必现，再把 WiFi 启动推迟或禁用（当前验证阶段不需要 WiFi）。

## 实板第三轮反馈（160ee4c）

- LVGL 加锁生效：`lv_inv_area` 断言不再出现，launcher 创建过程无 LVGL 异常。
- C6/SDIO 问题确定性复现：彻底断电重启后仍 `send_op_cond returned 0x107`（C6 从机对 SDIO 握手无应答，疑似未烧 esp_hosted 从机固件或固件不匹配），esp_hosted 失败路径随后跳空指针崩 Core 1（MEPC=0/RA=0，栈回溯损坏）。
- 处置：注释掉 `AppStartupAnim::onRunning` 中的 `GetHAL()->startWifiAp()`，开机不再拉起 WiFi/esp_hosted 整条链路（链路验证阶段不需要 WiFi）。恢复 WiFi 前需先用 M5Burner/官方烧录器核对并刷新 C6 从机固件，再验证 esp_hosted 失败路径或直接保证握手成功。

## 实板第四轮反馈（ff345a0）

- 主界面稳定，C6/SDIO 崩溃随 WiFi 自启停用而消失。
- LinkValidation 实测：Connect 后点击 Slow/4k+8k 死机，`Stack protection fault` in task `probe_link`，崩点 `ProbeClient::dispatch_stream`（probe_client.cpp:100），SP 越过 8KB 栈下界 0x80B。链路证据表明探头实际在应答（HELLO 会话匹配、流帧已到达派发层）。
- 根因：拉流调用链 worker→fetch(Frame/FrameBytes 各~1KB)→consume_until(256B)→parse_available(Frame ~1KB)→dispatch_stream(SampleBatch ~3.1KB，108×28B) 叠加超过 8KB 任务栈。
- 修复与增强（本轮提交）：
  - `probe_link` 任务栈 8192→16384，周期打印 `uxTaskGetStackHighWaterMark` 验证余量。
  - 按用户要求为按键与 worker 逻辑加日志（均在 app 层，probe_link 组件保持主机回归干净）：按键命令名、connect/设置链路/配置/启动/停止结果、fetch 失败（1s 节流）、每秒状态汇总行（阶段/波特率/样本/计数器/栈水位）、CountingSink 首批与每 500 批的流帧明细。

## 实板第五轮反馈（619998c 诊断 + 排水修复）

- 全流程（Connect/1M↔5M 互切/Slow/STOP）无死机；**TAB5↔探头 5Mbps SET_LINK 握手成功**（PC 转换器做不到），STOP 边界正常返回。
- `configure failed (phase MEASURING)` 为设计内防护：测量中须先 STOP。
- 探头侧计数定位样本丢失根因：`Δdrop+Δtot` = ADXL 253.8/s（250Hz ✓）、ICM 201.2/s（200Hz ✓），生产正常；交付率≈FETCH 循环率（三路各 ~55-65/s）且 `drop` 以 ~188/s、~145/s 猛涨——**探头软件队列极浅（约 1 帧/流），TAB5 拉 5ms 一轮跟不上即溢出**。丢样本按探头设计触发 `end_segment`（sequence 清零），即此前观察到的序号乱跳与 gap 增长的根源。PC 工具能跑满是因为紧凑循环无延时。
- 修复：`fetch()` 增加可选 `FetchDone*` 出参；worker 测量态按 `backlog` 连续排水（至积压清零，上限 48 轮），fetch 失败日志 1s 节流。
- 验证：MSVC 主机回归 `/W4 /WX` PASS（新增 `tests/host/run-host-tests.cmd` 一键运行，vcvars+Ninja，VS18 自带 generator 与 IDF cmake 3.30 不兼容改用 Ninja）；TAB5 完整构建通过。
- 实板复测待做：Slow 30s，预期 `probe` 行 `drop` 停止增长、`seg` 稳定、TAB5 `gap` 归零、样本速率 ~250/200/s。
- 遗留：探头按样本成帧（count=1，每样本 32B 头开销 78%）是高采样率吞吐的硬瓶颈之一；默认 4k/8k（≈528KB/s 载荷）在 1Mbps 下本就不可行，需 5Mbps + 探头侧批量成帧，与已推迟的高采样率瓶颈专项合并处理。

- 烧录后确认：主界面稳定进入、无 lv_inv_area 断言、无重启。
- C6/SDIO 崩溃是否复现。

## 实板第六轮反馈（e7ec170 → 2ad1692）

- backlog 排水循环无效：`backlog` 恒为 0——探头队列深度约 1 帧/流，**丢样发生在两次 FETCH 之间的新样本入队时**，排水无从排起。读探头源码确认：window 在两路空闲时立即关闭（不等新样本）。
- 唯一 TAB5 侧杠杆是把 FETCH 间隔压到样本周期（ADXL 250Hz = 4ms）以内：测量态外层延时 5ms→1 tick，每秒日志增加 `fetches/frames/bmax` 验证泵速。
- 1Mbps 下 FETCH 往返约 2-3ms，贴近 4ms 红线，可能有零星残余丢样；5Mbps 往返 <1ms 应干净。结构性的解仍是探头侧批量成帧（count=1 时 32B 头开销 78%），归入高采样率瓶颈专项。

## 实板第七轮反馈（2ad1692 → 读延迟修复）

- 关键数据：`fetches 10`/s（每周期恰 ~100ms）、`frames ~210`/s（每 fetch ~21 帧）、`tmo 0`、与 ODR 无关（4k/8k 也是 ~11/s）。
- 根因在 TAB5 transport：`uart_read_bytes(256, timeout)` 是"凑满长度或超时"语义。低速窗口 20ms 产 ~950B，最后一个不满 256B 的尾块（含 FETCH_DONE）一直等到 100ms 超时才返回——FetchDone 每次都被拖着晚 ~80ms 交付，窗口之间的生产全部溢出丢样。`tmo 0` 是因为 FetchDone 在超时块里仍被正常解析。
- 修复：`ProbeUartEspIdf::read()` 改为"缓冲区有数据读多少算多少；空则阻塞等第 1 字节、到了立即排空剩余"，消除凑包延迟。
- 预期：fetch 周期 ~20ms（窗口时长主导）→ fetches ~45/s、交付≈生产（慢速 250/200/200）、probe `drop`/`seg` 停涨、TAB5 `gap` 归零。

## 实板第八轮反馈（6d0496f 读修复后，Slow 模式验收数据）

- `fetches` 10 → **250~350/s @1Mbps、~930/s @5Mbps**，读凑包修复完全生效。
- 交付速率：1M 下 ADXL 224/s（90%）、ICM 179/s（90%）；**5M 下 ADXL 242/s（97%）、ICM 193/s（96.5%）、姿态 186/s**。
- 探头 `drop`：188/s → ~30/s @1M、**~5/s @5M**；`gap` 随之从 +20/s 降到 +2/s；STOP 边界在 5M 下正常返回。
- 结论：低速链路达标（推荐 5Mbps 配置，接近无损）。残余 ~3% 丢样源于探头队列深度≈1、入队即丢、丢则断段重置序号，结构性解法是探头侧批量成帧（count=1 时每样本 32B 头开销 78%），与 4k/8k 吞吐专项合并推进。
- 遗留观察：`tmo` 全程累计 17~20 次（偶发 FETCH 100ms 超时），占比低，先观察；串口命令闭环调试方案按用户偏好撤回，验收走触屏+monitor 流程。

## 构建环境注意

从 Git Bash 直接调用 `tools/build-tab5-windows.ps1` 会失败：Git Bash 注入的 `MSYSTEM` 会透传到 PowerShell，`idf_tools.py` 检测到即拒绝（MSys/Mingw not supported）。需经 cmd 会话删除该变量后再启动 PowerShell：

```
cmd /c "set MSYSTEM=& powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-tab5-windows.ps1"
```

## 下一步

实板复测主界面稳定性；随后按 HARDWARE_CHECK_5MBPS.md 继续 LinkValidation 验证页与探头联调。

## 实板第九轮反馈（5badb1f 探头合并 + 预算修复）

- **Slow 模式验收通过**：1Mbps 交付 ADXL 250/s、ICM 198/s，探头 drop=0、seg 恒 1、TAB5 gap=0、STOP 边界与总数精确吻合（adxl 5147/icm 4070）；5Mbps 同样干净（248/193）。探头合并后批次以 count=1 为主属正常——拉流泵排空快于合并窗口，合并仅在排水滞后时兜底（5M 下偶发 count 2）。
- **4k/8k@5Mbps 首测失败但定位明确**：交付 ADXL 41%/ICM 11%，fetches 塌缩到 48/s（每周期打满 20ms 窗口）。根因是 TAB5 自设的窗口预算 1600B——探头以 FETCH budget 为单窗口发送上限，每窗口只带走 ~1.4KB（~4 个合并帧），生产 ~209KB/s 大部分在探头侧溢出（ICM drop +7000/s）。合并已生效（日志出现 count 31/55 批次，即 512B 槽上限形态）。
- 修复（`b81b7dd`）：FETCH 预算 1600→8192B。5M 线速下 8KB ≈ 13ms 传输 < 20ms 窗口 deadline，单窗口可带走 ~8KB，产能 ~209KB/s < 400KB/s 预算覆盖。
- 待实板：仅重烧 TAB5（探头无需动），StartDefault@5Mbps 复测，预期 probe drop 大幅收敛、交付逼近 4000/8000。

## 实板第十轮反馈（b81b7dd 预算 8192 → 14f6e16 预算 16384）

- Slow 复测依旧干净（250/198，drop 0，STOP 边界精确）。
- **4k/8k@5Mbps 交付率 11-41% → 73-81%**（ADXL 3250/s、ICM 5810/s、姿态从饿死恢复 147/s），探头合并批次大量出现（count 9~56）；探头 drop 从 ~7000/s 降到 ~1450/s。
- 残余瓶颈定位：`fetches` 恒 48/s = 每个 FETCH 打满 20ms 窗口 deadline——8192B 预算在 5M 线速 13ms 发完后窗口因数据不断流继续开到 deadline。线上交付 ~380KB/s 已接近 5M 实效上限，且持续满载下 RS485 出现零星错误（uart 0→53、crc 30），5M 信号裕量是真实约束。
- 下一步实验（`14f6e16`）：预算 16384B > 20ms×500KB/s=10KB 满线速，让"预算耗尽"不再先于"窗口到期"，交付由线速与探头队列决定。若交付逼近全量但 uart/crc 错误持续增长，则 4k/8k 需要产品决策（降 ODR 组合 / 短时捕获模式 / 信号链排查）。

## 实板第十一轮反馈（14f6e16 预算 16384）与 64KB RAM 方案

- **4k/8k@5Mbps 再进一步**：交付 ADXL 3792/s（93.6%）、ICM 6707/s（83.8%）、姿态 168/s（不再饿死）；探头 drop s1 318/s、s2 1367/s；STOP 边界正常。
- 预算已移出方程（16384B > 20ms 满线速 10KB），`fetches` 仍 48/s 且每窗口交付 ~8KB 不再增长——瓶颈确认为**探头发射节奏**：合并帧均 ~400B，5M 下 0.64ms/帧 + 每轮回主循环的间隙，线缆利用率 ~76%，帧大小被 512B 槽上限锁死。
- 持续满载下 RS485 错误累积（uart 0→72、crc 30）：5M 信号裕量在 ~76% 线缆占用时已现零星错误，是后续满载方案的硬件风险项。
- **64KB SRAM 方案（用户确认芯片支持）**：EOPB0=0xFA（USD 0x1FFFF810，flash_user_system_data_erase/program 原语已有），需掉电重启生效；同时 MDK IRAM 0x8000→0x10000、batch data[] 512→1024（4 槽 ×2 路仅 +4KB ZI，可再加深队列），合并上限即达线缆满批 108/74，发射帧数降 ~350/s、帧均 ~1KB → 线缆利用率 >90%，4k/8k 全量可达。EOPB0 为器件级一次性配置，实施前需用户确认。
