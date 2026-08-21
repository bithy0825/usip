// ==============================================================================
// draw.cpp — 层绘制模板实现:L1 原始层 / L2 运算层 / L3 mask 层
//            (L4 ROI / L5 标注 / L6 临时 预留)
// ==============================================================================

#include "draw.hpp"
#include "orient.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace usip::ui {
namespace {

    // 像素进入显示域的第一步:orient + MINISWHITE 反相(zero_is_white 属显示
    // 侧职责,不碰存储像素)
    [[nodiscard]] auto oriented_display(QImage img, common::orientation orient,
        bool zero_is_white) -> QImage
    {
        if (img.isNull())
            return { };
        img = img.transformed(core::orient_transform(orient));
        if (zero_is_white)
            img.invertPixels(QImage::InvertRgb);
        return img;
    }

    // RGBA8888 内存字(小端):r | g<<8 | b<<16 | a<<24(与 core::colormap 同约定)
    [[nodiscard]] constexpr auto rgba_word(int r, int g, int b, int a) noexcept -> std::uint32_t
    {
        return static_cast<std::uint32_t>(a) << 24 | static_cast<std::uint32_t>(b) << 16
            | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(r);
    }

    // 8 位显示域归一化:orient + 反相;16 位按 hist 量程(缺省全量程)压到 8 位;
    // 彩色页无灰度显示域 → 空(进对比模式前 canvas 已校验,此处双保险)
    [[nodiscard]] auto display_gray8(const core::page& page) -> QImage
    {
        auto img = oriented_display(page.image, page.info.orient, page.info.zero_is_white);
        if (img.format() == QImage::Format_Grayscale8)
            return img;
        if (img.format() != QImage::Format_Grayscale16)
            return { };

        double lo = 0.0, hi = 65535.0;
        if (const auto& hist = page.info.hist; hist && !hist->range_min.empty()) {
            lo = hist->range_min.front();
            hi = hist->range_max.front();
        }
        if (!(hi > lo))
            hi = lo + 1.0; // 退化量程(常量图):全部落索引 0
        const auto scale = 255.0 / (hi - lo);

        QImage out { img.size(), QImage::Format_Grayscale8 };
        for (int y = 0; y < img.height(); ++y) {
            const auto* src = reinterpret_cast<const std::uint16_t*>(img.constScanLine(y));
            auto* dst = out.scanLine(y);
            for (int x = 0; x < img.width(); ++x) {
                const auto v = (static_cast<double>(src[x]) - lo) * scale;
                dst[x] = static_cast<uchar>(std::clamp(std::lround(v), 0L, 255L));
            }
        }
        return out;
    }

    // L1 内容:orient → 反相 → 伪彩(仅灰度原生格式;归一化+查表由 core::colorize
    // 单遍完成;16 位按 hist 量程归一化,带 clamp:原始 0 恒落索引 0)
    [[nodiscard]] auto make_base(const core::page& page, const options& opts) -> QImage
    {
        auto img = oriented_display(page.image, page.info.orient, page.info.zero_is_white);
        if (img.isNull() || !opts.pseudocolor_enabled || !core::pseudocolorable(img.format()))
            return img;

        std::optional<std::pair<double, double>> range;
        if (const auto& hist = page.info.hist; hist && !hist->range_min.empty())
            range = std::pair { hist->range_min.front(), hist->range_max.front() };
        const auto lut = core::make_color_lut(opts.pseudocolor_colormap, opts.zero_is_black);
        if (QImage colored = core::colorize(img, lut, range); !colored.isNull())
            return colored;
        return img;
    }

