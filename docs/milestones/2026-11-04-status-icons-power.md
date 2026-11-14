# 状态图标及电源显示

用户本轮要求：去掉坐标说明并进一步上移指标，Pitch/Roll/Yaw 靠近数值；传感器模型 X:Y:Z=2:2:1；固定默认4k+8k移除档位下拉；TEMP；连接和供电图标。

- 姿态区高100、指标区y162、控制区y452，指标比上版再上移14。角度名称x160、数值x218。模型顶点X/Y均±1，Z±0.5，保留传感器Z朝上/Y朝屏内的视基。
- 档位UI与切换回调删除；ScopeRuntime默认fast=true，原独立链路诊断能力不改。
- USB显示探头会话连接状态；Desktop模拟不冒充真实连接，USB带未知标记。无线关闭=灰色斜杠；待连接=空心点；连接中=刷新标记；已连接=黄标/勾；失败=警告；未知=问号。当前WiFi与BT未启用，保持关闭，不为点亮图标启动无线。
- HAL新增缓存状态 getter。WiFi仅本地事件更新，BT未初始化显示Off。后台低优先级任务每秒读INA226两笔寄存器，I2C错误返回无效；界面仅取缓存，不访问I2C。状态变更才重绘图标。
- INA226 BAT电压与电流：负电流为充电，与官方M5Unified实现一致。连续三次一致再切换图标；充电关闭时<4.5V视为无电池、≥5.5V视为电池；充电开启时高压但无明显电流保持未知，避免把空电池口被充电器抬高到8.4V误判为电池。实际负充电电流才显示电池+闪电。无电池显示插头，表示外部供电（不区分USB/DC）；电池条为电压粗分档，不宣称精确SOC。
- 没有使用现有 `bsp_usb_c_detect()` 判定USB供电：它实际读取IOE2.P6 CHG_STAT，并非独立VBUS检测。没有修改原有关闭充电的消噪设置。
- 图标字库使用仓内LVGL FontAwesome来源，含USB、WiFi、BT、电池、闪电、插头；构建脚本 `tools/generate-status-icons.py`。

验证：host回归通过（无电池/有电池/充电/充电器空口歧义/I2C失败与防抖）；Desktop和TAB5构建通过。独立代码复核无重要问题，修正未知电池标记颜色残留。Desktop实际检查关闭图标斜杠、模型归零/比例、TEMP和档位移除。预览图 `docs/screenshots/2026-11-04-status-icons.png`（Desktop硬件状态未知，不模拟真实电池）。

COM10 / 2500000bps 已下载，哈希验证通过。`build/status-icons-device.log` 两次电源状态 valid=true、battery=1（Absent）、charging=false，1912/1851mV、0mA，已确认无电池的外部供电识别/插头分支；探头connected=1、CRC/gaps/drops=0（此轮停机状态，不能当成RUN吞吐测试）。32s启动检查未见panic/brownout。当前没有电池，充电/电池条尚需装电池验收。

参考：本地M5Unified-0.2.21 `Power_Class.cpp`，M5Stack官方Tab5引脚/供电资料 https://docs.m5stack.com/en/core/Tab5 。
