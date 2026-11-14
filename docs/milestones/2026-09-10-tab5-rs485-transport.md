# 2026-09-10 TAB5 RS-485 传输适配器

## 完成内容

- 用 `ProbeUartEspIdf` 替换官方 115200 bps、127 字节缓冲、逐字节发送的回环演示任务。
- 固定使用 UART1：TX GPIO20、RX GPIO21、RTS/DIR GPIO34，8N1，RS-485 半双工模式。
- 初始速率 1 Mbps，使用 PLL 80 MHz 时钟；协议可切换到 5 Mbps并回读实际波特率。
- RX 环形缓冲预分配 32 KiB，TX 缓冲 2 KiB；整帧一次写入并等待发送完成。
- 记录 RX 高水位以及帧错误、校验错误、FIFO 溢出和环形缓冲满计数。
- 保留 `HalEsp32::rs485_init()` 生命周期入口，移除旧后台任务和 UI 监视队列耦合。

## 验证

- 编译期接口检查确认适配器实现 `IByteTransport`。
- ESP-IDF 5.4.4 / ESP32-P4 `build-link` 完整构建通过。
- 镜像 `vibranode_tab5.bin` 大小 `0x5746c0`，最小应用分区剩余 45%。

## 下一步

增加单一所有者的 FETCH 调度服务及链路快照，再接入板载链路验证界面。
