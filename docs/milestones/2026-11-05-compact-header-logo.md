# 收起侧栏时显示顶栏 Logo

- 复用现有黄色符号 + 白色 VibraNode 字标图片，无新增图形资源。
- 顶栏左侧预留 144×28 px 区域，图片以 160/256 比例缩放至约 143×27 px。
- 侧栏收起时显示该区域，运行状态、温度、REC 组向右偏移 160 px；时钟保持居中，连接/电源图标保持靠右。
- 展开侧栏后隐藏顶栏小 Logo，状态组恢复原位置。底栏保持贯穿全宽。
- 后续对齐修正：状态、温度、REC 及圆点统一使用 `LEFT_MID` 锚点，不再手写各自 Y 坐标；时钟和右侧图标维持中心锚点。小 Logo 的白色字标位于整张图片较低处，采用居中后 -3 px 的视觉补偿，使字标与状态文字对齐。

验证：Desktop 与 ESP-IDF 构建通过（`build/compact-header-logo-desktop.log`、`build/compact-header-logo-native.log`）；Desktop 实际展开/收起检查通过，收起状态预览见 `docs/screenshots/2026-11-05-compact-header-logo.png`。
实机下载与启动监测日志：`build/compact-header-logo-flash.log`、`build/compact-header-logo-device.log`。
垂直居中版双构建日志：`build/header-center-desktop.log`、`build/header-center-native.log`；下载及启动日志：`build/header-center-flash.log`、`build/header-center-device.log`。Desktop 收起状态截图已更新。

本记录与对应顶栏修改一同归档。