    // 掩膜非零像素着色(色 × 不透明度),零像素全透明;与 L1 同 orient 对齐
    [[nodiscard]] auto mask_overlay_impl(const QImage& mask, common::orientation orient,
        const options& opts) -> QImage
    {
        if (mask.isNull())
            return { };

        QImage overlay { mask.size(), QImage::Format_RGBA8888 };
        overlay.fill(Qt::transparent);

        const auto word = rgba_word(opts.mask_color.red(), opts.mask_color.green(),
            opts.mask_color.blue(), static_cast<int>(opts.mask_opacity * 255.0));
        for (int y = 0; y < mask.height(); ++y) {
            const auto* src = mask.constScanLine(y);
            auto* dst = reinterpret_cast<std::uint32_t*>(overlay.scanLine(y));
            for (int x = 0; x < mask.width(); ++x)
                if (src[x] != 0)
                    dst[x] = word;
        }
        return overlay.transformed(core::orient_transform(orient));
    }

    // 差值:(主+255)-副 ∈ [0,510] 存 Grayscale16(255 = 零差异);尺寸不一致 → 空
    [[nodiscard]] auto make_diff(const QImage& s8, const QImage& c8) -> QImage
    {
        if (s8.isNull() || c8.isNull() || s8.size() != c8.size())
            return { };

        QImage d { s8.size(), QImage::Format_Grayscale16 };
        for (int y = 0; y < s8.height(); ++y) {
            const auto* ss = s8.constScanLine(y);
            const auto* cs = c8.constScanLine(y);
            auto* ds = reinterpret_cast<std::uint16_t*>(d.scanLine(y));
            for (int x = 0; x < s8.width(); ++x)
                ds[x] = static_cast<std::uint16_t>(int { ss[x] } - int { cs[x] } + 255);
        }
        return d;
    }

    // L2 内容:同区 = 主图 8 位灰底(不开伪彩);异区按模式着色
    [[nodiscard]] auto make_op_image(const core::page& subject, const core::page& compare,
        const options& opts) -> QImage
    {
        const QImage s8 = display_gray8(subject);
        const QImage diff = make_diff(s8, display_gray8(compare));
        if (s8.isNull() || diff.isNull())
            return { };

        // 灰底:gray → (g,g,g,255)
        QImage out { s8.size(), QImage::Format_RGBA8888 };
        for (int y = 0; y < s8.height(); ++y) {
            const auto* ss = s8.constScanLine(y);
            auto* dst = reinterpret_cast<std::uint32_t*>(out.scanLine(y));
            for (int x = 0; x < s8.width(); ++x) {
                const auto g = ss[x];
                dst[x] = rgba_word(g, g, g, 255);
            }
        }

        if (opts.mode == core::view_mode::highlight) { // 异区:固定色按不透明度混合
            const double a = opts.highlight_opacity;
            const int hr = opts.highlight_color.red();
            const int hg = opts.highlight_color.green();
            const int hb = opts.highlight_color.blue();
            for (int y = 0; y < s8.height(); ++y) {
                const auto* dd
                    = reinterpret_cast<const std::uint16_t*>(diff.constScanLine(y));
                const auto* ss = s8.constScanLine(y);
                auto* dst = reinterpret_cast<std::uint32_t*>(out.scanLine(y));
                for (int x = 0; x < s8.width(); ++x) {
                    if (dd[x] == 255) // 索引 255 = 零差异
                        continue;
                    const auto g = static_cast<double>(ss[x]);
                    dst[x] = rgba_word(static_cast<int>(std::lround(g + (hr - g) * a)),
                        static_cast<int>(std::lround(g + (hg - g) * a)),
                        static_cast<int>(std::lround(g + (hb - g) * a)), 255);
                }
            }
            return out;
        }

        // difference:异区替换为差值伪彩(零差异像素不查表,LUT[255] 不落屏)
        if (const QImage colored = core::colorize_diff(diff,
                core::make_diff_lut(opts.pseudocolor_colormap, opts.zero_is_black));
            !colored.isNull()) {
            for (int y = 0; y < s8.height(); ++y) {
                const auto* dd
                    = reinterpret_cast<const std::uint16_t*>(diff.constScanLine(y));
                const auto* cs
                    = reinterpret_cast<const std::uint32_t*>(colored.constScanLine(y));
                auto* dst = reinterpret_cast<std::uint32_t*>(out.scanLine(y));
                for (int x = 0; x < s8.width(); ++x)
                    if (dd[x] != 255)
                        dst[x] = cs[x];
            }
        }
        return out;
    }

