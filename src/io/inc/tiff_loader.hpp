#pragma once

// ==============================================================================
// tiff_loader.hpp — TIFF 加载:open → 聚合页头 → 设备识别 → 策略过滤 → 读+规则
//
// 职责边界:io 层产出"原始数据 + 元数据",规则本体(Highway 实现)与设备档案
// 统一在 common/tiff.hpp 管理;本层只负责读取、策略硬过滤、按设备 apply。
// ==============================================================================

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "error.hpp"
#include "tiff.hpp"

namespace usip::io {

// ─── 加载策略(disable 选项 + 资源护栏):命中即报错,不进入像素读取 ─────────────

struct tiff_policy {
    std::uint32_t max_dimension = 65535; // 单边像素上限
    std::uint64_t max_pixels_per_page = 268'435'455; // 单页像素上限

    bool disable_big_tiff = false; // 拒绝 BigTIFF
    bool disable_sub_ifds = false; // 拒绝携带 SubIFD 的文件
};

[[nodiscard]] auto check_policy(const common::tiff_info& info, const tiff_policy& policy)
    -> result<>;

// ─── 读取器(RAII;无 pimpl,句柄即 void*)──────────────────────────────────────

class tiff_reader {
public:
    tiff_reader() = default;
    tiff_reader(tiff_reader&&) noexcept;
    ~tiff_reader();

    tiff_reader(const tiff_reader&) = delete;
    auto operator=(const tiff_reader&) -> tiff_reader& = delete;
    auto operator=(tiff_reader&&) = delete; // tiff_info 的 const id 使移动赋值不可用;按值返回走移动构造

    // 打开:逐目录填充页头 → 聚合 tiff_info → 按特征判定来源设备(device 字段)
    [[nodiscard]] static auto open(const std::filesystem::path& path) -> result<tiff_reader>;

    [[nodiscard]] auto info() const noexcept -> const common::tiff_info& { return info_; }

    // 读第 index 页原始像素(info().pages 下标;内部映射到 TIFF 目录序号)
    // dst 至少 pages[index].byte_size() 字节
    [[nodiscard]] auto read_page(std::uint32_t index, std::span<std::byte> dst) const
        -> result<>;

private:
    void* tif_ = nullptr; // TIFF*(不引 tiffio.h 进头文件)
    common::tiff_info info_;
    std::vector<std::uint16_t> dir_indices_; // pages[i] → TIFF 目录序号(有跳页)
};

// ─── 组合操作(主流程)────────────────────────────────────────────────────────
// open → check_policy → 逐页读入单块缓冲 → 按识别到的设备档案依次应用其规则
// (设备规则:tiff.hpp 统一管理;本层只按设备调用)

struct loaded_tiff {
    common::tiff_info info;
    std::vector<std::byte> pixels; // 全部页按序拼接;规则应用后尺寸可能已收缩
};

[[nodiscard]] auto load_tiff(const std::filesystem::path& path,
    const tiff_policy& policy = { }) -> result<loaded_tiff>;

} // namespace usip::io
