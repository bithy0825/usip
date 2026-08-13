#include "platform/cpu.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

#include <intrin.h>
#include <windows.h>

namespace usip::platform {

auto cpu::logical_cores() -> unsigned int
{
    static const auto n = [] {
        SYSTEM_INFO si { };
        GetSystemInfo(&si);
        return static_cast<unsigned int>(si.dwNumberOfProcessors);
    }();
    return n;
}

auto cpu::physical_cores() -> unsigned int
{
    static const auto n = [] {
        DWORD len = 0;
        GetLogicalProcessorInformation(nullptr, &len);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0)
            return logical_cores();

        std::vector<std::byte> buf(len);
        auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buf.data());
        if (!GetLogicalProcessorInformation(first, &len))
            return logical_cores();

        // 每个 RelationProcessorCore 条目对应一个物理核心
        unsigned int count = 0;
        const std::size_t entries = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        for (std::size_t i = 0; i < entries; ++i)
            if (first[i].Relationship == RelationProcessorCore)
                ++count;
        return count == 0 ? logical_cores() : count;
    }();
    return n;
}

auto cpu::brand_name() -> const std::string&
{
    static const std::string brand = [] {
        // CPUID 扩展叶 0x80000002~04 给出 48 字节型号串
        int regs[4] = { };
        char text[49] = { };

        __cpuid(regs, 0x80000000);
        if (static_cast<unsigned int>(regs[0]) < 0x80000004)
            return std::string { "Unknown CPU" };

        for (unsigned int leaf = 0x80000002; leaf <= 0x80000004; ++leaf) {
            __cpuid(regs, static_cast<int>(leaf));
            std::memcpy(text + (leaf - 0x80000002) * 16, regs, 16);
        }

        std::string s { text };
        // 厂商填充的首尾空格修掉
        const auto first = s.find_first_not_of(' ');
        const auto last = s.find_last_not_of(' ');
        return first == std::string::npos ? std::string { "Unknown CPU" }
                                          : s.substr(first, last - first + 1);
    }();
    return brand;
}

} // namespace usip::platform
