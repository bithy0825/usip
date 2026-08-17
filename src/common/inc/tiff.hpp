#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <cuuidpp/cuuidpp.hpp>

namespace usip::common {

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

struct device_probe_facts {
    using mask = std::uint32_t;

    static constexpr mask format_uint8 = 1u << 0; // 全部页面为 uint8
    static constexpr mask palette_midpoint_zero = 1u << 1; // pva 特征:colormap 红通道中点为 0
    static constexpr mask rgb_channels_identical = 1u << 2; // casic 特征:RGB 三通道逐像素一致
    static constexpr mask alpha_all_255 = 1u << 3; // casic 特征:第 4 通道恒 255(冗余 alpha)

    mask bits = 0;

    constexpr void set(mask m) noexcept { bits |= m; }
    [[nodiscard]] constexpr auto has(mask m) const noexcept -> bool { return (bits & m) != 0; }
    [[nodiscard]] constexpr auto has_all(mask m) const noexcept -> bool { return (bits & m) == m; }
};

struct page_info {
    cuuidpp::uuid id = cuuidpp::generate_uuid(); // 打开时生成(v7)

    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    std::uint16_t channels { 1 }; // SamplesPerPixel,物理数量(见上注释)
    sample_format format { sample_format::uint8 };
    pixel_class klass { pixel_class::gray };
    bool zero_is_white { false }; // MINISWHITE:显示管线反相,不碰像素

    orientation orient { orientation::top_left };
    std::optional<float> dpi_x, dpi_y; // 已归一化;文件缺 tag 则为 nullopt

    std::optional<histogram> hist;

    [[nodiscard]] constexpr auto byte_size() const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(width) * height * channels
            * sample_format_size(format);
    }
};

// ASCII 大小写折叠的子串包含(空 needle 恒 false)
[[nodiscard]] auto contains_ci(std::string_view haystack, std::string_view needle) -> bool;

struct tiff_info {
    // 注:id 刻意不加 const —— const 成员会删除赋值运算符,
    // unordered_map<uuid, document> 的值语义替换将不可用(与 page_info 同理)
    cuuidpp::uuid id { cuuidpp::generate_uuid() }; // 文档级 uuid
    std::filesystem::path path { };
    bool big_tiff { false };
    bool has_sub_ifds { false }; // 任一页携带 SubIFD 标签(策略过滤用;读取不深入)

    std::string make, model, software, description; // 设备档案
    device_probe_facts facts; // 设备特征事实

    enum class source : std::uint8_t { none,
        tags,
        signature,
    };
    source detected_by { source::none };

    std::string_view device { };

    std::vector<page_info> pages;

    float slice_spacing { 1.0f }; // z 层距:文件携带则用,否则 1(既定决策)

    [[nodiscard]] auto anonymous() const noexcept -> bool
    {
        return make.empty();
    }

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

// ─── 设备 CRTP 基类:一个厂家+型号一个类,静态多态 ────────────────────────────
// 派生类契约(按名遮蔽覆盖;缺省即无):
//   * make      —— 设备唯一身份(厂家+型号):匹配 needle + 结论键
//   * facts     —— 特征兜底(文件无 Make 时须全部命中)
//   * reject    —— 特征位,命中任一即排除
//   * transform —— 命中后的像素规则
// 判定语义(防误报):文件自带 Make → 只认 make 命中(未命中即终判,不再走特征;
// software/description 是导出软件写的,证明不了设备);文件无 Make → 特征兜底

template <typename Derived>
class tiff_device {
public:
    static constexpr device_probe_facts facts { };
    static constexpr device_probe_facts::mask reject { 0 };

    static void transform(std::vector<std::byte>&, tiff_info&) { }

    [[nodiscard]] static auto match(const tiff_info& info) -> bool
    {
        if (!info.make.empty())
            return contains_ci(info.make, Derived::make);
        return info.facts.has_all(Derived::facts.bits) && !info.facts.has(Derived::reject);
    }
};

// 只保留指定通道(冗余编码收窄);应用后各页 channels=1、klass → gray
auto keep_channel(std::vector<std::byte>& pixels, tiff_info& doc, std::uint16_t channel) -> void;
// pva:调色板 ±128 偏移解码;应用后 klass → gray
auto palette_signed_decode(std::vector<std::byte>& pixels, tiff_info& doc) -> void;

// 德国 PVA(Winsam 扫描声镜):palette ±128 偏移编码;Make 自报 "PVA Tepla ..."
class pva : public tiff_device<pva> {
public:
    static constexpr std::string_view make { "pva" };

    // 无 Make 兜底:不含 rgb_ident —— 真样例为 palette 单通道,永远没有 RGB 页
    static constexpr device_probe_facts facts {
        .bits = device_probe_facts::palette_midpoint_zero | device_probe_facts::format_uint8,
    };

    static void transform(std::vector<std::byte>& pixels, tiff_info& doc)
    {
        keep_channel(pixels, doc, 0); // 若有冗余通道则收窄(palette 单通道时静默跳过)
        palette_signed_decode(pixels, doc);
    }
};

// 航天测控 AMC99026:RGBA 冗余编码(R=G=B、alpha 恒 255);样例零身份 tag
class casic_amc99026 : public tiff_device<casic_amc99026> {
public:
    static constexpr std::string_view make { "casic_amc99026" };

    static constexpr device_probe_facts facts {
        .bits = device_probe_facts::rgb_channels_identical
            | device_probe_facts::alpha_all_255
            | device_probe_facts::format_uint8,
    };

    static void transform(std::vector<std::byte>& pixels, tiff_info& doc)
    {
        keep_channel(pixels, doc, 0); // R=G=B 冗余,只留 R
    }
};

using tiff_device_list = std::tuple<pva, casic_amc99026>;

// 检测:遍历 tiff_device_list 依次 match,直到命中;结论写入 info
auto detect_device(tiff_info& info) -> void;

// 规则应用:按 info.device 找到设备类,应用其 transform(load_tiff 读完全部页后调用)
auto apply_device_rules(std::vector<std::byte>& pixels, tiff_info& doc) -> void;

struct loaded_tiff {
    tiff_info info;
    std::vector<std::byte> pixels; // 全部页按序拼接;设备规则应用后尺寸可能已收缩
};

}
