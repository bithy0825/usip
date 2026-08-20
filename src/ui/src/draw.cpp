// ==============================================================================
// draw.cpp — 层绘制模板实现:L1 原始层 / L2 运算层 / L3 mask 层
//            (L4 ROI / L5 标注 / L6 临时 预留)
// ==============================================================================

#include "draw.hpp"

#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace usip::ui {
namespace {

    // TIFF orientation → 显示变换(基线 top_left)
    [[nodiscard]] auto orient_transform(common::orientation orient) -> QTransform
    {
        using enum common::orientation;
        switch (orient) {
        [[likely]] case top_left:
            return { };
        case top_right:
            return QTransform::fromScale(-1.0, 1.0); // 水平镜像
        case bottom_right:
            return QTransform::fromScale(-1.0, -1.0); // 旋转 180°
        case bottom_left:
            return QTransform::fromScale(1.0, -1.0); // 垂直镜像
        case left_top:
            return { 0.0, 1.0, 1.0, 0.0, 0.0, 0.0 }; // 转置
        case right_top: {
            QTransform t;
            t.rotate(90.0);
            return t;
        }
        case right_bottom:
            return { 0.0, -1.0, -1.0, 0.0, 0.0, 0.0 }; // 反转置
        case left_bottom: {
            QTransform t;
            t.rotate(270.0);
            return t;
        }
        }
        std::unreachable();
    }

    // 像素进入显示域的第一步:orient + MINISWHITE 反相(zero_is_white 属显示
    // 侧职责,不碰存储像素)
    [[nodiscard]] auto oriented_display(QImage img, common::orientation orient,
        bool zero_is_white) -> QImage
    {
        if (img.isNull())
            return { };
        img = img.transformed(orient_transform(orient));
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
        return overlay.transformed(orient_transform(orient));
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
    } else {
        // l4/l5/l6 预留:ROI / 标注 / 临时预览,实现时在此补分支
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

}
