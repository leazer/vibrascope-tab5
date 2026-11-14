# TAB5 1 Mbps / 5 Mbps 链路上机验证

## 准备

1. TAB5 RS-485：TX GPIO20、RX GPIO21、DIR GPIO34；使用板载接口时无需另接这三个 GPIO。
2. TAB5 与探头 A/B、GND 对应连接，链路两端各保留一个 120 Ω 终端，中间不重复终端。
3. 探头烧录当前生产模式固件；记录探头提交号和 TAB5 提交号。
4. 首次验证不要插 microSD，先隔离通信吞吐问题。

## 编译和烧录

```powershell
Set-Location E:\WorkSpaces\6_digikey2026\firmware\tab5
.\tools\build-tab5-windows.ps1 -Flash -Port COMx
```

烧录后屏幕应进入 `VibraNode Link Validation`。界面显示当前状态、波特率、两路样本数、CRC、超时、重试、错误 session、序号缺口、UART 错误和 RX 高水位。

## 有序验证

1. 点 `Connect`，应在 1 Mbps 进入 `STANDBY`。若保持 `DISCONNECTED`，先检查 A/B 极性和终端。
2. 点 `5 Mbps`。通过条件：界面保持 `STANDBY`、波特率显示 5000000，随后状态查询成功。确认失败时会自动回到 1 Mbps 并重新 HELLO。
3. 点 `Slow`，连续 10 分钟。ADXL 与 ICM 样本数都应持续增加，CRC、序号缺口、UART 错误保持 0。
4. 点 `STOP`，状态先经过 `STOPPING`，排空最终边界后回到 `STANDBY`。
5. 点 `4k + 8k`，先运行 60 秒观察 RX 高水位，再按结果延长到 10 分钟。记录探头 GET_STATUS 中队列丢弃、FIFO 溢出和格式错误。

## 结果记录

每次记录：连接方式、终端、电源、两端提交号、速率、配置、时长、ADXL/ICM 样本数、CRC、序号缺口、UART 四类错误、RX 高水位和探头队列/FIFO/格式计数。任何非零丢样计数都不列入可用配置。

ICM 32 kSPS 单流需要探头端先接受单流 SET_CONFIG；TAB5 编码器已经支持，但当前探头生产固件仍要求双流 17 字节配置，因此本轮界面暂不开放该按钮。
