# 2026-09-10 链路验证应用

## 完成内容

- 启动后直接显示专用链路验证页，不进入官方硬件演示 Launcher。
- 后台 FreeRTOS 任务独占 `ProbeClient` 和 UART；LVGL 线程仅提交原子命令并读取原子快照。
- 支持连接、1/5 Mbps 切换、低速双流、4k/8k 双流、STOP 和持续 FETCH。
- 实时显示两路样本数、CRC、超时、重试、错误 session、序号缺口、UART 错误和 RX 高水位。
- Desktop 分支只产生确定性模拟数据，不打开串口。

## 构建结果

- ESP-IDF 5.4.4 / ESP32-P4 构建通过。
- 可烧录镜像：`platforms/tab5/build-link/vibranode_tab5.bin`。
- 镜像大小 `0x2be710`，最小应用分区剩余 73%。
- 当前 Windows Desktop Ninja 在启动编译子进程前持续无输出阻塞；已停止，没有将其记为通过。主机协议测试和目标完整构建均通过。

## 上机边界

当前 Windows 未枚举出串口，无法代替用户烧录。连接 TAB5 USB 后按 `docs/HARDWARE_CHECK_5MBPS.md` 执行；首轮结果用于确认 5 Mbps 和调整 FETCH 周期/额度。
