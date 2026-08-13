#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <cuuidpp/cuuidpp.hpp>

#include "error.hpp"

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

// ─── 设备识别:按特征(signature)判定来源设备 ─────────────────────────────────
// 每个设备一种特征;读取时先聚合全部页头,再依特征命中注册表。
// 新增设备 = 枚举加值 + 注册表加一条档案,其余零改动。
enum class tiff_device : std::uint8_t {
    unknown = 0, // 未识别(所有特征均不匹配)
    pva_unknown, // 德国 PVA 设备,型号未知 —— 特征:调色板 colormap 红通道中点为 0
                 // (像素实为 ±128 偏移的有符号强度编码)
    casic_unknown, // 航天测控设备,型号未知 —— 特征:RGB 三通道数据完全一致
};

// 设备特征事实:位标志(由 io 层在打开时收集:libtiff 标签 + 首页像素采样)
// 新增特征 = 加一位常量 + io 层一处 set(),其余零改动
struct device_probe_facts {
    using mask = std::uint32_t;

    static constexpr mask palette_midpoint_zero = 1u << 0; // pva 特征
    static constexpr mask rgb_channels_identical = 1u << 1; // casic / pva 特征
    static constexpr mask format_uint8 = 1u << 2; // 全部页面为 uint8(规则适用前提)

    mask bits = 0;

    constexpr void set(mask m) noexcept { bits |= m; }
    [[nodiscard]] constexpr auto has(mask m) const noexcept -> bool { return (bits & m) != 0; }
    [[nodiscard]] constexpr auto has_all(mask m) const noexcept -> bool { return (bits & m) == m; }
};

// ─── 转换规则与设备档案 ─────────────────────────────────────────────────────
// 规则输入 = 原始数据 + tiff_info(按描述/设备做数据转换);
// 语义为软应用:条件不满足 → 静默跳过,不报错。

struct tiff_info;

// 规则签名:两个参数 —— 原始数据 + tiff_info(页信息经 doc.pages 访问/改写);
// 语义为软应用:条件不满足 → 静默跳过,不报错
using transform_fn = std::function<void(std::vector<std::byte>& pixels, tiff_info& doc)>;

// 设备档案:一个设备拥有 0..N 条规则;matches() 判定特征,apply() 依次应用规则
struct tiff_device_profile {
    tiff_device id = tiff_device::unknown;
    device_probe_facts::mask require = 0; // 必须全部命中的特征(组合条件:按位或)
    device_probe_facts::mask reject = 0; // 命中任一即排除的特征
    std::vector<transform_fn> rules; // 0..N 条

    [[nodiscard]] constexpr auto matches(const device_probe_facts& facts) const noexcept -> bool
    {
        return facts.has_all(require) && !facts.has(reject);
    }

    void apply(std::vector<std::byte>& pixels, tiff_info& doc) const
    {
        for (const auto& rule : rules)
            rule(pixels, doc);
    }
};

// 内置规则工厂(实现见 src/tiff.cpp;Highway 静态分派,各 TU 独立使用)
namespace rules {
    // 只保留指定通道(casic:R=G=B → 只留 R);应用后各页 channels=1、klass → gray
    [[nodiscard]] auto keep_channel(std::uint16_t channel) -> transform_fn;
    // zero_is_white 的整型页反相;其余 → 跳过
    [[nodiscard]] auto invert_if_needed() -> transform_fn;
    // pva:调色板有符号偏移解码;应用后 klass → gray
    [[nodiscard]] auto palette_signed_decode() -> transform_fn;
} // namespace rules

// 设备档案注册表(统一管理;实现见 src/tiff.cpp)
[[nodiscard]] auto device_profiles() -> std::span<const tiff_device_profile>;
[[nodiscard]] auto detect_device(const device_probe_facts& facts) -> tiff_device;
[[nodiscard]] auto device_profile_of(tiff_device device) -> const tiff_device_profile&;

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

    // 来源设备:聚合全部页头后按特征判定(见 tiff_device)
    tiff_device device { tiff_device::unknown };

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
