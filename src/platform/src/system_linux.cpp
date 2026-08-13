#include "platform/system.hpp"

// ==============================================================================
// system_linux.cpp — Linux 实现
//
// 对话框后端:zenity(GNOME/GTK 系)优先,kdialog(KDE)兜底,均为子进程调用,
// 不引入 GTK/Qt 链接依赖(与 pfd 同一思路)。注:本文件尚未经实机编译验证。
// ==============================================================================

#include <array>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <system_error>

#include <sys/utsname.h>
#include <unistd.h>

namespace usip::platform {
namespace {

// ─── shell 工具 ──────────────────────────────────────────────────────────────

[[nodiscard]] auto shell_quote(std::string_view s) -> std::string
{
    std::string out { '\'' };
    for (const char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += '\'';
    return out;
}

[[nodiscard]] auto run_capture(const std::string& cmd, std::string& out) -> int
{
    std::array<char, 4096> buf { };
    out.clear();
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return -1;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe))
        out += buf.data();
    return pclose(pipe);
}

enum class dialog_backend { none, zenity, kdialog };

[[nodiscard]] auto backend() -> dialog_backend
{
    static const auto b = [] {
        if (std::system("command -v zenity >/dev/null 2>&1") == 0)
            return dialog_backend::zenity;
        if (std::system("command -v kdialog >/dev/null 2>&1") == 0)
            return dialog_backend::kdialog;
        return dialog_backend::none;
    }();
    return b;
}

// "*.tif;*.tiff" → "*.tif *.tiff"(zenity/kdialog 过滤器为空格分隔)
[[nodiscard]] auto patterns_spaced(std::string_view pattern) -> std::string
{
    std::string out { pattern };
    std::ranges::replace(out, ';', ' ');
    return out;
}

[[nodiscard]] auto trim_newlines(std::string s) -> std::string
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

// ─── 路径查询 ────────────────────────────────────────────────────────────────

[[nodiscard]] auto query_executable_path() -> std::filesystem::path
{
    std::array<char, 4096> buf { };
    const auto n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n <= 0)
        return { };
    return std::filesystem::path { buf.data(), buf.data() + n };
}

[[nodiscard]] auto query_app_data_dir() -> std::filesystem::path
{
    // XDG Base Directory 规范:$XDG_CONFIG_HOME,缺省 ~/.config
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path { xdg } / "usip";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path { home } / ".config" / "usip";
    return std::filesystem::temp_directory_path() / "usip";
}

[[nodiscard]] auto query_os_name() -> std::string
{
    utsname u { };
    if (uname(&u) == 0)
        return std::format("{} {}", u.sysname, u.release);
    return "Linux";
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
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0)
        return 0;
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
}

// ─── 独显优先(PRIME 渲染卸载)───────────────────────────────────────────────

void system::prefer_discrete_gpu() noexcept
{
    // 须在创建任何 GL/Vulkan 上下文前设置;overwrite = 0,不覆盖用户已有设置
    setenv("DRI_PRIME", "1", 0);                     // Mesa(AMD/Intel 混合显卡)
    setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);     // NVIDIA 专有驱动
    setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
}

// ─── 消息框 ──────────────────────────────────────────────────────────────────

void system::message_box(message_kind kind, std::string_view title, std::string_view message,
    window_handle parent) noexcept
{
    (void)parent;
    try {
        std::string cmd;
        if (backend() == dialog_backend::zenity) {
            const char* opt = kind == message_kind::error    ? "--error"
                : kind == message_kind::warning              ? "--warning"
                                                             : "--info";
            cmd = std::format("zenity {} --title={} --text={}", opt, shell_quote(title),
                shell_quote(message));
        } else if (backend() == dialog_backend::kdialog) {
            const char* opt = kind == message_kind::error    ? "--error"
                : kind == message_kind::warning              ? "--sorry"
                                                             : "--msgbox";
            cmd = std::format("kdialog --title {} {} {}", shell_quote(title), opt,
                shell_quote(message));
        }
        if (!cmd.empty()) {
            std::string out;
            run_capture(cmd, out);
            return;
        }
        // 无后端:stderr 兜底
        std::fprintf(stderr, "%.*s: %.*s\n", static_cast<int>(title.size()), title.data(),
            static_cast<int>(message.size()), message.data());
    } catch (...) {
    }
}

