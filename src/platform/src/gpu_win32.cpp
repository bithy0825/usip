#include "platform/gpu.hpp"

#include <algorithm>
#include <format>

#include <dxgi.h>

#include "detail_win32.hpp"

namespace usip::platform {
namespace {

    [[nodiscard]] auto vendor_name(std::uint32_t id) -> std::string
    {
        switch (id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        default:     return std::format("0x{:04X}", id);
        }
    }

    [[nodiscard]] auto query_devices() -> std::vector<gpu::info>
    {
        std::vector<gpu::info> out;

        detail::com_ptr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put()))))
            return out;

        for (UINT i = 0;; ++i) {
            detail::com_ptr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_ADAPTER_DESC1 desc { };
            if (FAILED(adapter->GetDesc1(&desc)))
                continue;
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)   // 剔除 WARP 等纯软件适配器
                continue;

            out.push_back(gpu::info {
                .vendor = vendor_name(desc.VendorId),
                .device_name = detail::from_wide(desc.Description),
                .dedicated_vram_bytes = static_cast<std::uint64_t>(desc.DedicatedVideoMemory),
            });
        }

        // 独显显存降序:双显卡笔记本上独显在前
        std::ranges::stable_sort(out, [](const gpu::info& a, const gpu::info& b) {
            return a.dedicated_vram_bytes > b.dedicated_vram_bytes;
        });
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
