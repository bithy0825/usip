# renderer 管线设计 —— 五条独立渲染管线 + 视图变换

日期:2026-08-17
状态:待评审

## 1. 目标与范围

**本轮实现**:renderer 的五条独立渲染管线 + `render_canvas` 中央控件(渲染、滚轮缩放、右键平移)。

**明确不做(后续轮次)**:ROI 点选、hover 像素取值、ROI/标注/阈值的创建手势、数据写路径(命令事件经 document_service)、页切换与 compare_to 设置的 UI 接线、config 变更事件、关闭文档事件。

## 2. 术语与层级模型

- **S** = 选中页(当前文档 `active_page` 对应页);**C** = 对比页(`S.compare_to` 指向,可跨文档)。
- 四个数据层:**L1 原始层**(可伪彩色)、**L2 mask 层**、**L3 ROI 层**、**L4 标注层**。
- **进行中预览**:创建手势产生的临时 ROI/标注,归 L3/L4 渲染 —— 层渲染函数接收"已有数据 + 可选预览"两部分输入,预览来自画布手势状态而非 document。slider/highlight/difference 三模式中 L3/L4 **只画预览、不画已有数据**。本轮无创建手势,预览输入恒空,但层接口预留该参数。

## 3. 组件与文件

| 文件 | 层 | 职责 |
|---|---|---|
| `core/inc/view_mode.hpp` | core | `enum class view_mode : std::uint8_t { single, split, slider, highlight, difference }`(事件值类型须在 core 可见) |
| `ui/inc/render_canvas.hpp/.cpp` | ui | 中央控件(`ui_protocol<render_canvas, QWidget>`):持有五条管线实例、`view_state`、文档别名表;订阅 bus 事件;wheel 缩放 / 右键平移;`paintEvent` 委托激活管线 |
| `ui/inc/render_pipeline.hpp/.cpp` | ui | 管线基类 + 五条实现;各管线自持层缓存、各自失效 |
| `ui/inc/render_layers.hpp/.cpp` | ui | 四层渲染 + 像素格式转换,**纯函数**,被五条管线复用 |
| `ui/inc/render_options.hpp` | ui | 渲染参数快照(各 config 值的打包结构) |

依赖方向:`canvas → pipeline → layers →(core::page / QImage)`。pipeline 与 layers 不依赖 QWidget,可脱离控件单测。

管线基类接口:

```cpp
class render_pipeline {
public:
    virtual ~render_pipeline() = default;
    // 页/对比页变化时调用,管线据此失效自身缓存
    virtual void set_pages(const core::page* subject, const core::page* compare) = 0;
    virtual void render(QPainter& painter, const QRect& viewport,
        const view_state& view, const render_options& options) = 0;
};
```

canvas 以 `std::array<std::unique_ptr<render_pipeline>, 5>` 持有全部实例,按 `view_mode` 激活 —— 切模式不丢缓存。

## 4. 层渲染契约(render_layers,纯函数)

- `make_base(page, options) -> QImage`:orient 变换(`QImage::transformed`)→ `zero_is_white` 反相 → 16 位按 `hist.range_min/max` 归一化到 8 位(无 hist 用格式全量程)→ 伪彩色开启且 `klass == gray` 时套 256 色 LUT(内置 turbo)。不伪彩色时输出 Grayscale8 / 原 rgb 图;伪彩色时输出 RGBA8888。
- `make_mask_overlay(mask, color, opacity) -> QImage`(RGBA8888):`mask.image` 非零像素着色(config 色 × 不透明度),零像素全透明。分割产物已把 [lo,high] 内置 255、范围外置 0,故"非零即着色"等价于"range 外全透明";`range` 字段仅记录分割参数,渲染不读。
- `make_diff(S, C) -> QImage`(8 位灰度):逐像素 `d = s8 − c8`,S/C 各自先按 base 管线归一化到 8 位显示域(统一 8 位域以支持跨格式对比);存储值 = `clamp(d, -128, 127) + 128`(128 = 零差异)。尺寸或像素类别不一致 → 返回空 QImage,由管线回落。逐像素计算用 hwy SIMD(先例见 `common/tiff.cpp`)。
- `draw_rois(painter, rois, transform, style, selected, pending)`:矢量直绘不缓存。`PathD → QPainterPath`;边框用 cosmetic pen(线宽不随缩放),颜色 `roi.color` / 不透明度 `roi.opacity`;label = 1-based vector 下标,画在包围盒左上角(删除中间元素后下标即时重排,天然满足"编号会变");`selected` 项内部填充同色 × `roi.fill_opacity`。
- `draw_annotations(painter, annotations, transform, style, pending)`:线段 + label 文本;线宽 `annotation.line_width`(device 像素,cosmetic),颜色/不透明度从 config。

## 5. 五条管线的合成方式

公共:每条管线自持层缓存(base/mask/diff 的 QImage),`set_pages` 或 options 变化时失效对应缓存;`render()` 先按 viewport 与 zoom/offset 求"图像→屏幕"变换,只画可见区。

