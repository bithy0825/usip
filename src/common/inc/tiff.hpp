#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <cuuidpp/cuuidpp.hpp>

#include "error.hpp"
#include "tiff_device.hpp"

namespace usip::common {

// ─── 采样格式:管线实际支持的全集 ─────────────────────────────────────────────
// 文件声明的格式与此不一致(复数、12bit 等)→ 打开时以 errc::unsupported 拒绝

enum class sample_format : std::uint8_t {
    uint8,
    uint16,
    uint32,
    uint64,
    int8,
    int16,
    int32,
    int64,
    float32,
    float64
};

[[nodiscard]] constexpr auto sample_format_size(sample_format f) noexcept -> std::size_t
{
    switch (f) {
    case sample_format::uint8:
    case sample_format::int8:
        return 1;
    case sample_format::uint16:
    case sample_format::int16:
        return 2;
    case sample_format::uint32:
    case sample_format::int32:
    case sample_format::float32:
        return 4;
    case sample_format::uint64:
    case sample_format::int64:
    case sample_format::float64:
        return 8;
    }
    return 0;
}

[[nodiscard]] constexpr auto is_signed(sample_format f) noexcept -> bool
{
    switch (f) {
    case sample_format::int8:
    case sample_format::int16:
    case sample_format::int32:
    case sample_format::int64:
    case sample_format::float32:
    case sample_format::float64:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr auto is_floating(sample_format f) noexcept -> bool
{
    return f == sample_format::float32 || f == sample_format::float64;
}

enum class pixel_class : std::uint8_t {
    gray, // 1 通道
    gray_alpha, // 2 通道
    rgb, // 3 通道
    rgba, // 4 通道(含未声明类型的附加通道:SAM 真样本的常见非标准写法)
    palette, // 1 通道索引 + 调色板
    cmyk, // 4 通道分色
};

// 显示翻转
enum class orientation : std::uint8_t {
    top_left,
    top_right,
    bottom_right,
    bottom_left,
    left_top,
    right_top,
    right_bottom,
    left_bottom
};

// ─── 设备识别:见 tiff_device.hpp(身份 + 档案注册表,单一文件收拢)────────────

// ─── 直方图:派生统计(由 service 层从像素计算)─────────────────────────────────
// 通道数是运行期值(读取变换规则可能只保留 R 通道、或任意重组),
// 因此存储用运行期长度的 vector —— 不做定长模板(浪费内存),也不设 4 通道上限。
// bin 映射:uint8 一值一 bin(精确);其余格式把 [range_min, range_max] 线性均分。
struct histogram {
    static constexpr std::size_t bin_count = 256;

    std::uint16_t channels = 0;
    std::vector<std::uint64_t> bins; // 长度 = channels * bin_count,通道优先排列
    std::vector<double> range_min; // 每通道值域
    std::vector<double> range_max;

    [[nodiscard]] auto bins_of(std::uint16_t channel) const noexcept
        -> std::span<const std::uint64_t, bin_count>
    {
        return std::span<const std::uint64_t, bin_count> {
            bins.data() + static_cast<std::size_t>(channel) * bin_count, bin_count
        };
    }
};

struct page_info {
    cuuidpp::uuid id = cuuidpp::generate_uuid(); // 打开时生成(v7)
    // 注:id 刻意不加 const —— const 成员会删除赋值运算符,vector 元素将不可替换

    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    std::uint16_t channels { 1 }; // SamplesPerPixel,物理数量(见上注释)
    sample_format format { sample_format::uint8 };
    pixel_class klass { pixel_class::gray };
    bool zero_is_white { false }; // MINISWHITE:显示管线反相,不碰像素

    orientation orient { orientation::top_left };
    std::optional<float> dpi_x, dpi_y; // 已归一化;文件缺 tag 则为 nullopt

    // 派生统计:由 service 层按需计算填充(nullopt = 尚未计算)
    std::optional<histogram> hist;

    [[nodiscard]] constexpr auto byte_size() const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(width) * height * channels
            * sample_format_size(format);
    }
};

// ─── 文档描述:文件级;3D 体视图的来源 ─────────────────────────────────────────

struct tiff_info {
    const cuuidpp::uuid id { cuuidpp::generate_uuid() }; // 文档级 uuid
    std::filesystem::path path { };
    bool big_tiff { false };
    bool has_sub_ifds { false }; // 任一页携带 SubIFD 标签(策略过滤用;读取不深入)

    // 来源设备:身份线索 + 判定结论(设备键 / 判定来源;见 tiff_device.hpp)
    tiff_identity identity { };

    std::vector<page_info> pages;

    float slice_spacing { 1.0f }; // z 层距:文件携带则用,否则 1(既定决策)

    [[nodiscard]] auto uniform() const noexcept -> bool
    {
        if (pages.size() < 2)
            return true;
        const auto& first = pages.front();
        for (const auto& p : pages) {
            if (p.width != first.width || p.height != first.height
                || p.channels != first.channels || p.format != first.format
                || p.klass != first.klass)
                return false;
        }
        return true;
    }
};

struct loaded_tiff {
    tiff_info info;
    std::vector<std::byte> pixels; // 全部页按序拼接;设备规则应用后尺寸可能已收缩
};

// ─── 体描述:tiff_info 的投影(组合,不继承)────────────────────────────────────
// 只有 uniform 文档才能构造;构造函数不能返回错误,因此走工厂函数:
// 失败时 result 带具体原因(全项目约定;common::exception 仅用于真正的例外)
struct volume_info {
    std::uint32_t width = 0, height = 0, depth = 0;
    float spacing_x = 1.0f, spacing_y = 1.0f, spacing_z = 1.0f;

    std::uint16_t channels = 1;
    sample_format format = sample_format::uint8;
    pixel_class klass = pixel_class::gray;
    bool zero_is_white = false;
    orientation orient = orientation::top_left;

    [[nodiscard]] static auto from(const tiff_info& doc) -> result<volume_info>
    {
        if (doc.pages.empty())
            return common::fail(common::errc::invalid_argument,
                "tiff document has no pages");
        if (!doc.uniform())
            return common::fail(common::errc::failed_precondition,
                "tiff pages are not uniform (width/height/channels/format/class must match)");

        const auto& p = doc.pages.front();
        return volume_info {
            .width = p.width,
            .height = p.height,
            .depth = static_cast<std::uint32_t>(doc.pages.size()),
            .spacing_x = p.dpi_x ? 25.4f / *p.dpi_x : 1.0f,
            .spacing_y = p.dpi_y ? 25.4f / *p.dpi_y : 1.0f,
            .spacing_z = doc.slice_spacing,
            .channels = p.channels,
            .format = p.format,
            .klass = p.klass,
            .zero_is_white = p.zero_is_white,
            .orient = p.orient,
        };
    }
};

} // namespace usip::common
