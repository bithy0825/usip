#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/base.h>
#include <fmt/format.h>

namespace usip::common {

enum class errc : std::uint8_t {
    unknown = 0,

    // ── 通用控制流 ──
    cancelled, // 操作被取消
    timeout, // 等待超时

    // ── IO / 资源 ──
    io, // 通用 IO 失败(打开/读/写)
    not_found, // 资源不存在
    already_exists, // 资源已存在
    permission_denied, // 权限不足
    resource_exhausted, // 内存/句柄/配额耗尽
    unavailable, // 资源暂不可用(设备忙、服务未启动)

    // ── 输入 / 数据 ──
    invalid_argument, // 调用方参数非法
    out_of_range, // 数值/索引越界
    parse, // 解析失败(文本/文件/表达式)
    type_mismatch, // 类型不符
    validation_failed, // 业务约束校验失败
    data_loss, // 数据损坏/不完整

    // ── 状态 ──
    not_initialized, // 尚未初始化
    already_initialized, // 重复初始化
    failed_precondition, // 当前状态不允许该操作
    aborted, // 操作被中止(如并发冲突)
    unsupported, // 不支持的格式/特性/操作
    unimplemented, // 尚未实现

    // ── 边界 ──
    internal, // 内部不变式违反(即 bug)
    external, // 第三方库/系统调用失败(消息包装 e.what() 等)
};

[[nodiscard]] constexpr auto errc_name(errc c) noexcept -> std::string_view
{
    switch (c) {
    case errc::unknown:
        return "unknown";
    case errc::cancelled:
        return "cancelled";
    case errc::timeout:
        return "timeout";
    case errc::io:
        return "io";
    case errc::not_found:
        return "not_found";
    case errc::already_exists:
        return "already_exists";
    case errc::permission_denied:
        return "permission_denied";
    case errc::resource_exhausted:
        return "resource_exhausted";
    case errc::unavailable:
        return "unavailable";
    case errc::invalid_argument:
        return "invalid_argument";
    case errc::out_of_range:
        return "out_of_range";
    case errc::parse:
        return "parse";
    case errc::type_mismatch:
        return "type_mismatch";
    case errc::validation_failed:
        return "validation_failed";
    case errc::data_loss:
        return "data_loss";
    case errc::not_initialized:
        return "not_initialized";
    case errc::already_initialized:
        return "already_initialized";
    case errc::failed_precondition:
        return "failed_precondition";
    case errc::aborted:
        return "aborted";
    case errc::unsupported:
        return "unsupported";
    case errc::unimplemented:
        return "unimplemented";
    case errc::internal:
        return "internal";
    case errc::external:
        return "external";
    }
    return "invalid_errc";
}

// ─── located_format:带位置捕获的格式串 ───────────────────────────────────────
//
// 为什么不能把 source_location 放在参数包之后:
//   template <typename... Args> f(fmt::format_string<Args...>, Args&&...,
//                                 source_location = current());   // ← 错误
// 非尾参数包在推导时恒为空包,后续实参会撞进 loc 形参(C2664)。
// 标准解法:把"格式串 + 位置"打包成一个类型 —— 其构造函数非变参,
// 默认 source_location 在调用点求值;fmt 的编译期格式检查在构造时照常生效。

template <typename... Args>
struct located_format {
    fmt::format_string<Args...> value;
    std::source_location loc;

    template <typename S>
        requires std::is_convertible_v<const S&, fmt::string_view>
    consteval located_format(const S& s,
        std::source_location l = std::source_location::current()) noexcept
        : value { fmt::format_string<Args...> { s } }
        , loc { l }
    {
    }
};

// 关键:type_identity_t 使包装类型成为非推导语境 —— 否则 MSVC 会尝试用
// 字符串字面量推导 located_format<Args...> 而失败(C2672),Args 只能来自参数包
template <typename... Args>
using located_format_t = std::type_identity_t<located_format<Args...>>;

// ─── error:错误码 + 上下文消息 + 自动捕获的构造位置 ──────────────────────────

class error {
public:
    // 直接构造(消息已就绪时);常规路径请用 make()/fail()
    error(errc code, std::string message,
        std::source_location loc = std::source_location::current()) noexcept
        : code_ { code }
        , message_ { std::move(message) }
        , location_ { loc }
    {
    }