- **single_pipeline**:依次 `drawImage(base)` → `drawImage(mask)` → `draw_rois` → `draw_annotations`。
- **split_pipeline**:viewport 垂直均分左右两半;**两半共享同一 zoom/offset**(已确认);左半画 S 的完整四层栈,右半画 C 的完整四层栈;S/C 各自持有层缓存。
- **slider_pipeline**:单视口;比例 `t ∈ [0,1]` 由图像底部的 QSlider 输入(canvas 持有的子控件,仅本模式可见;现有 QRangeSlider 是双滑块范围控件,不适用,另用 QSlider 定制)。左 `setClipRect` 画 S 的 L1+L2,右 clip 画 C 的 L1+L2(两层各自独立,故"第二层会有差别");L3/L4 仅画 pending。
- **highlight_pipeline**:帧 = base(S),diff 存储值 ≠ 128 的区域以 `highlight.color` / `highlight.opacity` 纯色覆盖;无 L2;L3/L4 仅 pending。
- **difference_pipeline**:帧 = base(S),其中 diff 存储值 ≠ 128 的像素替换为 `伪彩色 LUT[存储值]`(与 L1 伪彩色同款,即 config 的 `pseudocolor.colormap` 所选映射);无 L2;L3/L4 仅 pending。实现上 diff 与 base 一次性合成为帧缓存,仅页变化时重算。

## 6. 视图变换

```cpp
struct view_state {
    view_mode mode { view_mode::single };
    double zoom { 1.0 };   // clamp [0.1, 10]
    QPointF offset { };    // 屏幕平移
};
```

- 滚轮:以光标为锚点缩放(锚点处图像坐标不动);`zoom` clamp 到 [0.1, 10]。
- 右键拖动:改 `offset` 平移。
- split 两半共享同一 `zoom`/`offset`。
- 图像→屏幕:`p_screen = offset + zoom × p_pixel`,渲染前 `QPainter::setTransform`。

## 7. config 键

已有:`mask.color`(字符串 "#rrggbb")、`mask.opacity`(float 0–1)。

新增(在默认配置中注册):

| 键 | 默认 | 说明 |
|---|---|---|
| `roi.color` | `"#ff0000"` | ROI 边框/label 色 |
| `roi.opacity` | `1.0` | 边框不透明度 |
| `roi.fill_opacity` | `0.15` | 选中 ROI 的内部填充不透明度(同色) |
| `annotation.color` | `"#00ff00"` | 标注线/文本色 |
| `annotation.opacity` | `1.0` | 标注不透明度 |
| `annotation.line_width` | `2` | 标注线宽(device 像素) |
| `highlight.color` | `"#ffff00"` | 差异高亮色 |
| `highlight.opacity` | `0.5` | 高亮不透明度 |
| `pseudocolor.enabled` | `false` | 伪彩色开关(仅 gray 页生效) |
| `pseudocolor.colormap` | `"turbo"` | 内置映射表名 |

读取时机:每次 paint 前从 `config::global()` 组装 `render_options` 快照(每帧十余次 map 查找,开销可忽略;后续可加 config 变更事件优化)。

## 8. 事件(canvas 订阅侧)

- 已存在:`document_ready` / `document_switch`(`shared_ptr<document>` 非拥有别名)。
- 新增:`view_mode_set { view_mode mode }`(触发方 UI 下轮接线,canvas 先具备响应能力)。

**跨文档取 C**:compare_to 可跨文档,而事件只给当前文档别名 → canvas 维护一份"文档别名表"(订阅 `document_ready` 累积 `map<uuid, shared_ptr<document>>`,`document_switch` 指定当前文档),按 `compare_to` 在全量页中查 C。service 当前多文档只增不替,别名表安全;关闭文档的清理属后续轮次。

## 9. 错误与边界

- 无文档:空白背景。
- C 缺失(未设 `compare_to`)却处于 split/slider/highlight/difference:静默回落 single 渲染(模式入口的禁用/提示属触发方,后续轮次)。
- S、C 尺寸或像素类别不一致:`make_diff` 返回空 → highlight/difference 回落 single。
- `mask.image` 为空:跳过 L2。
- rgb/rgba 页:伪彩色不生效。
- 16 位页无 hist:按全量程归一化。

## 10. 测试

- `render_layers`:构造小图(4×2 的 gray8/gray16/rgb),断言 `make_base` / `make_mask_overlay` / `make_diff` 输出像素;LUT 边界(0/128/255);orient 与反相。
- 管线合成:离屏 QImage 作 render 目标,断言关键像素(split 左右半、slider 分割线两侧、highlight 覆盖区、difference 替换区)。
- 变换数学:锚点缩放公式、zoom clamp。
- canvas 本体:手动验证。

## 11. 后续轮次(明确排除,仅记录)

ROI 点选(无工具时左键)、hover 像素取值(→ 状态栏)、ROI/标注创建手势(矩形拖拽/两点)、命令事件与 service 写路径(`roi_added` / `annotation_added` / `page_activated` / `compare_set` 等)、split 模式 ROI 添加镜像(仅添加双向同步,已确认)、标注 label 的 dpi 换算(service 侧,dpi 缺失用 px)、config 变更事件、关闭文档与别名表清理。
