# LVGL 9.2.2 图片素材查看器设计

## 1. 目标

在 `firmware/tab5` 仓库内提供一个 Windows 可直接使用的离线图片素材查看器。用户将 LVGL 图片生成器产生的 `.c` 或 `.bin` 文件拖入页面后，工具自动识别素材、解析元数据、解码像素并显示预览，同时给出尺寸、颜色格式、stride、数据长度和解析诊断。

工具以当前 TAB5 固件实际依赖的 LVGL 为唯一格式基准：

- LVGL 9.2.2；
- `LV_COLOR_DEPTH = 16`；
- 小端系统；
- `LV_IMAGE_HEADER_MAGIC = 0x19`；
- 当前工程关闭 RLE 和 LZ4。

首要成功标准是仓库 `app/assets/images/*.c` 中的全部业务图片可以正确解析和预览，其中现有素材主要为 `RGB565`，`mouse_cursor.c` 为 `RGB565A8`。

## 2. 产品形态与范围

开发工程使用 TypeScript、Vite 和原生浏览器 API，生产构建输出单个离线 HTML 文件：

```text
tools/lvgl-image-viewer/dist/lvgl-image-viewer.html
```

用户双击该文件即可在 Windows 的 Edge 或 Chrome 中使用，不需要 Python、Node.js、服务器或网络连接。所有输入只在浏览器内存中处理，不上传素材。

第一版包含：

- 拖放或文件选择器导入一个或多个 `.c`、`.bin` 文件；
- 单个 C 文件内多个 `lv_image_dsc_t` 的发现与选择；
- 图片预览、平移、缩放、适应窗口和 1:1 显示；
- 棋盘、白色、黑色和自定义预览背景；
- 图片元数据、像素坐标和值、结构化诊断；
- 将当前解码结果导出为 PNG；
- 大图在 Web Worker 中解析和解码；
- 构建为单 HTML 文件。

第一版不包含：

- 编辑像素或元数据；
- PNG/JPEG 到 LVGL 素材的反向转换；
- 批量格式转换；
- 项目目录监控；
- LVGL 8 或其他 LVGL 版本兼容；
- RLE、LZ4、RAW、RAW_ALPHA 或 YUV 解码；
- 从互联网加载文件。

## 3. 总体架构

数据流如下：

```text
File/Blob
  -> InputRouter
     -> CAssetParser | BinAssetParser
  -> ImageAsset
  -> PixelDecoderRegistry
  -> RGBA8888 buffer
  -> CanvasRenderer
```

模块边界：

- `InputRouter` 根据扩展名和内容选择解析器，不负责像素解释。
- `CAssetParser` 只识别允许的 C 语法子集并建立描述符到字节数组的引用。
- `BinAssetParser` 解析 LVGL 9.2.2 的 12 字节图片头和后续数据。
- `ImageAsset` 是文件格式无关的中间模型。
- `PixelDecoderRegistry` 根据颜色格式分发到独立解码器。
- `CanvasRenderer` 只接收 RGBA8888，不感知 C/BIN 或 LVGL 结构。
- Web Worker 承担文本分词、字节解析和像素解码；主线程承担状态管理和绘制。

统一模型至少包含：

```ts
interface ImageAsset {
  id: string;
  name: string;
  sourceFileName: string;
  sourceType: "c" | "bin";
  width: number;
  height: number;
  stride: number;
  colorFormat: LvColorFormat;
  flags: number;
  dataSize: number;
  data: Uint8Array;
  diagnostics: Diagnostic[];
}
```

解析器返回零个或多个素材及文件级诊断，以允许一个 C 文件部分成功。

## 4. C 素材解析

C 文件不得被执行、编译或传给 `eval`/`Function`。解析器实现受限词法分析和语法提取，支持 LVGL 图片生成器常见结构：

- `uint8_t name[] = { ... }` 形式的字节数组；
- 十六进制和十进制整数、注释、空白、换行和尾逗号；
- `lv_image_dsc_t` 指定初始化器；
- `.header.cf`、`.header.magic`、`.header.flags`；
- `.header.w`、`.header.h`、`.header.stride`；
- `.data_size`、`.data`；
- 常量整数表达式中的括号及 `+ - * / << >> | &`。

