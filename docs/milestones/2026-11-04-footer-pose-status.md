# 模型交互、录制提示与贯穿底栏

## 界面

- 模型点击区域 120×100，点击后白色背景以 50% 不透明度开始、240 ms ease-out 衰减至透明；每次点击重新触发。仍仅在探头连接且姿态有效时归零，不恢复旧参考。
- 模型投影中心上移 8 px；下方显示黄色“已连接”或灰色“未连接”。断连模型和坐标轴文字均变灰。
- 模型右侧 x=124 添加 1 px 分隔线，y=8、高 82；Pitch/Roll/Yaw 标签移至 x=138，数值 x=201。
- 温度实际字符串由 `C` 改为 `℃`；重新生成 16/18 px 子集字体以包含 U+2103。
- REC 前添加圆点；空闲同为灰色，无 `--`、无时长；实际 recording 为真时圆点、REC 与时长均变红。只在录制状态改变时修改圆点背景色，避免周期性无意义重绘。
- 底栏固定 x=0、宽 1280，不随侧栏隐藏而移动；最左为无背景侧栏按钮，右邻为运行/停止按钮，其后为波形配置。侧栏高度截止底栏上缘。
- 删除“采集控制”及其原右侧状态值，包括演示/采集中/等待探头等文本；录制、分析源、设置上移。错误 toast 及各图自身质量提示保留。

## USB 状态来源

USB 图标改为 HAL `getComputerUsbState()`，不再使用探头会话状态。
本地官方 `references/M5STACK_TAB5/datasheets/Tab5_Schematics_PDF.pdf` 标明 USB-C 数据接 P4 GPIO24/25 Serial/JTAG PHY，USB-A 主机为独立通道。
实机采用 ESP-IDF 5.4.4 `usb_serial_jtag_is_connected()`：通过主机 SOF 判断活跃 USB 连接，串口无需打开；普通供电器不产生 SOF，主机休眠/挂起时也可能判为断开。
不使用 IOE2.P6 的 CHG_STAT 判电脑连接。Desktop 缺少该物理接口，返回 Unknown。

## 验证与限制

- Desktop 构建通过，预览确认 ℃、模型文字/分隔线、灰色 REC 无占位时长、侧栏状态文本移除。
- 实际点击模型归零成功；收起侧栏后底栏保持固定、绘图区展开，重新展开亦正常。
- 主机回归 `vhm_host_tests` PASS；无新增模拟实现细节的测试。
- ESP-IDF 构建及 COM10 2500000 bps 下载验证见 `build/dashboard-controls-native-final.log`、`build/dashboard-controls-flash-final.log`，启动监测见 `build/dashboard-controls-device-final.log`。
- 预览图 `docs/screenshots/2026-11-04-footer-pose-status.png`。
- Desktop 从当前启动器运行时录制文件创建失败，未验证成功录制时的红色计时状态；未为测试修改录制文件路径。实机录制仍需 SD 卡，成功录制、电脑 USB 拔插和断开探头的视觉变化待实测。
- 点击闪白的时长/透明度已按代码设置，截图未捕获该短暂阶段，流畅度待实机目测。

后续 SD 录制修复与红色 REC 验证见 `2026-11-05-history-sd-recording.md`；最终窗口上限见 `2026-11-05-two-second-rollback.md`。本记录与对应界面修改一同归档。
