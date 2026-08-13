#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include "error.hpp"

namespace usip::core {

using string_list = std::vector<std::string>;

// ─── setting_codec:开放扩展点 ────────────────────────────────────────────────
//
// 支持新的设置值类型时,特化本模板即可,框架代码零改动:
//   template <> struct setting_codec<std::filesystem::path> { ... };
//
// from_toml 以 const toml::node& 读取;to_toml 返回具体节点类型
// (toml::value<X> / toml::array 等,toml::node 为抽象基类不可按值构造)。

template <typename T, typename = void>
struct setting_codec; // 未特化 = 不是合法的设置类型

// toml++ 原生标量(bool / 整型 / 浮点 / 字符串)
template <typename T>
concept toml_scalar = std::same_as<T, bool>
    || (std::is_integral_v<T> && !std::same_as<T, bool>)
    || std::is_floating_point_v<T>
    || std::same_as<T, std::string>;

template <toml_scalar T>
struct setting_codec<T> {
    [[nodiscard]] static auto from_toml(const toml::node& node) -> std::optional<T>
    {
        if constexpr (std::same_as<T, bool>) {
            return node.value<bool>();
        } else if constexpr (std::is_integral_v<T>) {
            // toml 整数为 int64:收窄时做值域检查,溢出即类型不符
            const auto i = node.value<std::int64_t>();
            if (!i)
                return std::nullopt;
            if (*i < 0) {
                if constexpr (!std::is_signed_v<T>)
                    return std::nullopt;
                else if (*i < static_cast<std::int64_t>(std::numeric_limits<T>::min()))
                    return std::nullopt;
            } else {
                const auto u = static_cast<std::uint64_t>(*i);
                if constexpr (std::numeric_limits<T>::max()
                    <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                    if (u > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                        return std::nullopt;
            }
            return static_cast<T>(*i);
        } else if constexpr (std::is_floating_point_v<T>) {
            if (const auto d = node.value<double>())
                return static_cast<T>(*d);
            if (const auto i = node.value<std::int64_t>()) // 容忍整数字面量
                return static_cast<T>(*i);
            return std::nullopt;
        } else {
            return node.value<std::string>();
        }
    }

    [[nodiscard]] static auto to_toml(const T& v)
    {
        if constexpr (std::same_as<T, bool>)
            return toml::value<bool> { v };
        else if constexpr (std::is_integral_v<T>)
            return toml::value<std::int64_t> { static_cast<std::int64_t>(v) };
        else if constexpr (std::is_floating_point_v<T>)
            return toml::value<double> { static_cast<double>(v) };
        else
            return toml::value<std::string> { v };
    }
};

// 列表 ↔ TOML array(元素递归走 codec)
template <typename T>
struct setting_codec<std::vector<T>> {
    [[nodiscard]] static auto from_toml(const toml::node& node) -> std::optional<std::vector<T>>
    {
        const auto* arr = node.as_array();
        if (!arr)
            return std::nullopt;

        std::vector<T> out;
        out.reserve(arr->size());
        for (const auto& el : *arr) {
            auto v = setting_codec<T>::from_toml(el);
            if (!v)
                return std::nullopt;
            out.push_back(std::move(*v));
        }
        return out;
    }

    [[nodiscard]] static auto to_toml(const std::vector<T>& v) -> toml::array
    {
        toml::array arr;
        arr.reserve(v.size());
        for (const auto& x : v)
            arr.push_back(setting_codec<T>::to_toml(x));
        return arr;
    }
};

// 合法设置类型:codec 已特化(from_toml/to_toml 均可用,to_toml 产出具体节点)
template <typename T>
concept setting_type = requires(const toml::node& n, const T& v) {
    { setting_codec<T>::from_toml(n) } -> std::convertible_to<std::optional<T>>;
    { setting_codec<T>::to_toml(v) } -> std::derived_from<toml::node>;
};

template <typename T>
concept list_setting = requires { typename T::value_type; }
    && std::same_as<T, std::vector<typename T::value_type>>;

// ─── 一项设置的完整元数据 ────────────────────────────────────────────────────

struct setting_meta {
    std::string_view path; // 点分路径,如 "opengl.samples"
    std::shared_ptr<const toml::node> default_value; // 类型擦除的默认值节点
    std::string_view description; // 生成配置文件时写作 # 注释

    bool restart_required = false; // 修改后需重启生效

    // 约束校验链(range/one_of/max_count/custom 统一组装于此)
    std::vector<std::function<result<>(const toml::node&)>> checks;

    [[nodiscard]] auto validate(const toml::node& v) const -> result<>
    {
        for (const auto& check : checks)
            if (auto r = check(v); !r)
                return r;
        return { };
    }
};

// ─── 注册表 ──────────────────────────────────────────────────────────────────

class settings_registry {
public:
    template <setting_type T>
    class builder {
    public:
        auto range(T lo, T hi) -> builder&
            requires(std::is_arithmetic_v<T> && !std::same_as<T, bool>)
        {
            return add_check([lo, hi, path = meta_->path](const T& v) -> result<> {
                if (v < lo || v > hi)
                    return common::fail(common::errc::out_of_range,
                        "{}: value {} is out of range [{}, {}]", path, v, lo, hi);
                return { };
            });
        }

        auto one_of(std::initializer_list<std::string_view> values) -> builder&
            requires std::same_as<T, std::string>
        {
            std::vector<std::string> allowed;
            allowed.reserve(values.size());
            for (const auto v : values)
                allowed.emplace_back(v);

            return add_check([allowed = std::move(allowed),
                                 path = meta_->path](const std::string& v) -> result<> {
                if (std::ranges::find(allowed, v) == allowed.end())
                    return common::fail(common::errc::invalid_argument,
                        "{}: \"{}\" is not an allowed value", path, v);
                return { };
            });
        }

        auto max_count(std::size_t n) -> builder&
            requires list_setting<T>
        {
            return add_check([n, path = meta_->path](const T& v) -> result<> {
                if (v.size() > n)
                    return common::fail(common::errc::out_of_range,
                        "{}: count {} exceeds maximum {}", path, v.size(), n);
                return { };
            });
        }

        auto restart_required() noexcept -> builder&
        {
            meta_->restart_required = true;
            return *this;
        }

        auto validate(std::function<result<>(const T&)> fn) -> builder&
        {
            return add_check(std::move(fn));
        }

    private:
        friend class settings_registry;

        explicit builder(setting_meta& m) noexcept
            : meta_ { &m }
        {
        }

        // 把强类型检查包装为节点检查:先经 codec 解码,类型不符即报错
        auto add_check(std::function<result<>(const T&)> fn) -> builder&
        {
            meta_->checks.push_back(
                [fn = std::move(fn), path = meta_->path](const toml::node& node) -> result<> {
                    const auto v = setting_codec<T>::from_toml(node);
                    if (!v)
                        return common::fail(common::errc::type_mismatch,
                            "{}: value type does not match the declared type", path);
                    return fn(*v);
                });
            return *this;
        }

        setting_meta* meta_;
    };

    // path / description 须为静态生存期字符串(声明处用字面量即可)
    template <setting_type T>
    auto add(std::string_view path, T default_value, std::string_view description)
        -> builder<T>
    {
        assert(index_.find(path) == index_.end() && "duplicate setting key registration");

        auto def = setting_codec<T>::to_toml(default_value);
        auto& m = metas_.emplace_back(setting_meta {
            .path = path,
            .default_value = std::make_shared<const std::decay_t<decltype(def)>>(std::move(def)),
            .description = description,
        });
        index_.emplace(m.path, &m);
        return builder<T> { m };
    }

    [[nodiscard]] auto find(std::string_view path) const noexcept -> const setting_meta*
    {
        const auto it = index_.find(path);
        return it != index_.end() ? it->second : nullptr;
    }

    // 按声明顺序遍历(= 生成的配置文件布局顺序)
    [[nodiscard]] auto metas() const noexcept -> const std::deque<setting_meta>&
    {
        return metas_;
    }

    // 是否存在以 prefix 为祖先表的已注册键(如 "opengl" 命中 "opengl.samples")
    [[nodiscard]] auto any_key_under(std::string_view prefix) const -> bool
    {
        const auto it = index_.lower_bound(prefix);
        if (it == index_.end())
            return false;
        const std::string_view key = it->first;
        return key.size() > prefix.size()
            && key.starts_with(prefix)
            && key[prefix.size()] == '.';
    }

private:
    // deque:容器移动后元素地址不变,index_ 中的 meta 指针保持有效,
    // 因此 config 可以安全地按值持有注册表
    std::deque<setting_meta> metas_;
    std::map<std::string_view, const setting_meta*, std::less<>> index_;
};

} // namespace usip::core
