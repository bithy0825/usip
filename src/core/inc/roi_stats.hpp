#pragma once

// ==============================================================================
// roi_stats.hpp — 选区统计(infodock 数据源;无总线/无控件,纯函数)
//
// compute_roi_stats(page, 选区编号, 阈值区间) → infodock 一行所需的全部统计:
// 选区矢量路径(图像显示域坐标,与 L4 渲染同侧)先栅格化为二值区域,再与页像
// 素的 8 位阈值域(Grayscale8 直取,Grayscale16 >>8 —— 与 threshold_tool 同
// 口径)逐像素联合统计,单遍完成,highway 加速(静态分派 /arch:AVX2 基线):
//   total               选区内像素数(Pixel 列)
//   valid               其中灰度落入 [floor,ceil] 的像素数(Valid 列)
//   percent             valid/total × 100
//   mean/std_dev/min/max 有效像素灰度统计(仅 valid > 0 有意义,调用方留空)
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "document.hpp"

namespace usip::core {

// 单选区统计结果:total/valid/percent 恒有效;mean/std_dev/min/max 仅 valid>0
struct roi_stats {
    std::uint64_t total { 0 };
    std::uint64_t valid { 0 };
    double percent { 0.0 };
    double mean { 0.0 };
    double std_dev { 0.0 };
    int min { 0 };
    int max { 0 };
};

// 页 + 选区编号 + 阈值区间 → 一行的统计内容;
// nullopt:编号越界 / 空路径 / 空图 / 非灰度页(无阈值域,与 threshold_tool 同限制)
[[nodiscard]] auto compute_roi_stats(const core::page& page, std::size_t roi_index,
    std::pair<double, double> range) -> std::optional<roi_stats>;

// 阈值区间 → 8 位整数域边界(与 threshold_tool 同口径:下限取 ceil、上限取
// floor、钳 [0,255]);空交集(lo > hi)返回 {1,0}
[[nodiscard]] auto integral_range(std::pair<double, double> range) -> std::pair<int, int>;

}
