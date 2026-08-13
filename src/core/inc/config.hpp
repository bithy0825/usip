#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include "error.hpp"
#include "settings.hpp"

namespace usip::core {

namespace detail {

    // 点分路径下降,只读
    [[nodiscard]] inline auto find_node(const toml::table& root, std::string_view path) noexcept
        -> const toml::node*
    {
        const toml::node* cur = &root;
        std::string_view rest = path;

        while (true) {
            const auto dot = rest.find('.');
            const auto seg = rest.substr(0, dot);

            const auto* tbl = cur->as_table();
            if (!tbl)
                return nullptr;
            cur = tbl->get(seg);
            if (!cur)
                return nullptr;
            if (dot == std::string_view::npos)
                return cur;
            rest.remove_prefix(dot + 1);
        }
    }

    // 点分路径下降,按需建表,写入节点(拷贝)
    inline void set_path_node(toml::table& root, std::string_view path, const toml::node& value)
    {
        toml::table* tbl = &root;
        std::string_view rest = path;

        while (true) {
            const auto dot = rest.find('.');
            if (dot == std::string_view::npos) {
                tbl->insert_or_assign(rest, value);
                return;
            }

            const auto seg = rest.substr(0, dot);
            rest.remove_prefix(dot + 1);

            if (auto* node = tbl->get(seg); node && node->is_table()) {
                tbl = node->as_table();
            } else {
                auto [it, inserted] = tbl->insert_or_assign(seg, toml::table { });
                tbl = it->second.as_table();
            }
        }
    }

    // 点分路径删除(清除文件中的非法键用)
    inline void erase_path(toml::table& root, std::string_view path) noexcept
    {
        toml::table* tbl = &root;
        std::string_view rest = path;

        while (true) {
            const auto dot = rest.find('.');
            if (dot == std::string_view::npos) {
                tbl->erase(rest);
                return;
            }

            const auto seg = rest.substr(0, dot);
            rest.remove_prefix(dot + 1);

            auto* node = tbl->get(seg);
            if (!node || !node->is_table())
                return;
            tbl = node->as_table();
        }
    }

} // namespace detail

class config {
public:
    explicit config(settings_registry reg);
    ~config();

    config(const config&) = delete;
    config& operator=(const config&) = delete;

    // ── 生命周期(写线程)──────────────────────────────────────────
    [[nodiscard]] auto load(const std::filesystem::path& path) -> result<>;
    [[nodiscard]] auto reload() -> result<>;
    [[nodiscard]] auto save() -> result<>;

    // ── 读(任意线程,无锁 RCU 快照)────────────────────────────────
    // 取值顺序:DOM → 注册表默认值 → T{}
    template <setting_type T>
    [[nodiscard]] auto get(std::string_view path) const -> T
    {
        if (const auto snap = current_.load()) {
            if (const auto* node = detail::find_node(*snap, path)) {
                if (auto v = setting_codec<T>::from_toml(*node))
                    return std::move(*v);
            }
        }
        if (const auto* m = reg_.find(path)) {
            if (auto v = setting_codec<T>::from_toml(*m->default_value))
                return std::move(*v);
        }
        return T { };
    }

    // ── 写(写线程):类型守卫 → 约束校验 → copy-on-write 提交 ────────
    template <setting_type T>
    auto set(std::string_view path, T value) -> result<>
    {
        const auto* m = reg_.find(path);
        if (!m)
            return common::fail(common::errc::not_found, "unregistered setting key: {}", path);

        auto node = setting_codec<T>::to_toml(value);
        if (node.type() != m->default_value->type())
            return common::fail(common::errc::type_mismatch,
                "{}: value type does not match the declared type", path);
        if (auto r = m->validate(node); !r)
            return r;

        auto next = std::make_shared<toml::table>(*current_.load());
        detail::set_path_node(*next, path, node);
        current_.store(std::move(next));

        dirty_ = true;
        if (m->restart_required)
            pending_restart_ = true;
        return { };
    }

    // ── 元数据 / 状态 ────────────────────────────────────────────
    [[nodiscard]] auto registry() const noexcept -> const settings_registry& { return reg_; }
    [[nodiscard]] auto find(std::string_view path) const noexcept -> const setting_meta*
    {
        return reg_.find(path);
    }
    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& { return path_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] bool pending_restart() const noexcept { return pending_restart_; }

    // 上次 load/reload 产生的键级警告(类型/约束违规被忽略并回落默认的键)
    [[nodiscard]] auto warnings() const noexcept -> const std::vector<common::error>&
    {
        return warnings_;
    }

    // ── 全局读 facade(app 引导处登记一次)─────────────────────────
    [[nodiscard]] static auto global() noexcept -> config*;
    static void set_global(config& cfg) noexcept;

private:
    auto generate_file(const toml::table& dom) const -> std::string;

    settings_registry reg_;
    std::filesystem::path path_;
    std::atomic<std::shared_ptr<const toml::table>> current_;
    std::vector<common::error> warnings_;
    bool dirty_ = false;
    bool pending_restart_ = false;
};

// 全部内建设置的唯一声明清单(定义于 config.cpp)
void register_builtin_settings(settings_registry& reg);

} // namespace usip::core
