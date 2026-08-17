// ==============================================================================
// tiff.cpp — tiff.hpp 的实现:像素规则内核(Highway 静态分派)+ 设备检测遍历
//
// 静态分派:全项目统一编译基线(/arch:AVX2,见 cmake/usip-config.cmake)。
// 每个 TU 直接用 hwy::HWY_NAMESPACE 写 SIMD —— 无需 foreach_target / HWY_EXPORT /
// HWY_DYNAMIC_DISPATCH,PCH 亦完全兼容(不再需要 SKIP_PRECOMPILE_HEADERS)。
// ==============================================================================

#include "tiff.hpp"

#include <algorithm>
#include <cstring>
#include <hwy/highway.h>
#include <type_traits>

namespace usip::common {
namespace {

    namespace hn = hwy::HWY_NAMESPACE;

    // ─── SIMD 内核 ───────────────────────────────────────────────────────────────

    // u8 四通道提取单通道:LoadInterleaved4 按通道分离,StoreU 写回目标通道
    void extract_channel_u8_4ch(const std::uint8_t* src, std::uint8_t* dst,
        std::size_t pixel_count, std::uint16_t channel)
    {
        const hn::ScalableTag<std::uint8_t> d;
        const std::size_t n = hn::Lanes(d);

        std::size_t i = 0;
        for (; i + n <= pixel_count; i += n) {
            hn::Vec<decltype(d)> c0, c1, c2, c3;
            hn::LoadInterleaved4(d, src + i * 4, c0, c1, c2, c3);
            const auto picked = channel == 0 ? c0 : channel == 1 ? c1
                : channel == 2                                   ? c2
                                                                 : c3;
            hn::StoreU(picked, d, dst + i);
        }
        for (; i < pixel_count; ++i)
            dst[i] = src[i * 4 + channel];
    }

    // pva ±128 偏移解码(uint8),全程 uint8 运算无需加宽:
    //   val <  128 → 255 − 2·val   (即 ~(val << 1))
    //   val ≥ 128 → (2·val) mod 256 (即 val << 1 的自然回绕)
    void palette_signed_decode_u8(std::uint8_t* data, std::size_t count)
    {
        const hn::ScalableTag<std::uint8_t> d;
        const std::size_t n = hn::Lanes(d);
        const auto threshold = hn::Set(d, std::uint8_t { 128 });

        std::size_t i = 0;
        for (; i + n <= count; i += n) {
            const auto val = hn::LoadU(d, data + i);
            const auto doubled = hn::ShiftLeft<1>(val);
            const auto inverted = hn::Not(doubled);
            const auto is_lo = hn::Lt(val, threshold);
            hn::StoreU(hn::IfThenElse(is_lo, inverted, doubled), d, data + i);
        }
        for (; i < count; ++i) {
            const unsigned val = data[i];
            data[i] = static_cast<std::uint8_t>(
                val < 128 ? 255 - 2 * val : (2 * val) & 0xFF);
        }
    }

    // ─── 标量后备(任意位深/通道数)──────────────────────────────────────────────

    void extract_channel_scalar(std::byte* data, std::size_t pixel_count,
        std::uint16_t channels, std::uint16_t channel, std::size_t sample_bytes)
    {
        const std::size_t src_stride = channels * sample_bytes;
        const std::size_t offset = static_cast<std::size_t>(channel) * sample_bytes;
        for (std::size_t i = 0; i < pixel_count; ++i)
            std::memmove(data + i * sample_bytes,
                data + i * src_stride + offset, sample_bytes);
    }

    [[nodiscard]] auto total_pixels(const tiff_info& doc) -> std::size_t
    {
        std::size_t n = 0;
        for (const auto& p : doc.pages)
            n += static_cast<std::size_t>(p.width) * p.height;
        return n;
    }

} // namespace

// ─── 像素规则内核(软应用:条件不满足 → 静默跳过)─────────────────────────────

auto keep_channel(std::vector<std::byte>& pixels, tiff_info& doc, std::uint16_t channel) -> void
{
    auto& first = doc.pages.front();
    const auto channels = first.channels;
    if (channels <= 1 || channel >= channels)
        return; // 不适用 → 静默跳过

    const auto sample_bytes = sample_format_size(first.format);
    const std::size_t pixel_count = total_pixels(doc);
    if (pixels.size() < pixel_count * channels * sample_bytes)
        return;

    if (sample_bytes == 1 && channels == 4) {
        // 读写同址:写出始终落后读入,安全
        extract_channel_u8_4ch(
            reinterpret_cast<const std::uint8_t*>(pixels.data()),
            reinterpret_cast<std::uint8_t*>(pixels.data()), pixel_count, channel);
    } else {
        extract_channel_scalar(pixels.data(), pixel_count, channels, channel,
            sample_bytes);
    }

    pixels.resize(pixel_count * sample_bytes);
    for (auto& p : doc.pages) {
        p.channels = 1;
        if (p.klass == pixel_class::rgb || p.klass == pixel_class::rgba)
            p.klass = pixel_class::gray; // 单通道即灰度语义
    }
}

auto palette_signed_decode(std::vector<std::byte>& pixels, tiff_info& doc) -> void
{
    auto& first = doc.pages.front();
    if (first.klass != pixel_class::palette)
        return;
    if (first.format != sample_format::uint8)
        return;

    const std::size_t pixel_count = total_pixels(doc);
    if (pixels.size() < pixel_count)
        return;

    palette_signed_decode_u8(
        reinterpret_cast<std::uint8_t*>(pixels.data()), pixel_count);

    for (auto& page : doc.pages)
        page.klass = pixel_class::gray; // 解码后为强度值
}

// ─── 文本比对 ────────────────────────────────────────────────────────────────

auto contains_ci(std::string_view haystack, std::string_view needle) -> bool
{
    if (needle.empty() || haystack.size() < needle.size())
        return false;

    const auto folded = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (std::equal(needle.begin(), needle.end(), haystack.begin() + i,
                [&folded](char a, char b) { return folded(a) == folded(b); }))
            return true;
    }
    return false;
}

// ─── 检测遍历与规则应用(编译期设备列表展开)──────────────────────────────────

namespace {

    // 依次调用 match,直到命中;首个命中写入结论,后续设备跳过
    template <typename... Ds>
    auto detect_impl(tiff_info& info, std::type_identity<std::tuple<Ds...>>) -> void
    {
        ((info.device.empty() && Ds::match(info)
                 ? (info.device = Ds::make,
                       info.detected_by = info.anonymous()
                           ? tiff_info::source::signature
                           : tiff_info::source::tags,
                       true)
                 : false),
            ...);
    }

    // 按 info.device 找到设备类,应用其 transform
    template <typename... Ds>
    auto apply_impl(std::vector<std::byte>& pixels, tiff_info& doc,
        std::type_identity<std::tuple<Ds...>>) -> void
    {
        ((doc.device == Ds::make ? (Ds::transform(pixels, doc), true) : false), ...);
    }

} // namespace

auto detect_device(tiff_info& info) -> void
{
    info.device = { };
    info.detected_by = tiff_info::source::none;
    detect_impl(info, std::type_identity<tiff_device_list> { });
}

auto apply_device_rules(std::vector<std::byte>& pixels, tiff_info& doc) -> void
{
    apply_impl(pixels, doc, std::type_identity<tiff_device_list> { });
}

} // namespace usip::common
