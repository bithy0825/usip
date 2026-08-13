#include "icon_registry.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRectF>
#include <QSvgRenderer>

#include <ranges>
#include <utility>

namespace usip::ui {

// ─── 构造 / 单例 ───────────────────────────────────────────────────────────

icon_registry::icon_registry() = default;

auto icon_registry::instance() -> icon_registry&
{
    static icon_registry inst;
    return inst;
}

// ─── 初始化 ───────────────────────────────────────────────────────────────

auto icon_registry::scan(std::string_view dir) -> result<void>
{
    const auto qt_dir = QString::fromUtf8(dir.data(),
        static_cast<int>(dir.size()));

    if (!QDir(qt_dir).exists()) {
        return common::fail(common::errc::not_found,
            "scan: directory '{}' does not exist "
            "(check QRC compilation)", std::string { dir });
    }

    QDirIterator it(qt_dir, { "*.svg" }, QDir::Files,
        QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const auto path = it.next();
        const auto stem = QFileInfo(path).completeBaseName().toStdString();

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return common::fail(common::errc::io,
                "scan: cannot open '{}'", file.fileName().toStdString());
        }
        auto data = file.readAll();
        if (data.isEmpty()) {
            return common::fail(common::errc::io,
                "scan: '{}' is empty", stem);
        }

        icons_.insert_or_assign(stem,
            icon_entry { .original = std::move(data) });
        icon_cache_.erase(stem);
    }

    return {};
}

auto icon_registry::load(std::string_view name) -> result<void>
{
    auto path = QStringLiteral("%1%2%3")
        .arg(QString::fromLatin1(k_resource_prefix))
        .arg(QString::fromUtf8(name.data(),
            static_cast<int>(name.size())))
        .arg(QString::fromLatin1(k_svg_ext));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return common::fail(common::errc::not_found,
            "load: resource '{}' not found", std::string { name });
    }

    auto data = file.readAll();
    if (data.isEmpty()) {
        return common::fail(common::errc::io,
            "load: '{}' is empty", std::string { name });
    }

    return load_data(name, std::move(data));
}

auto icon_registry::load_data(std::string_view name, QByteArray svg) -> result<void>
{
    if (name.empty() || svg.isEmpty()) {
        return common::fail(common::errc::invalid_argument,
            "load_data: name or svg is empty");
    }

    auto key = std::string { name };
    icons_.insert_or_assign(key,
        icon_entry { .original = std::move(svg) });
    icon_cache_.erase(key);
    return {};
}

// ─── 主题 / 配色 ──────────────────────────────────────────────────────────

auto icon_registry::register_scheme(std::string_view name,
    std::span<const color_rule> rules) -> void
{
    schemes_.insert_or_assign(std::string { name },
        std::vector<color_rule> { rules.begin(), rules.end() });
}

auto icon_registry::apply_scheme(std::string_view name) -> result<void>
{
    if (!schemes_.contains(name)) {
        return common::fail(common::errc::not_found,
            "apply_scheme: '{}' not registered", std::string { name });
    }

    if (name == current_scheme_) {
        return {};
    }

    current_scheme_ = name;
    invalidate_all();
    return {};
}

auto icon_registry::recolor(std::string_view name,
    std::span<const color_rule> rules) -> result<void>
{
    auto it = icons_.find(name);
    if (it == icons_.end()) {
        return common::fail(common::errc::not_found,
            "recolor: '{}' not registered", std::string { name });
    }

    auto& entry = it->second;
    entry.themed = apply_rules(entry.original, rules);
    entry.themed_dirty = false;
    entry.pixmaps.clear();
    icon_cache_.erase(name);
    return {};
}

auto icon_registry::current_scheme() const noexcept -> std::string_view
{
    return current_scheme_;
}

auto icon_registry::has_scheme(std::string_view name) const noexcept -> bool
{
    return schemes_.contains(name);
}

auto icon_registry::scheme_names() const -> std::vector<std::string>
{
    auto keys = std::views::keys(schemes_);
    return { keys.begin(), keys.end() };
}

// ─── 查询 / 渲染 ──────────────────────────────────────────────────────────

