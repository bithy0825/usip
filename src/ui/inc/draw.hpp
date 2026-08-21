#pragma once

// ==============================================================================
// draw.hpp — 层绘制方法(无状态/无总线/无控件):canvas 管一切,这里只管画
//
// 六层模型(层结构固定;本轮实现 L1/L2/L3,L4/L5/L6 预留空分支):
//   L1 原始层  orient/反相/16 位归一化/伪彩 —— single / split
//   L2 运算层  主副逻辑运算(highlight 与 difference 同构,仅异区着色不同):
//              同区 = 主图 8 位灰度显示(本层不开伪彩、不画 L1);
//              异区 = 固定色按不透明度混合(highlight)或 511 项 diff LUT 伪彩
//              (difference,伪彩为该模式固有,不可关)。
//              差值 =(主+255)-副 ∈ [0,510] 存 Grayscale16,255 = 零差异。
//              slider 的"贴合"不在此层:缝是视图态,由 canvas 对两份 L1
//              内容 clip 合成(拖动零重建)
//   L3 mask层  阈值蒙版着色 —— 仅 single(非 single 模式永不显示)
//   L4 ROI 层 / L5 标注层(均受可见开关控制;非 single 仅画两页公有项)/
//   L6 临时层(创建手势预览)
//
// 一个模板方法特化六层:draw<layer::l1>(painter, subject, compare, options,
// cache) —— compare 仅 L2 使用(可空)。canvas(状态与事件的唯一管理者)持有
// options 唯一实例与每层一个 QImage 结果缓存;事件变更某层内容时 canvas 清
// 该层缓存并 update(),下次 draw 自动重建 —— 缓存只存绘制结果,不存状态。
// 绘制前由 canvas 预设图像→屏幕变换与 clip(怎么画),draw 只在图像坐标
// 原点落图(画什么)。
//
// 本文件不依赖 QWidget / 总线,可脱离控件离屏单测。
// ==============================================================================

#include <QColor>
#include <QImage>
#include <QRectF>

#include <optional>
#include <span>
#include <vector>

#include "colormap.hpp"
#include "document.hpp"
#include "roi_tool.hpp" // roi_shape(临时层草稿形状)
#include "view_mode.hpp"

class QPainter;

namespace usip::ui {

// ─── 渲染选项:定义在此,canvas 拥有唯一实例(config 播种部分键 + 总线事件修改)──
struct options {
    core::view_mode mode { core::view_mode::single };
    // L1:伪彩(仅 single/split;highlight/difference 不画 L1,此项无影响)
    bool pseudocolor_enabled { false };
    core::colormap_type pseudocolor_colormap { core::colormap_type::jet };
    bool zero_is_black { true };
    // L3:mask
    bool mask_enabled { false };
    QColor mask_color { "#FF0000" };
    double mask_opacity { 0.5 };
    // L4:ROI
    bool roi_visible { true };
    // L5:标注
    bool annotation_visible { true };
    int line_width { 2 };
    QColor line_color { "#00FF00" };
    // L6:临时(暂不实现)
    // L2 highlight 着色
    QColor highlight_color { "#FFFF00" };
    float highlight_opacity { 0.9F };
};

// ─── 层标签 ───────────────────────────────────────────────────────────────────
enum class layer : std::uint8_t { l1, l2, l3, l4, l5, l6 };

// ─── 层绘制:cache 空才重建该层内容;painter 变换/clip 由调用方(canvas)预设 ────
template <layer L>
void draw(QPainter&, const core::page& subject, const core::page* compare,
    const options&, QImage& cache);

// 页显示尺寸(orient 后):canvas 平移/缩放 clamp 与适配依据;纯函数不建缓存
[[nodiscard]] auto oriented_size(const core::page&) -> QSize;

// 掩膜叠加图(8 位掩膜非零像素着色 = 色 × 不透明度,零像素全透明;orient 对齐):
// L3 与画布的工具临时层(L6)共用 —— L6 传工具的临时掩膜与所属页 info
[[nodiscard]] auto mask_overlay(const QImage& mask, const common::page_info& info,
    const options& opts) -> QImage;

// 标注矢量渲染(L5 持久层与画布临时层共用):经 painter 现有变换映射到屏幕
// 坐标后直绘,不用层缓存(量少,缩放平移零重建)。样式:等距虚线(4:3,
// 线宽/线色走 opts)+ 端点圆点 + 半透明黑底药丸标签,尺寸屏幕恒定;
// ghost=true 整体降透明度(拖拽中的草稿)
void draw_annotations(QPainter& painter, std::span<const core::annotation> annotations,
    const options& opts, bool ghost = false);

// ROI 持久层渲染(L4,画布侧直绘,不用层缓存):蚂蚁线 = 2px 虚线边框,
// 颜色按 roi 在 vector 中的编号经 core::roi_color 取;空段不绘制(无底描
// 边),ants_offset 为动画相位(画布蚂蚁线定时器推进,模 ants_offset_cycle)。
// opts.roi_visible = false 时整层不画(五模式皆受控)。compare_rois 非 null
// (非 single 模式)时仅画两页完全相同的选区(逐点精确比较;apply 双写
// 同值拷贝保证公有项成立),独有项不显示。经 painter 现有变换映射到屏幕
// 坐标后直绘,尺寸屏幕恒定
void draw_rois(QPainter& painter, std::span<const core::roi> rois,
    const std::vector<core::roi>* compare_rois, const options& opts, int ants_offset);

// 蚂蚁线相位周期(= 虚线模式 {4,4} 的一整个实+空段;画布定时器按此取模)
inline constexpr int ants_offset_cycle { 8 };

// ROI 临时层渲染(L6,与持久层同坐标约定):会话累积路径 + 拖拽草稿
// (矩形画包围盒,椭圆画内切椭圆);实线 2px 边框 + 同色半透明填充
// (颜色 = 落盘后起始编号色,由调用方传入)
void draw_session_rois(QPainter& painter, const Clipper2Lib::PathsD& paths,
    roi_shape shape, const std::optional<QRectF>& draft, const QColor& color);

// ROI 临时层:多边形进行中的预览(已落顶点 + 悬停点;悬停点参与闭合,
// 预览"双击封闭后"的样子)—— 顶点折线 + 顶点圆点,同色半透明填充
void draw_session_poly(QPainter& painter, std::span<const QPointF> points,
    const QPointF* hover, const QColor& color);

}
