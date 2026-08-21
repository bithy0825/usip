#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <QColor>
#include <QImage>

namespace usip::core {

// ─── 伪彩色映射:gray 页归一化到 8 位索引后查 256 项 LUT(渲染 L1 原始层)────
enum class colormap_type : std::uint8_t {
    jet,
    parula,
    viridis,
    plasma,
};

// config 键 pseudocolor.colormap 的字符串互转;未知名字返回 nullopt
[[nodiscard]] constexpr auto to_string(colormap_type type) noexcept -> std::string_view
{
    switch (type) {
    case colormap_type::jet:
        return "jet";
    case colormap_type::parula:
        return "parula";
    case colormap_type::viridis:
        return "viridis";
    case colormap_type::plasma:
        return "plasma";
    }
    std::unreachable();
}

[[nodiscard]] constexpr auto colormap_from_string(std::string_view name) noexcept
    -> std::optional<colormap_type>
{
    if (name == "jet")
        return colormap_type::jet;
    if (name == "parula")
        return colormap_type::parula;
    if (name == "viridis")
        return colormap_type::viridis;
    if (name == "plasma")
        return colormap_type::plasma;
    return std::nullopt;
}

// ─── LUT:256 项 RGBA8888 内存字,一次生成、渲染帧间复用 ──────────────────────
// 每项为小端 u32:r | g<<8 | b<<16 | 0xFF<<24 —— 与 Format_RGBA8888 的字节序
// 一致,可直写目标 scanLine。zero_is_black 生成期烘焙进 LUT[0]:8 位原始 0 与
// 16 位归一化(带 clamp)后的索引 0 均命中,运行时零分支
struct color_lut {
    std::array<std::uint32_t, 256> entries {};
    colormap_type type { colormap_type::jet };
    bool zero_is_black { false };
};

[[nodiscard]] auto make_color_lut(colormap_type type, bool zero_is_black) -> color_lut;

// 允许伪彩色的 QImage 格式:仅 document_service 产出的两种灰度原生格式;
// rgb/rgba 等彩色页不允许伪彩色(渲染层回落原样显示)
[[nodiscard]] constexpr auto pseudocolorable(QImage::Format format) noexcept -> bool
{
    return format == QImage::Format_Grayscale8 || format == QImage::Format_Grayscale16;
}

// ─── 着色:page 原始灰度 QImage → RGBA8888(Highway SIMD,单遍无中间缓冲)───
//   * 仅 Grayscale8 / Grayscale16;其余格式(彩色)返回空 QImage 并告警,
//     调用方(渲染管线)据此回落原样显示
//   * 16 位按 range16 归一化到 8 位索引再查表(渲染层传 hist.range_min/max,
//     缺省全量程 [0,65535]);越界 clamp,故原始 0 像素恒落索引 0
[[nodiscard]] auto colorize(const QImage& gray, const color_lut& lut,
    std::optional<std::pair<double, double>> range16 = std::nullopt) -> QImage;

// ─── diff_lut:511 项表,两幅 8 位图的差值域 [-255,255],索引 = d + 255 ───────
// 由 256 项基础表重采样:p = (d+255)×255/510 —— 奇数 d 落整数位(精确取表项),
// 偶数 d 落半格(相邻项精确中点),sRGB 线性插值与连续映射的采样方式一致,
// 平滑度等价连续采样;运行时直接索引,无归一化运算。
// zero_is_black 与 color_lut 语义统一为"域的 0 值固定纯黑":此处烘焙
// entries[255](零差异),非索引 0
struct diff_lut {
    std::array<std::uint32_t, 511> entries {};
    colormap_type type { colormap_type::jet };
    bool zero_is_black { false };
};

[[nodiscard]] auto make_diff_lut(colormap_type type, bool zero_is_black) -> diff_lut;

// ─── 差值着色:偏移编码差值图 → RGBA8888(Highway SIMD 直接索引)────────────
// 输入约定:差值图 = int 求差、clamp [-255,255] 后偏移编码 d+255 存
// Format_Grayscale16(值域 0..510);超出 510 的脏值被 clamp 到表尾。
// 非 Grayscale16(含 8 位灰度与彩色)返回空 QImage 并告警
[[nodiscard]] auto colorize_diff(const QImage& diff, const diff_lut& lut) -> QImage;

// ─── ROI 选区颜色:编号 → 颜色 ─────────────────────────────────────────────
// 选区在 page.rois vector 中的编号 → 渲染色(L4 蚂蚁线、L6 临时层与 infodock
// 选区列表共用一处,保证同编号同色)。前 12 项固定查表(色环 12 等分,
// 旧版同款);超出后黄金角 HSV 生成(hue 步进 137.507764°,饱和度/明度按
// 编号取模抖动),相邻编号保持可分辨
[[nodiscard]] auto roi_color(std::size_t index) -> QColor;

}
