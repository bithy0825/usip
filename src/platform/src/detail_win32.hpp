#pragma once

// ==============================================================================
// detail_win32.hpp — platform 模块 Win32 内部工具(不对外)
//   UTF-8 ↔ UTF-16 转换、COM 指针守卫、COM 初始化守卫
// ==============================================================================

#include <string>
#include <string_view>

#include <objbase.h>
#include <windows.h>

namespace usip::platform::detail {

[[nodiscard]] inline auto to_wide(std::string_view s) -> std::wstring
{
    if (s.empty())
        return { };
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
        nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

[[nodiscard]] inline auto from_wide(std::wstring_view s) -> std::string
{
    if (s.empty())
        return { };
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n,
        nullptr, nullptr);
    return out;
}

// 最小 COM 智能指针(仅本模块内部使用)
template <typename T>
struct com_ptr {
    T* p = nullptr;

    com_ptr() = default;
    ~com_ptr()
    {
        if (p)
            p->Release();
    }

    com_ptr(const com_ptr&) = delete;
    auto operator=(const com_ptr&) -> com_ptr& = delete;

    [[nodiscard]] auto get() const noexcept -> T* { return p; }
    [[nodiscard]] auto operator->() const noexcept -> T* { return p; }
    [[nodiscard]] auto put() noexcept -> T** { return &p; }
    [[nodiscard]] explicit operator bool() const noexcept { return p != nullptr; }
};

// COM 单元初始化守卫(STA);已初始化(S_FALSE)同样配对 CoUninitialize
struct com_init {
    HRESULT hr;

    com_init() noexcept
        : hr { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) }
    {
    }
    ~com_init()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }

    [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(hr); }
};

} // namespace usip::platform::detail
