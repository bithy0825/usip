#include "config.hpp"

#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

namespace usip::core {
namespace detail {

    // 值节点 → TOML 字面量文本(字符串转义等交由 toml++ 序列化器;
    // 禁用 literal string,统一输出双引号基本字符串,符合配置文件惯例)
    [[nodiscard]] auto format_node(const toml::node& node) -> std::string
    {
        std::ostringstream oss;
        oss << toml::toml_formatter { node,
            toml::toml_formatter::default_flags & ~toml::format_flags::allow_literal_strings };
        return oss.str();
    }

    // 收集未注册叶子键,按"所属表路径"分桶为 "leaf = value" 文本行。
    // 未注册键按其表路径并入对应小节;落在已注册表内的同节输出,落在未注册表内的自成新节。
    // (不可用根级点分键:dotted key 会"定义"其父表,与后续 [table] 段冲突,非法 TOML)
    void collect_unknown_leaves(const toml::table& tbl, std::string& prefix,
        const settings_registry& reg, std::map<std::string, std::string, std::less<>>& out)
    {
        for (const auto& [key, node] : tbl) {
            const auto parent_size = prefix.size();
            if (!prefix.empty())
                prefix += '.';
            prefix += key.str();

            if (node.is_table()) {
                collect_unknown_leaves(*node.as_table(), prefix, reg, out);
            } else if (!reg.find(prefix)) {
                // 值遮蔽整张已注册表(如 "opengl = 5")会令生成文件非法 → 丢弃
                if (!reg.any_key_under(prefix)) {
                    const auto dot = prefix.rfind('.');
                    auto& bucket = dot == std::string::npos
                        ? out[std::string { }] // 根级叶子:无表前缀
                        : out[prefix.substr(0, dot)]; // 所属表路径
                    bucket += std::format("{} = {}\n", key.str(), format_node(node));
                }
            }
            prefix.resize(parent_size);
        }
    }

} // namespace detail

// ─── 全局 facade ─────────────────────────────────────────────────────────────

namespace {
    std::atomic<config*> g_config = nullptr;
}

auto config::global() noexcept -> config*
{
    return g_config.load();
}

void config::set_global(config& cfg) noexcept
{
    g_config.store(&cfg);
}

// ─── 构造 / 析构 ─────────────────────────────────────────────────────────────

config::config(settings_registry reg)
    : reg_(std::move(reg))
{
    current_.store(std::make_shared<toml::table>());
}

config::~config() = default;

// ─── 生命周期 ────────────────────────────────────────────────────────────────

auto config::load(const std::filesystem::path& path) -> result<>
{
    path_ = path;

    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        // 首启:生成带注释的默认配置(DOM 为空 → 全部取注册表默认值)
        return save();
    }

    if (auto r = reload(); !r) {
        // 损坏文件 fallback:备份 .bak,重建默认
        warnings_.push_back(common::error::make(common::errc::parse,
            "config file corrupted ({}); backed up to .bak and rebuilt with defaults",
            r.error().message()));

        auto bak = path_;
        bak += ".bak";
        std::filesystem::remove(bak, ec);
        std::filesystem::rename(path_, bak, ec);

        current_.store(std::make_shared<toml::table>());
        return save();
    }
    return { };
}

auto config::reload() -> result<>
{
    warnings_.clear();

    std::ifstream ifs(path_, std::ios::binary);
    if (!ifs)
        return common::fail(common::errc::not_found, "config file not found: {}",
            path_.string());

    const std::string text { std::istreambuf_iterator<char>(ifs),
        std::istreambuf_iterator<char> { } };

    // toml++ 默认启用异常:parse 直接返回 table,失败抛 toml::parse_error
    toml::table parsed;
    try {
        parsed = toml::parse(text, path_.string());
    } catch (const toml::parse_error& e) {
        return common::fail(common::errc::parse, "config parse failed: {}",
            e.description());
    }

    auto next = std::make_shared<toml::table>(std::move(parsed));

    // 键级违规:警告 + 从 DOM 移除(读时回落默认值),不否决整个文件
    for (const auto& m : reg_.metas()) {
        const auto* node = detail::find_node(*next, m.path);
        if (!node)
            continue;

        if (node->type() != m.default_value->type()) {
            warnings_.push_back(common::error::make(common::errc::type_mismatch,
                "setting {} has a wrong type; ignored (default restored)", m.path));
            detail::erase_path(*next, m.path);
            continue;
        }
        if (auto r = m.validate(*node); !r) {
            warnings_.push_back(std::move(r).error());
            detail::erase_path(*next, m.path);
        }
    }

    current_.store(std::move(next)); // 解析全部通过才切换(validate-then-apply)
    dirty_ = false;
    return { };
}

auto config::save() -> result<>
{
    const auto snap = current_.load();
    const auto text = generate_file(*snap);

    auto tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs)
            return common::fail(common::errc::io, "cannot write temp file: {}",
                tmp.string());
        ofs << text;
        if (!ofs)
            return common::fail(common::errc::io, "write failed: {}", tmp.string());
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec); // MSVC STL:替换语义,原子
    if (ec)
        return common::fail(common::errc::io, "rename failed: {} -> {}: {}",
            tmp.string(), path_.string(), ec.message());

    dirty_ = false;
    return { };
}

