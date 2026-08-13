#include "platform/gpu.hpp"

// ==============================================================================
// gpu_linux.cpp — Linux 实现(sysfs/drm)
//
// 读取 /sys/class/drm/card*/device/uevent 的 PCI_ID;厂商由 PCI vendor id 映射。
// 已知限制(实机可再扩充):
//   * 设备名需要 pci.ids 数据库或 libpci,此处回退为 "PCI 10de:1f82" 形式;
//     GL 渲染器名(更友好)应由 renderer 层在创建上下文后查询
//   * 显存无跨厂商统一 sysfs 接口,置 0
// 注:本文件尚未经实机编译验证。
// ==============================================================================

#include <format>
#include <fstream>
#include <string>

#include <dirent.h>

namespace usip::platform {
namespace {

    [[nodiscard]] auto vendor_name(unsigned int id) -> std::string
    {
        switch (id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        default:     return std::format("0x{:04X}", id);
        }
    }

    [[nodiscard]] auto query_devices() -> std::vector<gpu::info>
    {
        std::vector<gpu::info> out;

        DIR* dir = opendir("/sys/class/drm");
        if (!dir)
            return out;

        while (const auto* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (!name.starts_with("card") || name.find('-') != std::string::npos)
                continue;   // 只要 card0/card1...,跳过 card0-DP-1 之类的连接器节点

            std::ifstream uevent("/sys/class/drm/" + name + "/device/uevent");
            if (!uevent)
                continue;

            unsigned int vendor_id = 0;
            unsigned int device_id = 0;
            std::string line;
            while (std::getline(uevent, line)) {
                if (line.starts_with("PCI_ID=")) {
                    std::sscanf(line.c_str(), "PCI_ID=%x:%x", &vendor_id, &device_id);
                    break;
                }
            }
            if (vendor_id == 0)
                continue;

            out.push_back(gpu::info {
                .vendor = vendor_name(vendor_id),
                .device_name = std::format("PCI {:04x}:{:04x}", vendor_id, device_id),
                .dedicated_vram_bytes = 0,
            });
        }
        closedir(dir);
        return out;
    }

} // namespace

auto gpu::devices() -> const std::vector<info>&
{
    static const auto list = query_devices();
    return list;
}

auto gpu::primary() -> const info*
{
    const auto& list = devices();
    return list.empty() ? nullptr : &list.front();
}

} // namespace usip::platform