// ─── 文件对话框(zenity / kdialog 子进程)─────────────────────────────────────

auto system::show_file_dialog(dialog_type type, const file_dialog_desc& desc)
    -> result<std::vector<std::filesystem::path>>
{
    const auto be = backend();
    if (be == dialog_backend::none)
        return common::fail(common::errc::unavailable,
            "no file dialog backend (install zenity or kdialog)");

    const bool multi = type == dialog_type::open_file
        && has_option(desc.options, dialog_option::multi_select);

    std::string cmd;
    if (be == dialog_backend::zenity) {
        cmd = "zenity --file-selection";
        if (type == dialog_type::save_file) {
            cmd += " --save";
            if (has_option(desc.options, dialog_option::overwrite_prompt))
                cmd += " --confirm-overwrite";
        } else if (type == dialog_type::select_folder) {
            cmd += " --directory";
        }
        if (multi)
            cmd += " --multiple --separator='\\n'";
        if (!desc.title.empty())
            cmd += " --title=" + shell_quote(desc.title);
        if (!desc.initial_path.empty()) {
            auto start = desc.initial_path;
            if (type == dialog_type::save_file && !desc.initial_filename.empty())
                start /= std::string { desc.initial_filename };
            cmd += " --filename=" + shell_quote(start.string());
        }
        for (const auto& f : desc.filters)
            cmd += " --file-filter=" + shell_quote(
                std::string { f.description } + " | " + patterns_spaced(f.pattern));
    } else {
        // kdialog
        std::string start = desc.initial_path.empty()
            ? std::string {}
            : desc.initial_path.string();
        if (type == dialog_type::save_file && !desc.initial_filename.empty())
            start = (desc.initial_path / std::string { desc.initial_filename }).string();

        std::string filter;
        for (const auto& f : desc.filters) {
            if (!filter.empty())
                filter += '\n';
            filter += patterns_spaced(f.pattern) + " |" + std::string { f.description };
        }

        if (type == dialog_type::save_file)
            cmd = "kdialog --getsavefilename " + shell_quote(start);
        else if (type == dialog_type::select_folder)
            cmd = "kdialog --getexistingdirectory " + shell_quote(start);
        else
            cmd = "kdialog --getopenfilename " + shell_quote(start);

        if (multi)
            cmd += " --multiple --separate-output";
        if (!filter.empty())
            cmd += " " + shell_quote(filter);
        if (!desc.title.empty())
            cmd += " --title " + shell_quote(desc.title);
    }

    std::string out;
    const int rc = run_capture(cmd, out);
    if (rc != 0)
        return common::fail(common::errc::cancelled, "file dialog cancelled by user");

    std::vector<std::filesystem::path> paths;
    std::string line;
    std::istringstream lines(out);
    while (std::getline(lines, line)) {
        line = trim_newlines(std::move(line));
        if (!line.empty())
            paths.emplace_back(std::move(line));
    }
    if (paths.empty())
        return common::fail(common::errc::cancelled, "file dialog cancelled by user");
    return paths;
}

// ─── 文件管理器 ──────────────────────────────────────────────────────────────

auto system::reveal_in_file_manager(const std::filesystem::path& path) -> result<>
{
    std::error_code ec;
    const auto target = std::filesystem::is_directory(path, ec) ? path : path.parent_path();

    const auto cmd = "xdg-open " + shell_quote(target.string()) + " >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0)
        return common::fail(common::errc::external, "xdg-open failed");
    return { };
}

} // namespace usip::platform
