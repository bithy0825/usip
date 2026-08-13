#pragma once

// ==============================================================================
// platform/cpu.hpp — CPU 查询(逻辑/物理核心、型号名)
//
// 全部静态方法;结果进程内缓存(OS 报告值运行期不变)。
// ==============================================================================

#include <string>

namespace usip::platform {

class cpu {
public:
    cpu() = delete;

    [[nodiscard]] static auto logical_cores() -> unsigned int;    // 逻辑核心(含超线程)
    [[nodiscard]] static auto physical_cores() -> unsigned int;   // 物理核心
    [[nodiscard]] static auto brand_name() -> const std::string&; // "AMD Ryzen ..." / "Intel ..."
};

} // namespace usip::platform
