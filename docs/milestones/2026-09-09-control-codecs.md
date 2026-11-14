# 2026-09-09 控制协议编解码

## 本批完成

- 补齐 HELLO、SET_LINK、SET_CONFIG、START、FETCH、STOP、GET_STATUS、CALIBRATE 与 SAVE_CAL 请求编码。
- 补齐通用结果、HELLO、SET_CONFIG、STOP 最终边界、GET_STATUS、FETCH_DONE、校准状态与校准记录解码。
- 对消息类型、长度、结果码、状态值、保留字段和流数量执行边界检查。
- 用黄金字节和错误长度覆盖单流、双流、5 Mbps 链路配置及关键响应。

## 与文档草案的差异

实现以已经烧录并由 PC 工具验证的探头固件字节布局为准。当前 GET_STATUS 响应是 115 字节状态体，不带 request_id/result 前缀；FETCH_DONE 是 27 字节，包含两路积压批次/字节数和探头状态。后续应把冻结协议文档中的旧表格同步到这一实际布局。

TAB5 编码器已允许单流 SET_CONFIG，以支持产品要求的单独 ICM 高采样模式；当前探头固件仍只接受 17 字节双流配置，单流联调前需同步扩展探头端。

## 验证

- Windows 主机测试：MSVC C++17，`/W4 /WX`，`PASS: vhm_host_tests`。
- TAB5：ESP-IDF 5.4.4 / ESP32-P4 完整构建通过，生成 `vibranode_tab5.bin`，大小 `0x5746b0`，最小应用分区剩余 45%。
- Desktop：修改前的协议实现已增量构建通过；格式整理后 Ninja 曾无输出阻塞并被人工停止，协议源已由上述两套严格构建覆盖。

## 下一步

实现与 UART 无关的 `ProbeClient` 状态机，先用假传输测试握手、5 Mbps 切换确认、超时回退、请求匹配和 STOP 边界。
