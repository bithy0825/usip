// ==============================================================================
// roi_stats.cpp — 选区统计实现(highway 加速的单遍统计核)
// ==============================================================================

#include "roi_stats.hpp"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>

#include <hwy/highway.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace usip::core {
namespace {

    namespace hn = hwy::HWY_NAMESPACE; // 静态分派(/arch:AVX2 基线),用法同 colormap

    // TIFF orientation → 显示变换(与 ui/draw.cpp 同款;选区坐标 = 显示域坐标,
    // 像素面须先对齐再统计;基线 top_left 由调用方短路,不进此函数)
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

    // 单行统计累加器(vmin/vmax 仅 valid 车道参与;全行无有效像素时保持初值)
    struct accum {
        std::uint64_t total { 0 };
        std::uint64_t valid { 0 };
        std::uint64_t sum { 0 };
        std::uint64_t sumsq { 0 };
        std::uint8_t vmin { 255 };
        std::uint8_t vmax { 0 };
    };

    // 单行统计核(SIMD 主循环 + 标量尾部):region != 0 计入 total;
    // 且灰度落入 [lo,hi] 计入 valid 并累积 和/平方和/极值
    void stats_row(const std::uint8_t* region, const std::uint8_t* values, std::size_t width,
        std::uint8_t lo, std::uint8_t hi, accum& acc)
    {
        const hn::ScalableTag<std::uint8_t> d8;
        const auto n = hn::Lanes(d8);
        const auto zero = hn::Zero(d8);
        const auto one = hn::Set(d8, std::uint8_t { 1 });
        const auto top = hn::Set(d8, std::uint8_t { 255 });
        const auto vlo = hn::Set(d8, lo);
        const auto vhi = hn::Set(d8, hi);

        using V64 = decltype(hn::SumsOf8(zero)); // u64 车道(每 8 项一归,永不溢出)
        const hn::DFromV<V64> d64;
        auto total_v = hn::Zero(d64);
        auto valid_v = hn::Zero(d64);
        auto sum_v = hn::Zero(d64);

        // 平方和:偶/奇车道 u8 → u16 → 位转 i16(0..255 精确;异号无 PromoteTo),
        // 成对加宽乘(pmaddwd:积两两成对落 i32,2×255² 不溢出),i32 按行累加
        // (行宽 ≤ 26 万像素远未及 i32 上限),行末提升 i64 归约并入总和
        const hn::Repartition<std::uint16_t, decltype(d8)> d16u;
        const hn::Repartition<std::int16_t, decltype(d8)> d16;
        const hn::Repartition<std::int32_t, decltype(d8)> d32;
        const hn::Repartition<std::int64_t, decltype(d8)> d64i;
        auto sq_v = hn::Zero(d32);

        auto vmin = top;
        auto vmax = zero;

        std::size_t x = 0;
        for (; x + n <= width; x += n) {
            const auto r = hn::LoadU(d8, region + x);
            const auto v = hn::LoadU(d8, values + x);
            const auto in = hn::Ne(r, zero);
            total_v = hn::Add(total_v, hn::SumsOf8(hn::IfThenElse(in, one, zero)));

            const auto hit = hn::And(in, hn::And(hn::Ge(v, vlo), hn::Le(v, vhi)));
            const auto sel = hn::IfThenElse(hit, v, zero); // 有效像素值,其余 0
            valid_v = hn::Add(valid_v, hn::SumsOf8(hn::IfThenElse(hit, one, zero)));
            sum_v = hn::Add(sum_v, hn::SumsOf8(sel));
            const auto ev = hn::BitCast(d16, hn::PromoteEvenTo(d16u, sel));
            const auto od = hn::BitCast(d16, hn::PromoteOddTo(d16u, sel));
            sq_v = hn::Add(sq_v, hn::WidenMulPairwiseAdd(d32, ev, ev));
            sq_v = hn::Add(sq_v, hn::WidenMulPairwiseAdd(d32, od, od));
            vmin = hn::Min(vmin, hn::IfThenElse(hit, v, top));
            vmax = hn::Max(vmax, sel);
        }

        acc.total += hn::ReduceSum(d64, total_v);
        acc.valid += hn::ReduceSum(d64, valid_v);
        acc.sum += hn::ReduceSum(d64, sum_v);
        acc.sumsq += static_cast<std::uint64_t>(hn::ReduceSum(d64i,
            hn::Add(hn::PromoteEvenTo(d64i, sq_v), hn::PromoteOddTo(d64i, sq_v))));
        acc.vmin = std::min(acc.vmin, hn::ReduceMin(d8, vmin));
        acc.vmax = std::max(acc.vmax, hn::ReduceMax(d8, vmax));

        for (; x < width; ++x) {
            if (region[x] == 0)
                continue;
            ++acc.total;
            const auto v = values[x];
            if (v < lo || v > hi)
                continue;
            ++acc.valid;
            acc.sum += v;
            acc.sumsq += static_cast<std::uint64_t>(v) * v;
            acc.vmin = std::min(acc.vmin, v);
            acc.vmax = std::max(acc.vmax, v);
        }
    }

