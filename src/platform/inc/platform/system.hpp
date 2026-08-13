#pragma once

// ==============================================================================
// platform/system.hpp — 操作系统级服务:路径 / 环境 / 消息框 / 原生文件对话框
//
// 约定:
//   * 全部静态方法(system 为工具类,不可实例化);其他模块不得直接触碰 OS API
//   * 仅支持 Windows / Linux;实现按平台拆分(src/system_{win32,linux}.cpp)
//   * 文件对话框为原生对话框(Windows COM Common Item Dialog;Linux zenity/kdialog),
//     不使用 Qt;用户取消一律返回 errc::cancelled
// ==============================================================================

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "error.hpp"

namespace usip::platform {

// 原生窗口句柄(Windows: HWND;Linux: X11 Window);nullptr = 无属主
using window_handle = void*;

enum class message_kind : std::uint8_t { info, warning, error };

enum class dialog_type : std::uint8_t { open_file, save_file, select_folder };

enum class dialog_option : std::uint32_t {
    none = 0,
    multi_select = 1 << 0,        // 仅 open_file 有效
    path_must_exist = 1 << 1,
    file_must_exist = 1 << 2,     // 仅 open_file 有效
    show_hidden_files = 1 << 3,   // Windows 对话框默认显示隐藏文件,此项主要影响 Linux
    overwrite_prompt = 1 << 4,    // 仅 save_file 有效
};

[[nodiscard]] constexpr auto operator|(dialog_option lhs, dialog_option rhs) noexcept
    -> dialog_option
{
    return static_cast<dialog_option>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

constexpr auto operator|=(dialog_option& lhs, dialog_option rhs) noexcept -> dialog_option&
{
    return lhs = lhs | rhs;
}

[[nodiscard]] constexpr auto operator&(dialog_option lhs, dialog_option rhs) noexcept
    -> dialog_option
{
    return static_cast<dialog_option>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

[[nodiscard]] constexpr auto has_option(dialog_option options, dialog_option flag) noexcept
    -> bool
{
    return (options & flag) != dialog_option::none;
}

struct file_filter {
    std::string_view description;   // "TIFF images"
    std::string_view pattern;       // "*.tif;*.tiff"(分号分隔,同 COMDLG_FILTERSPEC)
};

struct file_dialog_desc {
    std::string_view title;
    std::filesystem::path initial_path { };     // 初始目录(空 = 系统默认)
    std::string_view initial_filename { };      // save_file 的默认文件名
    std::string_view initial_extension { };     // save_file 的默认扩展名(无点,如 "tif")
    std::span<const file_filter> filters { };
    dialog_option options = dialog_option::none;
    window_handle parent = nullptr;
};

class system {
public:
    system() = delete;

    // ── 路径(解析一次并缓存)──────────────────────────────────────────
    [[nodiscard]] static auto executable_path() -> const std::filesystem::path&;
    [[nodiscard]] static auto executable_dir() -> const std::filesystem::path&;
    // %APPDATA%/usip | $XDG_CONFIG_HOME/usip(首次访问自动创建)
    [[nodiscard]] static auto app_data_dir() -> const std::filesystem::path&;
    [[nodiscard]] static auto temp_dir() -> const std::filesystem::path&;

    // ── 系统 ─────────────────────────────────────────────────────────
    [[nodiscard]] static auto os_name() -> const std::string&;
    [[nodiscard]] static auto total_ram_bytes() -> std::uint64_t;
    [[nodiscard]] static auto env(std::string_view name) -> std::optional<std::string>;

    // ── 对话框与 shell 集成 ──────────────────────────────────────────
    // 尽力而为:Linux 无 zenity/kdialog 后端时退化为 stderr 输出
    static void message_box(message_kind kind, std::string_view title, std::string_view message,
        window_handle parent = nullptr) noexcept;

    // 优先使用独立显卡(双显卡机型)。必须在创建任何图形上下文之前调用,
    // 建议作为 main() 的第一行。调用点无需关心平台差异:
    //   Windows:NvOptimus / AMD PowerXpress 导出符号(随本函数所在 TU 被链接进 exe)
    //   Linux:  PRIME 渲染卸载环境变量(DRI_PRIME / __NV_PRIME_RENDER_OFFLOAD,不覆盖用户已有设置)
    static void prefer_discrete_gpu() noexcept;

    // 用户取消 → errc::cancelled;成功返回选中路径列表(单选时 size == 1)
    [[nodiscard]] static auto show_file_dialog(dialog_type type, const file_dialog_desc& desc)
        -> result<std::vector<std::filesystem::path>>;

    // 在文件管理器中显示(导出后打开目录等场景)
    [[nodiscard]] static auto reveal_in_file_manager(const std::filesystem::path& path)
        -> result<>;
};

} // namespace usip::platform