    // 完全相同的标注:两端点无序相等 + label 相等(apply 双写同值,精确比较即可)
    [[nodiscard]] auto same_annotation(const core::annotation& a, const core::annotation& b)
        -> bool
    {
        if (a.label != b.label)
            return false;
        return (a.line.first == b.line.first && a.line.second == b.line.second)
            || (a.line.first == b.line.second && a.line.second == b.line.first);
    }

    // 完全相同的选区:路径逐点多边形精确相等(apply 双写同值拷贝,精确比较即可)
    [[nodiscard]] auto same_roi(const core::roi& a, const core::roi& b) -> bool
    {
        if (a.path.size() != b.path.size())
            return false;
        for (std::size_t i = 0; i < a.path.size(); ++i) {
            const auto& pa = a.path[i];
            const auto& pb = b.path[i];
            if (pa.size() != pb.size())
                return false;
            for (std::size_t j = 0; j < pa.size(); ++j)
                if (!(pa[j] == pb[j]))
                    return false;
        }
        return true;
    }

    // 蚂蚁线参数:2px 笔宽,{4,4} 虚线(单位 = 线宽 → 屏幕 8px 实/8px 空);
    // offset 周期 ants_offset_cycle 见 draw.hpp(画布定时器共用)
    constexpr qreal roi_pen_width { 2.0 };

    // 高亮蒙版不透明度(infodock 行选中;约 62% 透明,加强显示且不遮图像数据)
    constexpr int highlight_fill_alpha { 96 };

    // 图像坐标 PathsD → 屏幕 QPainterPath(t = canvas 预设的图像→屏幕变换);
    // 奇偶填充(Clipper2 输出孔洞与外圈方向相反,两种规则皆正确)
    [[nodiscard]] auto mapped_roi_path(const Clipper2Lib::PathsD& paths, const QTransform& t)
        -> QPainterPath
    {
        QPainterPath pp;
        for (const auto& path : paths) {
            if (path.empty())
                continue;
            QPolygonF poly;
            poly.reserve(static_cast<qsizetype>(path.size()));
            for (const auto& pt : path)
                poly.append(t.map(QPointF { pt.x, pt.y }));
            pp.addPolygon(poly);
            pp.closeSubpath();
        }
        return pp;
    }

} // namespace

// ─── 层绘制:cache 为空才重建该层内容 ─────────────────────────────────────────

template <layer L>
void draw(QPainter& painter, const core::page& subject,
    [[maybe_unused]] const core::page* compare, const options& opts, QImage& cache)
{
    if constexpr (L == layer::l1) {
        if (cache.isNull())
            cache = make_base(subject, opts);
        if (!cache.isNull())
            painter.drawImage(0, 0, cache);
    } else if constexpr (L == layer::l2) {
        // 仅 highlight/difference;slider 贴合由 canvas 对两份 L1 内容 clip 合成
        if (opts.mode != core::view_mode::highlight && opts.mode != core::view_mode::difference)
            return;
        if (compare == nullptr)
            return;
        if (cache.isNull())
            cache = make_op_image(subject, *compare, opts);
        if (!cache.isNull())
            painter.drawImage(0, 0, cache);
    } else if constexpr (L == layer::l3) {
        // mask 仅 single 显示(非 single 模式永不)
        if (opts.mode != core::view_mode::single || !opts.mask_enabled || !subject.mask)
            return;
        if (cache.isNull())
            cache = mask_overlay_impl(subject.mask->image, subject.info.orient, opts);
        if (!cache.isNull())
            painter.drawImage(0, 0, cache);
    } else if constexpr (L == layer::l5) {
        // 标注:可见开关整层控制;非 single 仅画主副完全相同的部分(命中即止,O(MN))
        if (!opts.annotation_visible)
            return;
        if (compare != nullptr && opts.mode != core::view_mode::single) {
            std::vector<core::annotation> same;
            same.reserve(subject.annotations.size());
            for (const auto& s : subject.annotations)
                for (const auto& c : compare->annotations)
                    if (same_annotation(s, c)) {
                        same.push_back(s);
                        break;
                    }
            draw_annotations(painter, std::span<const core::annotation> { same }, opts);
        } else {
            draw_annotations(
                painter, std::span<const core::annotation> { subject.annotations }, opts);
        }
    } else {
        // l4/l6 预留:ROI / 临时层(阈值掩膜与标注预览由 canvas 侧直绘,不走层缓存)
        (void)painter;
        (void)subject;
        (void)opts;
        (void)cache;
    }
}