auto icon_registry::icon(std::string_view name) const -> result<QIcon>
{
    if (const auto cached = icon_cache_.find(name);
        cached != icon_cache_.end()) {
        return cached->second;
    }

    auto it = icons_.find(name);
    if (it == icons_.end()) {
        return common::fail(common::errc::not_found,
            "icon '{}' not registered", std::string { name });
    }

    auto& entry = it->second;
    ensure_themed(entry);

    QIcon icon;
    for (auto size : default_sizes_) {
        for (auto dpr : { 1.0, 2.0 }) {
            render_key key { size, dpr };

            if (const auto pit = entry.pixmaps.find(key);
                pit != entry.pixmaps.end()) {
                icon.addPixmap(pit->second);
                continue;
            }

            auto pm = render(entry.themed, size, dpr);
            if (!pm.isNull()) {
                entry.pixmaps.emplace(key, pm);
                icon.addPixmap(std::move(pm));
            }
        }
    }

    icon_cache_.emplace(std::string { name }, icon);
    return icon;
}

auto icon_registry::pixmap(std::string_view name,
    int size, qreal dpr) const -> result<QPixmap>
{
    auto it = icons_.find(name);
    if (it == icons_.end()) {
        return common::fail(common::errc::not_found,
            "icon '{}' not registered", std::string { name });
    }

    auto& entry = it->second;
    ensure_themed(entry);

    render_key key { size, dpr };
    if (const auto pit = entry.pixmaps.find(key);
        pit != entry.pixmaps.end()) {
        return pit->second;
    }

    auto pm = render(entry.themed, size, dpr);
    if (pm.isNull()) {
        return common::fail(common::errc::external,
            "failed to render '{}'", std::string { name });
    }

    entry.pixmaps.emplace(key, pm);
    return pm;
}

auto icon_registry::contains(std::string_view name) const noexcept -> bool
{
    return icons_.contains(name);
}

auto icon_registry::names() const -> std::vector<std::string>
{
    auto keys = std::views::keys(icons_);
    return { keys.begin(), keys.end() };
}

auto icon_registry::count() const noexcept -> std::size_t
{
    return icons_.size();
}

auto icon_registry::clear() noexcept -> void
{
    icons_.clear();
    icon_cache_.clear();
}

// ─── 配置 ──────────────────────────────────────────────────────────────────

auto icon_registry::set_default_sizes(
    std::span<const int> sizes) noexcept -> void
{
    default_sizes_.assign(sizes.begin(), sizes.end());
    icon_cache_.clear();
}

auto icon_registry::default_sizes() const noexcept -> std::span<const int>
{
    return default_sizes_;
}

// ─── 主题变更通知 ──────────────────────────────────────────────────────────

auto icon_registry::on_theme_changed(theme_changed_fn cb) -> void
{
    callbacks_.push_back(std::move(cb));
}

// ─── 内部实现 ──────────────────────────────────────────────────────────────

auto icon_registry::current_rules() const -> std::span<const color_rule>
{
    if (current_scheme_.empty()) {
        return {};
    }
    if (const auto it = schemes_.find(current_scheme_);
        it != schemes_.end()) {
        return it->second;
    }
    return {};
}

auto icon_registry::apply_rules(const QByteArray& svg,
    std::span<const color_rule> rules) -> QByteArray
{
    if (rules.empty()) {
        return svg;
    }

    QByteArray result = svg;
    for (const auto& [from, to] : rules) {
        result.replace(
            QByteArray { from.data(), static_cast<int>(from.size()) },
            QByteArray { to.data(), static_cast<int>(to.size()) });
    }
    return result;
}

auto icon_registry::ensure_themed(icon_entry& entry) const -> void
{
    if (!entry.themed_dirty) {
        return;
    }

    const auto rules = current_rules();
    entry.themed = rules.empty()
        ? entry.original
        : apply_rules(entry.original, rules);
    entry.themed_dirty = false;
    entry.pixmaps.clear();
}

auto icon_registry::render(const QByteArray& svg,
    int size, qreal dpr) -> QPixmap
{
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }

    const auto px_size = static_cast<int>(size * dpr);
    QPixmap pixmap(px_size, px_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter,
        QRectF { 0, 0, static_cast<qreal>(px_size),
                    static_cast<qreal>(px_size) });
    painter.end();

    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

auto icon_registry::invalidate_all() -> void
{
    for (auto&& [_, entry] : icons_) {
        entry.themed_dirty = true;
        entry.pixmaps.clear();
    }
    icon_cache_.clear();

    for (const auto& cb : callbacks_) {
        cb();
    }
}

} // namespace usip::ui