    // fmt 风格格式化构造;source_location 自动捕获调用点(勿显式传 loc)
    template <typename... Args>
    [[nodiscard]] static auto make(errc code, located_format_t<Args...> fmt,
        Args&&... args) -> error
    {
        return error { code, fmt::format(fmt.value, std::forward<Args>(args)...), fmt.loc };
    }

    [[nodiscard]] constexpr auto code() const noexcept -> errc { return code_; }
    [[nodiscard]] auto message() const noexcept -> std::string_view { return message_; }
    [[nodiscard]] constexpr auto location() const noexcept -> const std::source_location&
    {
        return location_;
    }

    // "[io] 无法打开 config.toml (E:\...\config.cpp:42)"
    [[nodiscard]] auto to_string() const -> std::string
    {
        return std::format("[{}] {} ({}:{})", errc_name(code_), message_,
            location_.file_name(), location_.line());
    }

private:
    errc code_ { };
    std::string message_ { };
    std::source_location location_ { };
};

// ─── result:错误的主载体 ─────────────────────────────────────────────────────

template <typename T = void>
using result = std::expected<T, error>;

// return common::fail(errc::io, "cannot open {}", path);
template <typename... Args>
[[nodiscard]] auto fail(errc code, located_format_t<Args...> fmt, Args&&... args)
    -> std::unexpected<error>
{
    return std::unexpected {
        error { code, fmt::format(fmt.value, std::forward<Args>(args)...), fmt.loc }
    };
}

// ─── exception:薄异常壳(真正的例外 / 跨边界抛出用)───────────────────────────

class exception : public std::exception {
public:
    explicit exception(error err) noexcept
        : err_ { std::move(err) }
    {
    }

    template <typename... Args>
    explicit exception(errc code, located_format_t<Args...> fmt, Args&&... args)
        : err_ { error::make(code, fmt, std::forward<Args>(args)...) }
    {
    }

    // message_ 是 std::string,视图数据 null 终止,what() 直接借用
    [[nodiscard]] auto what() const noexcept -> const char* override
    {
        return err_.message().data();
    }

    [[nodiscard]] auto err() const noexcept -> const error& { return err_; }

private:
    error err_;
};

// ─── capture:边界捕获器 ──────────────────────────────────────────────────────
//
// 把"可能抛异常的调用"收敛为 result;f 返回 void / T / result<T> 均可。
// 典型用法(替代边界处成片的 try/catch):
//     if (auto r = common::capture([&] { opengl_.setup_global_format(); }); !r)
//         return std::unexpected(r.error());

namespace detail {

    template <typename T>
    struct is_result : std::false_type { };
    template <typename T>
    struct is_result<result<T>> : std::true_type { };
    template <typename T>
    inline constexpr bool is_result_v = is_result<T>::value;

    template <typename R>
    using capture_result_t = std::conditional_t<std::is_void_v<R>, result<>,
        std::conditional_t<is_result_v<R>, R, result<R>>>;

} // namespace detail

template <typename F>
[[nodiscard]] auto capture(F&& f) noexcept
    -> detail::capture_result_t<std::invoke_result_t<F>>
{
    using R = std::invoke_result_t<F>;

    try {
        if constexpr (std::is_void_v<R>) {
            std::invoke(std::forward<F>(f));
            return { };
        } else {
            // T → result<T> 隐式转换;result<T> 原样传递
            return std::invoke(std::forward<F>(f));
        }
    } catch (const exception& e) {
        return std::unexpected { e.err() };
    } catch (const std::exception& e) {
        return std::unexpected {
            error { errc::external, e.what(), std::source_location::current() }
        };
    } catch (...) {
        return std::unexpected {
            error { errc::unknown, "unrecognized exception", std::source_location::current() }
        };
    }
}

} // namespace usip::common

// ─── 格式化集成(spdlog/fmt 与 std::format 双侧)──────────────────────────────

template <>
struct fmt::formatter<usip::common::error> : fmt::formatter<std::string_view> {
    auto format(const usip::common::error& e, fmt::format_context& ctx) const
    {
        return fmt::formatter<std::string_view>::format(e.to_string(), ctx);
    }
};

template <>
struct std::formatter<usip::common::error> {
    constexpr auto parse(std::format_parse_context& ctx)
    {
        return ctx.begin();
    }

    auto format(const usip::common::error& e, std::format_context& ctx) const
    {
        return std::ranges::copy(e.to_string(), ctx.out()).out;
    }
};

// ─── 全项目通用别名(result 在 usip:: 下可无限定使用)─────────────────────────

namespace usip {
using common::result;
}
