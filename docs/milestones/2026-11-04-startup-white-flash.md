# TAB5 开机白闪修复

## 原因与处理

原 HAL 在 `bsp_display_start_with_config()` 后立即开启背光，此时动画 app
尚未创建，LVGL 默认活动屏幕为白色。LCD 初始化、首帧与背光之间缺少顺序保证。

`HalEsp32::init()` 现在先将背光 GPIO22 的输出锁存设为低电平，再配置输出，
让背光在外设初始化期间保持关闭；BSP 后续接管 LEDC，初始占空比仍为 0。
显示创建完成后，在 LVGL 互斥锁内设置旋转、将活动屏幕设为不透明 `#222222`，
全屏失效并调用 `lv_refr_now()` 完成首帧。现有旋转 flush 使用阻塞 PPA 写入扫描缓冲，
再等待 40 ms 让面板扫描新内容，最后恢复目标亮度并释放锁。

同时移除原 HAL 末尾未配对的 `bsp_display_unlock()`；显示设置现在有明确的加锁/解锁配对。
Logo 升起、节点落下与提示音时序未改。

## 验证

- ESP-IDF 原生构建通过：`build/boot-background-build.log`。
- COM10、2500000 bps 下载及镜像哈希验证通过：`build/boot-background-flash.log`。
- 启动日志：`build/boot-background-device.log`。记录首帧完成后才开启背光，
  随后动画正常进入 Logo、节点下落、提示音及 VibraScope；未见 panic/abort/brownout。
- 动画统计 reveal 29 帧、drop 15 帧，平均渲染 17929 us、最大 28957 us。
- 白闪消失的视觉结果仍待用户断电开机确认。串口不能证明屏幕视觉效果；
  应用初始化之前的 ROM/bootloader 阶段也不在此 GPIO 时序的控制范围内。

本轮未修改探头固件，也未自动提交 Git。
