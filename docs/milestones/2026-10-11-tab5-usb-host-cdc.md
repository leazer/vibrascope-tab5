# 2026-10-11 TAB5 USB host CDC 探头链路实板验收

## 结论

**4k/8k @ USB host CDC 判决点通过**：TAB5 交付率 == 探头产量 100%（逐样本），
探头侧零丢样、单 segment，全程 CRC/超时/重试为零。RS-485 时代 12-16 轮攻坚
未达到的 ICM 8k 稳定交付（最好 85.4%）在 CDC 链路上一次到位。

## 验收数据（probe HEAD 0cd7a49 系固件，TAB5 HEAD 0cd7a49，build 13:16:04）

### 启动自检（Step 2 ✅）

- `I (3310) probe_cdc: probe cdc connected` —— 探头上电即枚举（3.3s）；
- HAL boot 顺序 `usb host init` → `probe cdc init` → `worker start: open probe cdc` →
  `connect result: standby` 全部按预期；HID 驱动共存（`usba: Waiting for HID Device`）。

### 会话 1：4k/8k（StartDefault）@ 15.67s —— 主判决（Step 4 ✅）

| 指标 | 实测 | 门槛 | 判定 |
|---|---|---|---|
| ADXL 交付 | 62626 / 探头产量 62627（100.0%） | ≈100% | ✅ |
| ICM 交付 | 124262 / 124263（100.0%） | ≥98.5% | ✅ |
| 姿态 | 195/s | ≥195 | ✅ |
| CRC / tmo / rty / gap / uart | 0 / 0 / 0 / 0 / 0 | CRC=0 | ✅ |
| 探头侧 | drop 0、seg 1、gaps 0、q 峰值 512B | drop≈0 | ✅ |
| 泵 | fetches ~170/s、frames ~695/s、bmax≤2 | — | 健康 |
| TAB5 rx 环水位 | 峰值 1778 / 16384（11%） | — | 余量充足 |

线速粗算：~695 帧/s × ~250B ≈ 170KB/s，对照需求 ~155KB/s 与 FS bulk 理论
1.2MB/s——**计划风险清单第 3 条（CDC 吞吐）解除**。

### 会话 2：Slow 3.59s（Step 3 ✅）

ADXL 859/860、ICM 672/673（各 99.9%，差 1 样本为交付锚点约定）；姿态 160/s
（RS-485 时代 167/s 同类观察项，非 CDC 回归）；gap +2 = 已知启停边界伪影
（每 STOP→configure 一次计 2，与 round-13 记录一致）；CRC 0、drop 0、单段。

### 会话 3：Link5M 回显 + 4k/8k 5.50s（Step 6 ✅）

- `set link 5000000 result: true` —— SET_LINK 协议帧经 CDC 回显成功（波特率
  显示为纯回显装饰，线速恒为 USB）；
- 交付 == 产量 100%（21519/21519、42390/42390），CRC 0、drop 0；
- 本会话按键窗口较短（5.5s），与名义 ODR 的百分比差为启停延迟 + 传感器实际
  时钟容差（两流同向偏快 ~2-3%），非丢样——交付/产量比是唯一权威口径。

### 杂项观察

- 连按 Stop 时 `stop failed (phase STANDBY)`：设计内防御（非测量态拒绝），无害。
- 首次 configure（冷启动）gap 0，STOP 后再 configure gap +2：与探头段序号
  生命周期相关的既有边界行为，记录不阻塞。

## 未尽事项

- **Step 5 拔插鲁棒性未测**：本日志无断开/重插事件。C1（断开回调内 close）、
  C2（Fault 相位自动恢复）两条修复路径**尚未经硬件执行**——下次上板拔插一次
  即可闭环。
- Link1M 按钮未按（与 Link5M 同一回显路径，低风险）。
- 姿态 Slow 档 160/s（200 名义）：att 旁路 4 槽在慢速档的表现，维持观察项。

## 裁决记录（SDD）

- 推翻计划"断开后驱动自行释放句柄"假设：esp-usb 2.4.1 DEV_GONE 仅回调不释放，
  必须在回调内 `cdc_acm_host_close`（0cd7a49，随 write 句柄快照）。
- CDC write 失败锁 Fault 相位 → worker 重连门放宽接受 Fault（probe_link 冻结不动）。
- 既有的纳秒级 DEV_GONE/monitor 窗口竞态：实际不可达，记录不修。

## 终态（2026-10-12，用户决策：放弃 16k 实时流）

16k 攻坚在探针侧全部达成（满速生产、FIFO 健康、零溢出零 SPI 错），但
TAB5 主机线缆天花板 **~211KB/s（≈3335 bulk 包/s）** 低于 4k+16k 需求
（~271KB/s）与 ICM-16k 单流需求（~220KB/s），且被两个独立主机驱动
（cdc_acm 类驱动 / 裸排队管道）实测一致——约束在 IDF HCD host 模式的
逐包分发成本（usb_host API 之下），本层无法修复。用户决策：16k 实时流
不做；16k 单流模式（"16k" 按钮，实测 82% 交付、TIME_UNCERTAIN 诚实覆盖）
保留为探测能力，不代表产品档位。

**产品实时档位 = Slow / 4k+8k**，当前 HEAD（探头 6d741a7 + TAB5 e41d668）
实测：ADXL 28359/28359、ICM 55986/55986 双流 100%，drop/seg/gaps/CRC 全零，
拔插自动恢复、LED 时间线收敛窗口（启动 ~1-2s 红闪 = TIME_UNCERTAIN 诚实
告警，收敛后转绿）均按设计。**不需要回退**——验收后的全部改动或为
4k/8k 实测无扰，或仅在 16k 路径生效；当前 HEAD 即稳定基线。

遗留能力（未来如需 16k 实时流的路径）：IDF HCD 逐包分发成本优化
（upstream 级）；探头侧本地缓存回放（新硬件特性）。