// ─── 文件生成(注册表驱动:描述 → 注释;未注册键原样保留)──────────────────────

auto config::generate_file(const toml::table& dom) const -> std::string
{
    std::string out = "# usip configuration file\n"
                      "# unrecognized keys are preserved as-is; invalid values fall back to defaults on load\n";

    // 未注册叶子键按"所属表路径"分桶
    std::map<std::string, std::string, std::less<>> unknown;
    {
        std::string prefix;
        detail::collect_unknown_leaves(dom, prefix, reg_, unknown);
    }

    // 根级未注册键必须位于任何 [table] 段之前
    if (const auto it = unknown.find(std::string { }); it != unknown.end()) {
        out += "\n# --- unregistered keys, preserved as-is ---\n" + it->second;
        unknown.erase(it);
    }

    const auto flush_unknown = [&](std::string_view table_path) {
        if (const auto it = unknown.find(table_path); it != unknown.end()) {
            out += it->second;
            unknown.erase(it);
        }
    };

    std::string_view current_table;
    for (const auto& m : reg_.metas()) {
        const auto dot = m.path.find_last_of('.');
        const auto table_path = m.path.substr(0, dot);
        const auto leaf = m.path.substr(dot + 1);

        if (table_path != current_table) {
            flush_unknown(current_table); // 上一节收尾时并入该节的未注册键
            current_table = table_path;
            out += std::format("\n[{}]\n", table_path);
        }

        const auto* node = detail::find_node(dom, m.path);
        const toml::node& value = node ? *node : *m.default_value;

        out += std::format("# {}\n", m.description);
        out += std::format("{} = {}\n", leaf, detail::format_node(value));
    }
    flush_unknown(current_table);

    // 落在完全未注册表中的键:各自成节,附在末尾
    for (const auto& [table_path, text] : unknown)
        out += std::format("\n# --- unregistered keys, preserved as-is ---\n[{}]\n{}",
            table_path, text);

    return out;
}

// ─── 内建设置声明(唯一维护清单)──────────────────────────────────────────────
//
// 键设计参考成熟软件惯例:
//   log.*      spdlog/log4j:原生级别名;pattern 模式串;人类可读单位(MB)
//   opengl.*   渲染上下文参数(创建后不可变 → 全部 restart_required)
//   executor.* Blender 的 Auto/Fixed 思想:thread_count = 0 即用满全部核心,
//              取代旧版 enable_all_cores + thread_count 冗余双键

void register_builtin_settings(settings_registry& reg)
{
    // ── 日志(立即生效)────────────────────────────────────────────
    reg.add<std::string>("log.level", "info", "log level")
        .one_of({ "trace", "debug", "info", "warn", "err", "critical", "off" });
    reg.add<std::string>("log.pattern", "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v",
        "log output pattern (spdlog pattern syntax)");
    reg.add<bool>("log.to_console", true, "enable console log output");
    reg.add<bool>("log.to_file", true, "enable file log output");
    reg.add<std::string>("log.file_path", "logs/usip.log", "log file path");
    reg.add<int>("log.max_file_size_mb", 10, "max size of a single log file (MB)")
        .range(1, 1024);
    reg.add<int>("log.max_files", 10, "number of rotated log files to keep")
        .range(1, 100);

    // ── OpenGL(需重启)────────────────────────────────────────────
    reg.add<int>("opengl.samples", 8, "MSAA sample count (0 = disabled)")
        .range(0, 16)
        .restart_required();
    reg.add<bool>("opengl.vsync", true, "vertical sync")
        .restart_required();
    reg.add<bool>("opengl.srgb", false, "sRGB framebuffer")
        .restart_required();
    reg.add<bool>("opengl.debug_context", false, "OpenGL debug context (GL_KHR_debug)")
        .restart_required();

    // ── 执行器(需重启)────────────────────────────────────────────
    // 默认 2 条 worker 线程
    // 0 = 自动用满全部核心(计算密集场景再开)
    reg.add<int>("executor.thread_count", 2,
           "worker thread count (0 = auto, use all logical cores)")
        .range(0, 256)
        .restart_required();

    // ── 文件(立即生效)────────────────────────────────────────────
    reg.add<std::vector<std::string>>("file.recent_files", { },
        "recently opened file paths (UTF-8)");
    reg.add<int>("file.max_recent_files", 10,
           "maximum number of recent file entries to keep")
        .range(1, 100);

    // ── 渲染(立即生效,canvas 每帧快照读取)──────────────────────────
    reg.add<std::string>("pseudocolor.colormap", "jet",
        "pseudocolor colormap: jet | parula | viridis | plasma");
    reg.add<bool>("pseudocolor.zero_is_black", true,
        "pin domain-zero (gray 0 / zero diff) to pure black");

    reg.add<std::string>("mask.color", "#FF0000", "mask color");
    reg.add<float>("mask.opacity", 0.5F, "mask opacity");

    reg.add<int>("measure.line_width", 2, "measurement line width")
        .range(1, 10);
    reg.add<std::string>("measure.line_color", "#00FF00", "measurement line color");
}

} // namespace usip::core
