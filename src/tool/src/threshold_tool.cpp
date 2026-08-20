// ==============================================================================
// threshold_tool.cpp — 阈值分割工具实现(highway 加速的比较核)
// ==============================================================================

#include "threshold_tool.hpp"

#include <hwy/highway.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace usip::ui {
namespace {

    namespace hn = hwy::HWY_NAMESPACE; // 静态分派(/arch:AVX2 基线),用法同 document_service

    // 阈值区间 → 整数域边界:对整数 v 有 v >= floor ⟺ v >= ceil(floor),
    // v <= ceil ⟺ v <= floor(ceil);区间与 [0,255] 无交集 → false(产物全 0)
    [[nodiscard]] auto integral_bounds(std::pair<double, double> range, int& lo, int& hi) -> bool
    {
        lo = static_cast<int>(std::ceil(range.first));
        hi = static_cast<int>(std::floor(range.second));
        if (lo > hi || lo > 255 || hi < 0)
            return false;
        lo = std::clamp(lo, 0, 255);
        hi = std::clamp(hi, 0, 255);
        return true;
    }

    // 8 位阈值核(SIMD,尾部标量):[lo,hi] 内 → 255,否则 0
    void threshold_row(const std::uint8_t* src, std::size_t width, std::uint8_t lo,
        std::uint8_t hi, std::uint8_t* dst)
    {
        const hn::ScalableTag<std::uint8_t> d;
        const auto n = hn::Lanes(d);
        const auto vlo = hn::Set(d, lo);
        const auto vhi = hn::Set(d, hi);
        const auto von = hn::Set(d, std::uint8_t { 255 });

        std::size_t x = 0;
        for (; x + n <= width; x += n) {
            const auto v = hn::LoadU(d, src + x);
            const auto inside = hn::And(hn::Ge(v, vlo), hn::Le(v, vhi));
            hn::StoreU(hn::IfThenElse(inside, von, hn::Zero(d)), d, dst + x); // 向量在前,tag 在后
        }
        for (; x < width; ++x)
            dst[x] = src[x] >= lo && src[x] <= hi ? 255 : 0;
    }

    // 整幅 8 位显示域缓冲(行按 width 紧凑排布)→ 掩膜 QImage
    void threshold_buffer(const std::uint8_t* src, int width, int height,
        std::pair<double, double> range, QImage& out)
    {
        if (out.size() != QSize { width, height } || out.format() != QImage::Format_Grayscale8)
            out = QImage { width, height, QImage::Format_Grayscale8 };

        int lo = 0, hi = 0;
        if (!integral_bounds(range, lo, hi)) {
            out.fill(0);
            return;
        }
        for (int y = 0; y < height; ++y, src += width)
            threshold_row(src, static_cast<std::size_t>(width), static_cast<std::uint8_t>(lo),
                static_cast<std::uint8_t>(hi), out.scanLine(y));
    }

    // 图 → 8 位显示域工作面:Grayscale8 直拷(源 stride 可能有行padding,逐行拷);
    // Grayscale16 逐行 >>8(与直方图 bin 同域;一次性开销,不占滑条热路径);
    // 空图/非灰度 → false(display 不填)
    [[nodiscard]] auto to_display_gray8(const QImage& img, std::vector<std::uint8_t>& display,
        int& width, int& height) -> bool
    {
        switch (img.format()) {
        case QImage::Format_Grayscale8: {
            width = img.width();
            height = img.height();
            display.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            for (int y = 0; y < height; ++y)
                std::memcpy(display.data() + static_cast<std::size_t>(y) * width,
                    img.constScanLine(y), static_cast<std::size_t>(width));
            return true;
        }
        case QImage::Format_Grayscale16: {
            width = img.width();
            height = img.height();
            display.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            for (int y = 0; y < height; ++y) {
                const auto* src = reinterpret_cast<const std::uint16_t*>(img.constScanLine(y));
                auto* dst = display.data() + static_cast<std::size_t>(y) * width;
                for (int x = 0; x < width; ++x)
                    dst[x] = static_cast<std::uint8_t>(src[x] >> 8);
            }
            return true;
        }
        default:
            return false;
        }
    }

} // namespace

// ─── threshold_tool ───────────────────────────────────────────────────────────

auto threshold_tool::exec(std::span<const QImage> images, std::pair<double, double> range)
    -> result<void>
{
    if (images.empty() || images.size() > planes_.size() || images.front().isNull())
        return common::fail(common::errc::invalid_argument,
            "threshold tool expects 1 or 2 non-null images, got {}", images.size());

    std::array<plane, 2> planes { };
    if (!to_display_gray8(images.front(), planes.front().display, planes.front().width,
            planes.front().height))
        return common::fail(common::errc::unsupported,
            "threshold tool requires a grayscale main image");
    for (std::size_t i = 1; i < images.size(); ++i) // 副图非灰度:留空,该侧无预览
        [[maybe_unused]] const auto usable
            = to_display_gray8(images[i], planes[i].display, planes[i].width, planes[i].height);

    planes_ = std::move(planes);
    range_ = range;
    count_ = images.size();
    active_ = true;
    recompute();
    return {};
}

void threshold_tool::set_range(std::pair<double, double> range)
{
    if (!active_)
        return;
    range_ = range;
    recompute();
}

auto threshold_tool::active() const noexcept -> bool
{
    return active_;
}

auto threshold_tool::preview() const noexcept -> std::span<const QImage>
{
    return active_ ? std::span<const QImage> { masks_.data(), count_ }
                   : std::span<const QImage> { };
}

auto threshold_tool::range() const noexcept -> std::pair<double, double>
{
    return range_;
}

auto threshold_tool::apply() -> result<outcome>
{
    if (!active_)
        return common::fail(common::errc::failed_precondition,
            "threshold tool has no active session");

    outcome o;
    o.primary.image = std::move(masks_[0]);
    o.primary.range = range_;
    if (count_ > 1 && !masks_[1].isNull()) {
        o.secondary.emplace();
        o.secondary->image = std::move(masks_[1]);
        o.secondary->range = range_;
    }
    release();
    return o;
}

void threshold_tool::cancel() noexcept
{
    release();
}

void threshold_tool::recompute()
{
    for (std::size_t i = 0; i < count_; ++i) {
        const auto& pl = planes_[i];
        if (pl.display.empty()) { // 非灰度:该侧无掩膜
            masks_[i] = { };
            continue;
        }
        threshold_buffer(pl.display.data(), pl.width, pl.height, range_, masks_[i]);
    }
    for (std::size_t i = count_; i < masks_.size(); ++i) // 会话外残留位清空
        masks_[i] = { };
}

void threshold_tool::release() noexcept
{
    active_ = false;
    count_ = 0;
    range_ = { 0.0, 255.0 };
    for (auto& pl : planes_) {
        pl.display = { }; // 移动赋空:即时释放堆内存(销毁资源)
        pl.width = 0;
        pl.height = 0;
    }
    for (auto& m : masks_)
        m = { };
}

}
