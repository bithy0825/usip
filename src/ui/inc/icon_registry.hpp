#pragma once

#include "error.hpp" // usip::common::result / errc / fail

#include <QByteArray>
#include <QIcon>
#include <QPixmap>

#include <cstddef>
#include <flat_map>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usip::ui {

// ─── 配色规则 ─────────────────────────────────────────────────────────────
// 单条颜色替换:SVG 中的源色 → 目标色
// 基于 QByteArray::replace,适用于 hex 色值(如 "#e5e5e5")
//
// 当前 SVG 可见色(由 Inkscape 导出):
//   #e5e5e5  形状填充(light gray fill)
//   #4d4d4d  描边与点标记(dark gray stroke / dot fill)
//   #000333  pseudocolor 图标专用
//   #000000  about 图标专用
struct color_rule {
    std::string_view from; // SVG 中的原始色 "#e5e5e5"
    std::string_view to;   // 替换目标色   "#3d3d40"
};

// ─── 渲染缓存键 ───────────────────────────────────────────────────────────
// flat_map 有序键,(size, dpr) 唯一标识一份渲染结果
struct render_key {
    int size {};
    qreal dpr {};

    auto operator<=>(const render_key&) const = default;
};

// ─── 图标注册表(单例) ────────────────────────────────────────────────────
//
// 职责:SVG 资源自动发现 → 主题着色 → 按需渲染 → 多级缓存
//
// 渲染流水线:
//   original ──(apply_rules)──▶ themed ──(QSvgRenderer)──▶ QPixmap ──(×N)──▶ QIcon
//   不可变       主题切换重算      缓存命中跳过              缓存命中跳过
//
// 缓存失效:
//   apply_scheme()    → themed 全部 dirty,pixmap/icon 缓存全部清空
//   recolor(name)     → 该条 themed 重算,pixmap/icon 该条清空
//   set_default_sizes → icon 缓存全部清空(pixmap 不受影响)
class icon_registry {
public:
    icon_registry(const icon_registry&) = delete;
    auto operator=(const icon_registry&) -> icon_registry& = delete;
    icon_registry(icon_registry&&) = delete;
    auto operator=(icon_registry&&) -> icon_registry& = delete;

    static auto instance() -> icon_registry&;

    // ── 初始化 ────────────────────────────────────────────────────────────

    // 扫描 Qt 资源目录,自动加载所有 *.svg(文件名做 key,幂等)
    auto scan(std::string_view dir = ":/icon") -> result<void>;

    // 显式加载单个图标(:/icon/<name>.svg)
    auto load(std::string_view name) -> result<void>;

    // 从内存数据加载(程序化 SVG 或非资源路径)
    auto load_data(std::string_view name, QByteArray svg) -> result<void>;

    // ── 主题 / 配色 ──────────────────────────────────────────────────────

    // 注册命名配色方案
    //   register_scheme("dark", {
    //       {"#e5e5e5", "#3d3d40"},  // fill 变暗
    //       {"#4d4d4d", "#c0c0c0"},  // stroke 变亮
    //   });
    auto register_scheme(std::string_view name,
        std::span<const color_rule> rules) -> void;

    // 全局切换配色方案:
    //   1. 全部图标 themed_dirty = true
    //   2. pixmap 缓存 + icon 缓存全部清空
    //   3. 同步触发所有 on_theme_changed 回调
    auto apply_scheme(std::string_view name) -> result<void>;

    // 对单个图标应用指定配色(不切换全局主题;下次 apply_scheme 会覆盖)
    auto recolor(std::string_view name,
        std::span<const color_rule> rules) -> result<void>;

    [[nodiscard]] auto current_scheme() const noexcept -> std::string_view;
    [[nodiscard]] auto has_scheme(std::string_view name) const noexcept -> bool;
    [[nodiscard]] auto scheme_names() const -> std::vector<std::string>;

    // ── 查询 / 渲染 ──────────────────────────────────────────────────────

    // 多尺寸 QIcon(default_sizes × {1x, 2x} DPR,懒组装 + 缓存)
    [[nodiscard]] auto icon(std::string_view name) const -> result<QIcon>;

    // 精确尺寸 + DPR 的 QPixmap(懒渲染 + 缓存)
    [[nodiscard]] auto pixmap(std::string_view name, int size,
        qreal dpr = 1.0) const -> result<QPixmap>;

    [[nodiscard]] auto contains(std::string_view name) const noexcept -> bool;
    [[nodiscard]] auto names() const -> std::vector<std::string>;
    [[nodiscard]] auto count() const noexcept -> std::size_t;
    auto clear() noexcept -> void;

    // ── 配置 ──────────────────────────────────────────────────────────────

    auto set_default_sizes(std::span<const int> sizes) noexcept -> void;
    [[nodiscard]] auto default_sizes() const noexcept -> std::span<const int>;

    // ── 主题变更通知 ──────────────────────────────────────────────────────

    // 注册回调;apply_scheme() 后同步触发
    // 典型用途:QAction::setIcon(registry.icon("apply"))
    using theme_changed_fn = std::function<void()>;
    auto on_theme_changed(theme_changed_fn cb) -> void;

private:
    icon_registry();
    ~icon_registry() = default;

    struct icon_entry {
        QByteArray original; // 原始 SVG(磁盘读取后不可变)
        QByteArray themed; // 着色后 SVG(主题切换时 lazy 重算)
        bool themed_dirty { true };
        std::flat_map<render_key, QPixmap> pixmaps; // (size,dpr) → 渲染缓存
    };

    [[nodiscard]] auto current_rules() const -> std::span<const color_rule>;

    [[nodiscard]] static auto apply_rules(const QByteArray& svg,
        std::span<const color_rule> rules) -> QByteArray;

    // 确保 entry.themed 是最新着色结果(themed_dirty 时重算 + 清 pixmap 缓存)
    auto ensure_themed(icon_entry& entry) const -> void;

    [[nodiscard]] static auto render(const QByteArray& svg,
        int size, qreal dpr) -> QPixmap;

    // 全量失效:标记 dirty + 清空 pixmap/icon 缓存 + 触发回调
    auto invalidate_all() -> void;

    static constexpr auto k_resource_prefix { ":/icon/" };
    static constexpr auto k_svg_ext { ".svg" };

    // mutable:icon()/pixmap() 是 const(逻辑不变量),但缓存是实现细节
    mutable std::flat_map<std::string, icon_entry, std::less<>> icons_;
    std::flat_map<std::string, std::vector<color_rule>, std::less<>> schemes_;
    std::string current_scheme_;
    mutable std::flat_map<std::string, QIcon, std::less<>> icon_cache_;
    std::vector<int> default_sizes_ { 16, 24, 32, 48, 64, 128 };
    std::vector<theme_changed_fn> callbacks_;
};

} // namespace usip::ui