不允许标识符求值、宏展开、函数调用、字符串解释、指针运算或预处理器执行。include、属性宏和条件编译行作为无关文本忽略。

解析器先收集命名字节数组，再收集 `lv_image_dsc_t`，最后按 `.data` 引用建立关系。不能假定一个文件只有一个数组或描述符。

对每个候选素材验证：

- 数据数组存在；
- 宽高为正且未超过安全上限；
- 颜色格式已知；
- 显式 stride 合法，或可根据 LVGL 规则计算；
- `data_size` 不超过数组实际长度；
- 有效数据足够覆盖图片、调色板和 alpha 平面；
- 声明 magic 时其值为 `LV_IMAGE_HEADER_MAGIC`。

诊断尽可能带源文件行号和描述符名。

## 5. BIN 素材解析

LVGL 9.2.2 小端图片头固定为 12 字节：

```text
offset  size  field
0       1     magic, 0x19
1       1     color format
2       2     flags
4       2     width
6       2     height
8       2     stride
10      2     reserved
12      ...   image data
```

BIN 解析不能只依赖扩展名。解析器须校验 magic、已知颜色格式、非零且合理的尺寸、合法 stride、flags 和文件总长度。普通固件 BIN 或截断文件必须拒绝。

设置 `LV_IMAGE_FLAGS_COMPRESSED` 的文件仍显示可读取的头信息，但标记为“已识别、暂不支持解码”，不得误报为损坏文件。

## 6. stride 与颜色解码

正式验收格式：

- `LV_COLOR_FORMAT_RGB565`；
- `LV_COLOR_FORMAT_RGB565A8`，其 RGB565 平面后接独立 A8 平面。

同时支持并使用 LVGL 官方样例验证：

- RGB888；
- ARGB8888；
- XRGB8888；
- L8；
- A8；
- AL88；
- I1、I2、I4、I8，包含调色板和 LVGL 行对齐规则。

每个解码器实现同一接口，将一个 `ImageAsset` 转换为紧凑 RGBA8888 缓冲。解码时严格使用 stride，不能把行填充当成像素。RGB565 按小端 16 位值读取并把 5/6 位通道扩展到 8 位；RGB565A8 的 alpha 起点根据完整颜色平面长度计算。

未知、RAW、压缩和 YUV 格式返回“unsupported”诊断，不尝试猜测。

## 7. 界面与交互

界面为单窗口三栏布局：左侧素材列表、中间 Canvas 预览、右侧元数据和诊断，顶部提供打开文件、清空和导出 PNG，底部提供缩放、背景和状态控制。

交互规则：

- 文件可拖入窗口任意位置，也可通过文件选择器多选；
- 同一批次文件独立处理，一个失败不阻塞其他文件；
- 一个 C 文件内的多个描述符显示为独立条目；
- 同名文件再次拖入时替换对应旧条目；
- 默认适应窗口，缩放使用最近邻插值以保持像素边缘；
- 滚轮缩放，拖动画布平移，双击恢复适应窗口；
- 可切换棋盘、白、黑和自定义背景色；
- 可显示像素网格，悬停显示坐标、RGBA 和源格式值；
- `Ctrl+O` 打开文件，`Ctrl+0` 适应窗口，`Ctrl+1` 显示原始尺寸；
- PNG 导出当前图片的原始像素尺寸，不包含预览背景、缩放或像素网格；
- `sessionStorage` 只保存界面偏好，不保存用户素材。

空状态显示拖放提示和支持范围。解析中、成功、部分成功、失败和不支持解码均有不同状态，不使用模糊的统一“打开失败”提示。

## 8. 错误模型与安全限制

诊断分为：

- `error`：无法安全形成或解码图片；
- `warning`：存在可解释的不一致，但仍可安全预览；
- `info`：格式已识别但功能暂不支持。

典型错误必须报告预期值和实际值，例如“RGB565 75×42 至少需要 6300 字节，实际为 6288 字节”。

