#include "platform/system.hpp"

#include <format>

#include "detail_win32.hpp"   // windows.h + objbase.h 必须先于 shell 系列头(EXTERN_C/GUID 上下文)

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace usip::platform {
namespace {

// ─── 路径查询(一次性,调用点缓存)────────────────────────────────────────────

[[nodiscard]] auto query_executable_path() -> std::filesystem::path
{
    std::vector<wchar_t> buf(256);
    while (true) {
        const auto n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return { };   // 极端失败:返回空,调用方自行决定回退
        if (n < buf.size())
            return std::filesystem::path { buf.data(), buf.data() + n };
        buf.resize(buf.size() * 2);   // 截断 → 扩容重试
    }
}

[[nodiscard]] auto query_app_data_dir() -> std::filesystem::path
{
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr,
            &known))) {
        std::filesystem::path base { known };
        CoTaskMemFree(known);
        return base / "usip";
    }
    return std::filesystem::temp_directory_path() / "usip";   // 极端回退
}

[[nodiscard]] auto query_os_name() -> std::string
{
    // GetVersionEx 已废弃且受清单欺骗;走 ntdll 的 RtlGetVersion(业界标准做法)
    struct os_version_info {   // RTL_OSVERSIONINFOW 布局
        ULONG size;
        ULONG major;
        ULONG minor;
        ULONG build;
        ULONG platform;
        wchar_t csd[128];
    };
    using rtl_get_version_t = LONG(WINAPI*)(os_version_info*);

    if (const auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto fn = reinterpret_cast<rtl_get_version_t>(
                GetProcAddress(ntdll, "RtlGetVersion"))) {
            os_version_info vi { };
            vi.size = sizeof(vi);
            if (fn(&vi) == 0)
                return std::format("Windows {}.{}.{}", vi.major, vi.minor, vi.build);
        }
    }
    return "Windows";
}

// ─── 文件对话框公共设置(keep/specs 由调用方持有,必须活过 Show)──────────────

void apply_dialog_options(IFileDialog* dlg, const file_dialog_desc& desc, DWORD options,
    std::vector<std::pair<std::wstring, std::wstring>>& keep,
    std::vector<COMDLG_FILTERSPEC>& specs)
{
    dlg->SetOptions(options);

    if (!desc.title.empty())
        dlg->SetTitle(detail::to_wide(desc.title).c_str());

    if (!desc.filters.empty()) {
        for (const auto& f : desc.filters) {
            auto& [name, pattern] = keep.emplace_back(detail::to_wide(f.description),
                detail::to_wide(f.pattern));
            specs.push_back(COMDLG_FILTERSPEC { name.c_str(), pattern.c_str() });
        }
        dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }

    if (!desc.initial_path.empty()) {
        detail::com_ptr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(desc.initial_path.c_str(), nullptr,
                IID_PPV_ARGS(folder.put()))))
            dlg->SetDefaultFolder(folder.get());
    }
    if (!desc.initial_filename.empty())
        dlg->SetFileName(detail::to_wide(desc.initial_filename).c_str());
    if (!desc.initial_extension.empty())
        dlg->SetDefaultExtension(detail::to_wide(desc.initial_extension).c_str());
}

[[nodiscard]] auto shell_item_path(IShellItem* item) -> std::filesystem::path
{
    PWSTR p = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)))
        return { };
    std::filesystem::path out { p };
    CoTaskMemFree(p);
    return out;
}

[[nodiscard]] auto dialog_flags(dialog_type type, dialog_option options) -> DWORD
{
    DWORD flags = FOS_FORCEFILESYSTEM;
    if (has_option(options, dialog_option::path_must_exist))
        flags |= FOS_PATHMUSTEXIST;
    if (has_option(options, dialog_option::file_must_exist))
        flags |= FOS_FILEMUSTEXIST;
    if (has_option(options, dialog_option::overwrite_prompt))
        flags |= FOS_OVERWRITEPROMPT;
    if (type == dialog_type::open_file && has_option(options, dialog_option::multi_select))
        flags |= FOS_ALLOWMULTISELECT;
    if (type == dialog_type::select_folder)
        flags |= FOS_PICKFOLDERS;
    return flags;
}

} // namespace

// ─── 路径 ────────────────────────────────────────────────────────────────────

auto system::executable_path() -> const std::filesystem::path&
{
    static const auto path = query_executable_path();
    return path;
}

auto system::executable_dir() -> const std::filesystem::path&
{
    static const auto dir = executable_path().parent_path();
    return dir;
}

auto system::app_data_dir() -> const std::filesystem::path&
{
    static const auto dir = [] {
        auto d = query_app_data_dir();
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }();
    return dir;
}

// ─── 系统 ────────────────────────────────────────────────────────────────────

auto system::os_name() -> const std::string&
{
    static const auto name = query_os_name();
    return name;
}

auto system::total_ram_bytes() -> std::uint64_t
{
    MEMORYSTATUSEX ms { .dwLength = sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms))
        return static_cast<std::uint64_t>(ms.ullTotalPhys);
    return 0;
}

