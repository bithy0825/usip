#pragma once

// ==============================================================================
// platform/gpu.hpp — GPU 适配器查询(无需图形上下文的设备级信息)
//
// 数据源:Windows DXGI / Linux sysfs(drm)。
// 注意边界:需要 OpenGL 上下文才能拿到的信息(GL_RENDERER 字符串、max_samples 等)
// 不属于本模块,归 renderer 层。
// ==============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace usip::platform {

class gpu {
public:
    struct info {
        std::string vendor;                   // "NVIDIA" / "AMD" / "Intel" / 未知为 "0x10DE" 形式
        std::string device_name;              // 适配器名称
        std::uint64_t dedicated_vram_bytes = 0;  // 独立显存(Linux 暂为 0,见实现注释)
    };

    gpu() = delete;

    // 全部适配器,按独立显存降序(双显卡笔记本上独显在前);纯软件适配器(WARP 等)已剔除
    [[nodiscard]] static auto devices() -> const std::vector<info>&;

    // 首选适配器(显存最大者);无适配器时为 nullptr
    [[nodiscard]] static auto primary() -> const info*;
};

} // namespace usip::platform
