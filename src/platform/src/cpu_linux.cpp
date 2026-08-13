#include "platform/cpu.hpp"

// cpu_linux.cpp — Linux 实现(/proc/cpuinfo)。注:尚未经实机编译验证。

#include <fstream>
#include <set>
#include <string>
#include <utility>

#include <unistd.h>

namespace usip::platform {
namespace {

    struct cpuinfo_fields {
        std::set<std::pair<int, int>> physical_cores;   // (physical id, core id) 去重
        std::string model_name;
    };

    [[nodiscard]] auto parse_cpuinfo() -> cpuinfo_fields
    {
        cpuinfo_fields out;
        std::ifstream in("/proc/cpuinfo");

        int physical_id = -1;
        int core_id = -1;
        std::string line;
        while (std::getline(in, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            const auto key = line.substr(0, line.find_last_not_of(" \t", colon - 1) + 1);
            const auto value = line.substr(line.find_first_not_of(" \t", colon + 1));

            if (key == "processor") {
                if (physical_id >= 0 && core_id >= 0)
                    out.physical_cores.emplace(physical_id, core_id);
                physical_id = -1;
                core_id = -1;
            } else if (key == "physical id") {
                physical_id = std::atoi(value.c_str());
            } else if (key == "core id") {
                core_id = std::atoi(value.c_str());
            } else if (key == "model name" && out.model_name.empty()) {
                out.model_name = value;
            }
        }
        if (physical_id >= 0 && core_id >= 0)
            out.physical_cores.emplace(physical_id, core_id);
        return out;
    }

} // namespace

auto cpu::logical_cores() -> unsigned int
{
    static const auto n = [] {
        const auto c = sysconf(_SC_NPROCESSORS_ONLN);
        return c > 0 ? static_cast<unsigned int>(c) : 1u;
    }();
    return n;
}

auto cpu::physical_cores() -> unsigned int
{
    static const auto n = [] {
        const auto fields = parse_cpuinfo();
        return fields.physical_cores.empty()
            ? logical_cores()
            : static_cast<unsigned int>(fields.physical_cores.size());
    }();
    return n;
}

auto cpu::brand_name() -> const std::string&
{
    static const auto brand = [] {
        auto s = parse_cpuinfo().model_name;
        return s.empty() ? std::string { "Unknown CPU" } : s;
    }();
    return brand;
}

} // namespace usip::platform