安全限制：

- 拒绝超出设定尺寸、像素数、文本长度或数组长度上限的输入；
- 整数表达式使用安全整数运算并检测除零、负尺寸和溢出；
- 不执行拖入文件中的任何代码；
- 文本仅用 `textContent` 显示，不作为 HTML 注入；
- Worker 异常转为文件级诊断，不能导致整个页面失效；
- Object URL 在替换或清空素材时释放。

## 9. 工程结构

```text
tools/lvgl-image-viewer/
├─ src/
│  ├─ core/
│  │  ├─ image-asset.ts
│  │  ├─ color-formats.ts
│  │  ├─ stride.ts
│  │  └─ diagnostics.ts
│  ├─ parsers/
│  │  ├─ c-tokenizer.ts
│  │  ├─ c-expression.ts
│  │  ├─ c-asset-parser.ts
│  │  └─ bin-asset-parser.ts
│  ├─ decoders/
│  │  ├─ decoder-registry.ts
│  │  ├─ rgb.ts
│  │  ├─ alpha-luma.ts
│  │  └─ indexed.ts
│  ├─ worker/asset-worker.ts
│  ├─ ui/
│  │  ├─ asset-list.ts
│  │  ├─ preview-canvas.ts
│  │  ├─ metadata-panel.ts
│  │  └─ diagnostics-panel.ts
│  ├─ app.ts
│  └─ styles.css
├─ tests/
│  ├─ fixtures/
│  ├─ c-parser.test.ts
│  ├─ bin-parser.test.ts
│  ├─ pixel-decoders.test.ts
│  └─ repository-assets.test.ts
├─ index.html
├─ package.json
├─ vite.config.ts
└─ README.md
```

不引入 React 或 Vue。依赖限定为开发构建、测试和单 HTML 打包所需包；生产产物不从 CDN 加载资源。

## 10. 测试与验收

单元测试覆盖：

- 12 字节 BIN 头、小端字段、非法 magic 和截断数据；
- stride 自动计算和显式 stride；
- 所有支持颜色格式的代表像素与边界行；
- RGB565 字节序和通道展开；
- RGB565A8 alpha 平面偏移；
- indexed 调色板、位序和行对齐；
- C 注释、宏噪声、换行、尾逗号和允许的整数表达式；
- 函数调用、恶意表达式、除零、溢出和超大输入拒绝；
- 多描述符、缺失数组、数据不足和额外尾部数据。

仓库回归测试遍历 `app/assets/images/*.c`，要求全部现有业务素材成功解析，`mouse_cursor.c` 为 RGB565A8，其余当前业务素材为 RGB565，尺寸和数据长度一致。

黄金图测试使用固定小图比较 RGBA 缓冲哈希，防止字节序、红蓝通道、alpha 平面和调色板偏移回归。

浏览器验收覆盖拖放、多文件、缩放、背景、像素检查、诊断和 PNG 导出。最终生产文件必须能从 `file://` 在 Edge 和 Chrome 中打开，不发出网络请求。

完成标准：

- 双击单 HTML 即可运行；
- `.c`、`.bin` 拖入后自动解析和显示；
- TAB5 当前全部业务图片通过回归；
- RGB565/RGB565A8 与 LVGL 9.2.2 解释一致；
- 1280×720 C 素材解析期间界面保持响应；
- 非法或截断文件不会令页面崩溃；
- 类型检查、自动化测试和生产构建全部通过；
- README 记录使用方法、支持范围、构建方式和已知限制。

## 11. 实施顺序

1. 建立 TypeScript/Vite/Vitest 骨架、统一模型和基础限制。
2. 以测试驱动实现 BIN 解析、stride、RGB565 和 RGB565A8。
3. 以测试驱动实现安全 C 子集解析，并跑通仓库真实素材。
4. 实现其余未压缩颜色格式及黄金图测试。
5. 实现 Web Worker、素材状态和三栏界面。
6. 实现画布交互、像素检查和 PNG 导出。
7. 完成单 HTML 构建、浏览器验收和 README。