// ─── 独显优先 ────────────────────────────────────────────────────────────────
//
// 导出符号必须存在于最终 exe 中:本函数与导出符号同处一个 TU,
// main() 调用本函数 → 链接器从静态库拉入该 TU → 导出符号随之进入 exe。
// 若把导出符号单独放在一个 TU,静态库下链接器不会拉取,机制静默失效。

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;                      // NVIDIA
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;       // AMD
}

void system::prefer_discrete_gpu() noexcept
{
    // Windows:导出符号即全部机制,驱动在进程加载时读取
}

// ─── 消息框 ──────────────────────────────────────────────────────────────────

void system::message_box(message_kind kind, std::string_view title, std::string_view message,
    window_handle parent) noexcept
{
    try {
        const UINT icon = kind == message_kind::error    ? MB_ICONERROR
            : kind == message_kind::warning              ? MB_ICONWARNING
                                                         : MB_ICONINFORMATION;
        MessageBoxW(static_cast<HWND>(parent), detail::to_wide(message).c_str(),
            detail::to_wide(title).c_str(), icon | MB_SETFOREGROUND | MB_TASKMODAL);
    } catch (...) {
        // 错误上报路径不容再抛
    }
}

// ─── 文件对话框(COM Common Item Dialog)──────────────────────────────────────

auto system::show_file_dialog(dialog_type type, const file_dialog_desc& desc)
    -> result<std::vector<std::filesystem::path>>
{
    detail::com_init com;
    if (!com.ok())
        return common::fail(common::errc::external, "CoInitializeEx failed: 0x{:08X}",
            static_cast<std::uint32_t>(com.hr));

    const DWORD flags = dialog_flags(type, desc.options);

    // SetFileTypes 的字符串指针是借用语义,必须活过 Show()
    std::vector<std::pair<std::wstring, std::wstring>> keep;
    std::vector<COMDLG_FILTERSPEC> specs;
    keep.reserve(desc.filters.size());
    specs.reserve(desc.filters.size());

    if (type == dialog_type::save_file) {
        detail::com_ptr<IFileSaveDialog> dlg;
        HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(dlg.put()));
        if (FAILED(hr))
            return common::fail(common::errc::external,
                "CoCreateInstance(FileSaveDialog) failed: 0x{:08X}",
                static_cast<std::uint32_t>(hr));

        apply_dialog_options(dlg.get(), desc, flags, keep, specs);

        hr = dlg->Show(static_cast<HWND>(desc.parent));
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            return common::fail(common::errc::cancelled, "file dialog cancelled by user");
        if (FAILED(hr))
            return common::fail(common::errc::external, "IFileSaveDialog::Show failed: 0x{:08X}",
                static_cast<std::uint32_t>(hr));

        detail::com_ptr<IShellItem> item;
        if (FAILED(dlg->GetResult(item.put())))
            return common::fail(common::errc::external, "IFileSaveDialog::GetResult failed");

        std::vector<std::filesystem::path> out;
        if (auto p = shell_item_path(item.get()); !p.empty())
            out.push_back(std::move(p));
        return out;
    }

    // open_file / select_folder 共用 FileOpenDialog(PICKFOLDERS 区分)
    detail::com_ptr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(dlg.put()));
    if (FAILED(hr))
        return common::fail(common::errc::external,
            "CoCreateInstance(FileOpenDialog) failed: 0x{:08X}",
            static_cast<std::uint32_t>(hr));

    apply_dialog_options(dlg.get(), desc, flags, keep, specs);

    hr = dlg->Show(static_cast<HWND>(desc.parent));
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return common::fail(common::errc::cancelled, "file dialog cancelled by user");
    if (FAILED(hr))
        return common::fail(common::errc::external, "IFileOpenDialog::Show failed: 0x{:08X}",
            static_cast<std::uint32_t>(hr));

    detail::com_ptr<IShellItemArray> results;
    if (FAILED(dlg->GetResults(results.put())))
        return common::fail(common::errc::external, "IFileOpenDialog::GetResults failed");

    DWORD count = 0;
    results->GetCount(&count);

    std::vector<std::filesystem::path> out;
    out.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        detail::com_ptr<IShellItem> item;
        if (FAILED(results->GetItemAt(i, item.put())))
            continue;
        if (auto p = shell_item_path(item.get()); !p.empty())
            out.push_back(std::move(p));
    }
    return out;
}

// ─── 文件管理器 ──────────────────────────────────────────────────────────────

auto system::reveal_in_file_manager(const std::filesystem::path& path) -> result<>
{
    std::error_code ec;
    const bool is_dir = std::filesystem::is_directory(path, ec);

    const std::wstring params = is_dir
        ? std::wstring { L"\"" } + path.c_str() + L"\""
        : std::wstring { L"/select,\"" } + path.c_str() + L"\"";

    const auto rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr,
            SW_SHOWNORMAL));
    if (rc <= 32)   // ShellExecute 返回值 ≤32 为失败
        return common::fail(common::errc::external, "ShellExecuteW explorer.exe failed ({})",
            rc);
    return { };
}

} // namespace usip::platform