// 显式实例化六层(模板实现留在 cpp,头文件只暴露声明)
template void draw<layer::l1>(QPainter&, const core::page&, const core::page*, const options&,
    QImage&);
template void draw<layer::l2>(QPainter&, const core::page&, const core::page*, const options&,
    QImage&);
template void draw<layer::l3>(QPainter&, const core::page&, const core::page*, const options&,
    QImage&);
template void draw<layer::l4>(QPainter&, const core::page&, const core::page*, const options&,
    QImage&);
template void draw<layer::l5>(QPainter&, const core::page&, const core::page*, const options&,
    QImage&);
template void draw<layer::l6>(QPainter&, const core::page&, const core::page*, const options&,
    QImage&);

// ─── 页显示尺寸(纯函数)────────────────────────────────────────────────────────

auto oriented_size(const core::page& page) -> QSize
{
    using enum common::orientation;
    switch (page.info.orient) {
    case top_left:
    case top_right:
    case bottom_right:
    case bottom_left:
        return page.image.size();
    case left_top: // 转置/旋转型:宽高互换
    case right_top:
    case right_bottom:
    case left_bottom:
        return page.image.size().transposed();
    }
    std::unreachable();
}

// ─── 掩膜叠加(L3 与画布工具临时层共用)────────────────────────────────────────

auto mask_overlay(const QImage& mask, const common::page_info& info, const options& opts) -> QImage
{
    return mask_overlay_impl(mask, info.orient, opts);
}

// ─── 标注渲染(L5 与画布临时层共用;屏幕空间直绘)──────────────────────────────

void draw_annotations(QPainter& painter, std::span<const core::annotation> annotations,
    const options& opts, bool ghost)
{
    if (annotations.empty())
        return;

    const QTransform image_to_screen = painter.transform(); // canvas 预设的图像→屏幕
    painter.save();
    painter.resetTransform(); // 之后全部屏幕坐标:文本/虚线尺寸恒定
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor line_color = ghost ? [&] {
        QColor c = opts.line_color;
        c.setAlpha(150);
        return c;
    }() : opts.line_color;

    QPen pen { line_color, static_cast<qreal>(opts.line_width), Qt::PenStyle::CustomDashLine };
    pen.setDashPattern({ 4.0, 3.0 }); // 单位 = 线宽 → 等距虚线
    const QPen dot_pen { QColor { line_color.darker(260) }, 1.0 };

    QFont font = painter.font();
    font.setPointSize(9);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics fm { font };

    for (const auto& a : annotations) {
        const QPointF p1 = image_to_screen.map(a.line.first);
        const QPointF p2 = image_to_screen.map(a.line.second);
        if (p1 == p2) // 零长(刚起笔)
            continue;

        painter.setPen(pen);
        painter.drawLine(p1, p2);

        // 端点:线色实心圆点 + 深色细描边(任意底图上可读)
        painter.setPen(dot_pen);
        painter.setBrush(line_color);
        painter.drawEllipse(p1, 2.5, 2.5);
        painter.drawEllipse(p2, 2.5, 2.5);
        painter.setBrush(Qt::NoBrush);

        if (a.label.empty())
            continue;

        // 标签:线中点上方纯文字(无背景,不遮挡图像数据)
        const QString text = QString::fromStdString(a.label);
        const QPoint mid { static_cast<int>((p1.x() + p2.x()) / 2.0),
            static_cast<int>((p1.y() + p2.y()) / 2.0) };
        painter.setPen(line_color);
        painter.drawText(mid.x() - fm.horizontalAdvance(text) / 2, mid.y() - 10, text);
    }
    painter.restore();
}