    // 图 → 8 位阈值域工作面(行按 width 紧凑排布):Grayscale8 直拷(源 stride
    // 可能有行填充,逐行拷),Grayscale16 逐行 >>8;空图/非灰度 → false
    // (与 threshold_tool::to_display_gray8 同口径,输入为 orient 后的图)
    [[nodiscard]] auto to_threshold_plane(const QImage& img, std::vector<std::uint8_t>& plane,
        int& width, int& height) -> bool
    {
        switch (img.format()) {
        case QImage::Format_Grayscale8:
            width = img.width();
            height = img.height();
            plane.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            for (int y = 0; y < height; ++y)
                std::memcpy(plane.data() + static_cast<std::size_t>(y) * width,
                    img.constScanLine(y), static_cast<std::size_t>(width));
            return true;
        case QImage::Format_Grayscale16:
            width = img.width();
            height = img.height();
            plane.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            for (int y = 0; y < height; ++y) {
                const auto* src = reinterpret_cast<const std::uint16_t*>(img.constScanLine(y));
                auto* dst = plane.data() + static_cast<std::size_t>(y) * width;
                for (int x = 0; x < width; ++x)
                    dst[x] = static_cast<std::uint8_t>(src[x] >> 8);
            }
            return true;
        default:
            return false;
        }
    }

    // 选区矢量路径 → 二值栅格(0/255;奇偶填充与 L4 渲染同规则;关抗锯齿,
    // 边缘逐像素非黑即白,统计口径与可见选区一致)
    [[nodiscard]] auto rasterize(const Clipper2Lib::PathsD& paths, const QSize& size) -> QImage
    {
        QImage region { size, QImage::Format_Grayscale8 };
        region.fill(0);

        QPainterPath pp; // 默认 OddEvenFill(孔洞与外圈方向相反,与渲染同规则)
        for (const auto& path : paths) {
            if (path.empty())
                continue;
            QPolygonF poly;
            poly.reserve(static_cast<qsizetype>(path.size()));
            for (const auto& pt : path)
                poly.append(QPointF { pt.x, pt.y });
            pp.addPolygon(poly);
            pp.closeSubpath();
        }

        QPainter painter { &region };
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor { 255, 255, 255 });
        painter.drawPath(pp);
        return region;
    }

} // namespace

auto integral_range(std::pair<double, double> range) -> std::pair<int, int>
{
    // 对整数 v 有 v >= floor ⟺ v >= ceil(floor),v <= ceil ⟺ v <= floor(ceil);
    // 与 [0,255] 无交集 → {1,0}(lo > hi 即空判,产物 valid 恒 0)
    int lo = static_cast<int>(std::ceil(range.first));
    int hi = static_cast<int>(std::floor(range.second));
    if (lo > hi || lo > 255 || hi < 0)
        return { 1, 0 };
    return { std::clamp(lo, 0, 255), std::clamp(hi, 0, 255) };
}

auto compute_roi_stats(const core::page& page, std::size_t roi_index,
    std::pair<double, double> range) -> std::optional<roi_stats>
{
    if (roi_index >= page.rois.size() || page.rois[roi_index].path.empty()
        || page.image.isNull()) [[unlikely]]
        return std::nullopt;

    // 选区坐标 = 显示域(orient 后):先对齐再取阈值域;top_left 免变换(隐式共享)
    QImage oriented = page.image;
    if (page.info.orient != common::orientation::top_left) [[unlikely]]
        oriented = page.image.transformed(orient_transform(page.info.orient));

    std::vector<std::uint8_t> plane;
    int width = 0, height = 0;
    if (!to_threshold_plane(oriented, plane, width, height)) [[unlikely]]
        return std::nullopt;

    const QImage region = rasterize(page.rois[roi_index].path, QSize { width, height });

    const auto [lo, hi] = integral_range(range); // 空交集:lo > hi,核内比较恒假
    accum acc { };
    for (int y = 0; y < height; ++y)
        stats_row(region.constScanLine(y), plane.data() + static_cast<std::size_t>(y) * width,
            static_cast<std::size_t>(width), static_cast<std::uint8_t>(lo),
            static_cast<std::uint8_t>(hi), acc);

    roi_stats st;
    st.total = acc.total;
    st.valid = acc.valid;
    if (st.total > 0) [[likely]]
        st.percent = static_cast<double>(st.valid) * 100.0 / static_cast<double>(st.total);
    if (st.valid > 0) {
        const auto n = static_cast<double>(st.valid);
        st.mean = static_cast<double>(acc.sum) / n;
        // 单遍 E[x²]−E[x]²:8 位整数域下双精度精确(65025×2³² << 2⁵³),max 防尾差
        st.std_dev = std::sqrt(
            std::max(0.0, static_cast<double>(acc.sumsq) / n - st.mean * st.mean));
        st.min = acc.vmin;
        st.max = acc.vmax;
    }
    return st;
}

}
