// ==============================================================================
// tiff.cpp — tiff.hpp 的实现:转换规则(Highway 静态分派加速)+ 设备档案注册表
//
// 静态分派:全项目统一编译基线(/arch:AVX2,见 cmake/usip-config.cmake)。
// 每个 TU 直接用 hwy::HWY_NAMESPACE 写 SIMD —— 无需 foreach_target / HWY_EXPORT /
// HWY_DYNAMIC_DISPATCH,PCH 亦完全兼容(不再需要 SKIP_PRECOMPILE_HEADERS)。
// ==============================================================================

#include "tiff.hpp"

#include <array>
#include <cstring>

#include <hwy/highway.h>

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

    // 无符号整型反相:~v ≡ (2^N − 1) − v(对 uint8/16/32/64 通用)
    template <std::unsigned_integral T>
    void invert_unsigned(T* data, std::size_t count)
    {
        const hn::ScalableTag<T> d;
        const std::size_t n = hn::Lanes(d);

        std::size_t i = 0;
        for (; i + n <= count; i += n)
            hn::StoreU(hn::Not(hn::LoadU(d, data + i)), d, data + i);
        for (; i < count; ++i)
            data[i] = static_cast<T>(~data[i]);
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

// ─── 内置规则 ────────────────────────────────────────────────────────────────

namespace rules {

    auto keep_channel(std::uint16_t channel) -> transform_fn
    {
        return [channel](std::vector<std::byte>& pixels, tiff_info& doc) {
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
        };
    }

    auto invert_if_needed() -> transform_fn
    {
        return [](std::vector<std::byte>& pixels, tiff_info& doc) {
            auto& first = doc.pages.front();
            if (!first.zero_is_white)
                return;

            const auto sample_bytes = sample_format_size(first.format);
            const std::size_t total = total_pixels(doc) * first.channels;
            if (pixels.size() < total * sample_bytes)
                return;

            switch (first.format) {
            case sample_format::uint8:
                invert_unsigned(reinterpret_cast<std::uint8_t*>(pixels.data()), total);
                break;
            case sample_format::uint16:
                invert_unsigned(reinterpret_cast<std::uint16_t*>(pixels.data()), total);
                break;
            case sample_format::uint32:
                invert_unsigned(reinterpret_cast<std::uint32_t*>(pixels.data()), total);
                break;
            case sample_format::uint64:
                invert_unsigned(reinterpret_cast<std::uint64_t*>(pixels.data()), total);
                break;
            default:
                return; // 有符号/浮点无自然反相 → 跳过
            }

            for (auto& p : doc.pages)
                p.zero_is_white = false;
        };
    }

    auto palette_signed_decode() -> transform_fn
    {
        return [](std::vector<std::byte>& pixels, tiff_info& doc) {
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
        };
    }

} // namespace rules

// ─── 设备档案表 ──────────────────────────────────────────────────────────────
// 按枚举值排列(unknown=0、pva=1、casic=2)—— device_profile_of 可 O(1) 索引。
// 新增设备:tiff_device 加枚举值(保持连续)+ 这里按序加一条。

static const std::array<tiff_device_profile, 3> profiles = { {
    tiff_device_profile {
        .id = tiff_device::unknown,
        .rules = { },
    },
    tiff_device_profile {
        .id = tiff_device::pva_unknown,
        .require = device_probe_facts::palette_midpoint_zero
            | device_probe_facts::rgb_channels_identical
            | device_probe_facts::format_uint8,
        .rules = { rules::keep_channel(0), rules::palette_signed_decode() },
    },
    tiff_device_profile {
        .id = tiff_device::casic_unknown,
        .require = device_probe_facts::rgb_channels_identical
            | device_probe_facts::format_uint8,
        .rules = { rules::keep_channel(0) },
    },
} };

auto device_profiles() -> std::span<const tiff_device_profile>
{
    return profiles;
}

auto detect_device(const device_probe_facts& facts) -> tiff_device
{
    for (const auto& profile : profiles)
        if (profile.id != tiff_device::unknown && profile.matches(facts))
            return profile.id;
    return tiff_device::unknown;
}

auto device_profile_of(tiff_device device) -> const tiff_device_profile&
{
    return profiles[static_cast<std::size_t>(device)]; // 枚举值即数组下标
}

} // namespace usip::common