// ─── ROI 渲染(L4 持久层与画布临时层共用;屏幕空间直绘)──────────────────────

void draw_rois(QPainter& painter, std::span<const core::roi> rois,
    const std::vector<core::roi>* compare_rois, const options& opts, int ants_offset,
    int highlight)
{
    if (rois.empty() || !opts.roi_visible)
        return;

    const QTransform image_to_screen = painter.transform(); // canvas 预设的图像→屏幕
    painter.save();
    painter.resetTransform(); // 之后全部屏幕坐标:线宽/虚线尺寸恒定
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (std::size_t i = 0; i < rois.size(); ++i) {
        // 非 single:仅画主副两页完全相同的选区,独有项不显示
        if (compare_rois != nullptr
            && std::none_of(compare_rois->begin(), compare_rois->end(),
                [&r = rois[i]](const core::roi& c) { return same_roi(c, r); }))
            continue;

        const QPainterPath pp = mapped_roi_path(rois[i].path, image_to_screen);
        if (pp.isEmpty())
            continue;

        const QColor color = core::roi_color(i);

        // 高亮(infodock 行选中):蚂蚁线之内加一层同色高透明蒙版(仅渲染态)
        if (static_cast<int>(i) == highlight) {
            QColor fill { color };
            fill.setAlpha(highlight_fill_alpha);
            painter.fillPath(pp, fill);
        }

        // 蚂蚁线:颜色按 vector 编号;空段 = 不绘制(无底描边)
        QPen pen { color, roi_pen_width, Qt::PenStyle::CustomDashLine };
        pen.setDashPattern({ 4.0, 4.0 }); // 单位 = 线宽 → 屏幕 8px 实/8px 空
        pen.setDashOffset(static_cast<qreal>(ants_offset));
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(pp);
    }
    painter.restore();
}

void draw_session_rois(QPainter& painter, const Clipper2Lib::PathsD& paths,
    roi_shape shape, const std::optional<QRectF>& draft, const QColor& color)
{
    if (paths.empty() && !draft)
        return;

    const QTransform image_to_screen = painter.transform();
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 边框与填充同色;填充半透明,不遮图像数据
    QColor fill { color };
    fill.setAlpha(64);
    const QPen pen { color, roi_pen_width };

    painter.setPen(pen);
    painter.setBrush(fill);
    if (!paths.empty())
        painter.drawPath(mapped_roi_path(paths, image_to_screen));
    if (draft) { // 草稿:矩形画包围盒本身,椭圆画内切椭圆(屏幕域,边缘平滑)
        const QRectF r = image_to_screen.mapRect(*draft);
        if (shape == roi_shape::ellipse)
            painter.drawEllipse(r);
        else
            painter.drawRect(r);
    }
    painter.restore();
}

void draw_session_poly(QPainter& painter, std::span<const QPointF> points,
    const QPointF* hover, const QColor& color)
{
    if (points.empty())
        return;

    const QTransform image_to_screen = painter.transform();
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF screen; // 屏幕域顶点(悬停点作末顶点:预览封闭后的样子)
    screen.reserve(static_cast<qsizetype>(points.size()) + (hover ? 1 : 0));
    for (const auto& p : points)
        screen.append(image_to_screen.map(p));
    if (hover)
        screen.append(image_to_screen.map(*hover));

    QColor fill { color };
    fill.setAlpha(64);
    painter.setPen(QPen { color, roi_pen_width });
    painter.setBrush(fill);
    painter.drawPolygon(screen); // 自动闭合:填充 + 含闭合边的描边

    // 顶点圆点(同标注端点样式:实心 + 深色细描边)
    painter.setPen(QPen { color.darker(260), 1.0 });
    painter.setBrush(color);
    for (const auto& p : screen)
        painter.drawEllipse(p, 2.5, 2.5);
    painter.setBrush(Qt::NoBrush);
    painter.restore();
}

}
