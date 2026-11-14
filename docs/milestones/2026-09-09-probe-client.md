# 2026-09-09 传输无关 ProbeClient

## 完成内容

- 新增 `IByteTransport`，协议客户端不依赖 ESP-IDF、UART 或 FreeRTOS。
- `ProbeClient::Context` 集中持有会话、请求号、帧序号、波特率、两路预期序号和 STOP 边界。
- 控制请求超时后使用相同编码帧重试，因此 request_id 保持不变。
- 忽略错误 session 的响应并计数；CRC 错误由流式解析器计数。
- 5 Mbps SET_LINK 被接受后才修改本机波特率，并以 GET_STATUS 确认；确认失败恢复 1 Mbps、复位解析器并重新 HELLO。
- FETCH 同步分发 ADXL、ICM、TIME_INFO、STATUS 和 ATTITUDE，直到匹配的 FETCH_DONE。
- STOP 保存两路最终序号，后续 FETCH 排空越过边界后才返回 Standby。

## 验证

- 假传输支持任意字节切片、虚拟时间、响应丢失和波特率失败。
- MSVC C++17 `/W4 /WX` 主机回归通过，覆盖 HELLO、同 request_id 重试、错误 session、5 Mbps 成功和确认失败回退。

## 下一步

实现 ESP-IDF UART1/RS-485 半双工传输，替换官方逐字节演示任务。
