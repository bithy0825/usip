#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include "error.hpp"

namespace usip::common {

struct log_config {
    std::string name { "usip" };
    std::string pattern { "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v" };
    spdlog::level::level_enum level { spdlog::level::info };

    bool to_console { true };
    bool to_file { true };
    std::filesystem::path file_path { "logs/usip.log" };
    std::size_t max_file_size_mb { 10 }; // 与 config 的 log.max_file_size_mb 对齐
    std::size_t max_files { 10 };
};

class logger {
public:
    // 构造即初始化;失败返回错误(不抛)
    [[nodiscard]] static auto create(log_config cfg) -> result<logger>;
    ~logger();

    logger(logger&&) noexcept;
    auto operator=(logger&&) noexcept -> logger&;
    logger(const logger&) = delete;
    auto operator=(const logger&) -> logger& = delete;

    void set_level(spdlog::level::level_enum lvl) noexcept;
    void flush() noexcept;

    [[nodiscard]] auto get() const noexcept -> const std::shared_ptr<spdlog::logger>&
    {
        return logger_;
    }

private:
    explicit logger(std::shared_ptr<spdlog::logger> lg) noexcept
        : logger_ { std::move(lg) }
    {
    }

    std::shared_ptr<spdlog::logger> logger_;
};

// ─── 自由函数:经默认 logger 输出,自动携带调用点 ─────────────────────────────

namespace detail {

    inline auto to_spdlog_loc(const std::source_location& loc) noexcept -> spdlog::source_loc
    {
        return { loc.file_name(), static_cast<int>(loc.line()), loc.function_name() };
    }

    template <typename... Args>
    inline void log_impl(spdlog::level::level_enum lvl, located_format_t<Args...> fmt,
        Args&&... args)
    {
        const auto& lg = spdlog::default_logger();
        if (!lg || !lg->should_log(lvl))
            return;

        // 格式检查已在 located_format 构造时完成,此处直接求值后按文本输出
        // (.get() 取 string_view;fmt v12 已废弃隐式转换运算符)
        lg->log(to_spdlog_loc(fmt.loc), lvl,
            spdlog::string_view_t { fmt::vformat(fmt.value.get(), fmt::make_format_args(args...)) });
    }

} // namespace detail

template <typename... Args>
void log_trace(located_format_t<Args...> fmt, Args&&... args)
{
    detail::log_impl(spdlog::level::trace, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_debug(located_format_t<Args...> fmt, Args&&... args)
{
    detail::log_impl(spdlog::level::debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_info(located_format_t<Args...> fmt, Args&&... args)
{
    detail::log_impl(spdlog::level::info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_warn(located_format_t<Args...> fmt, Args&&... args)
{
    detail::log_impl(spdlog::level::warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_error(located_format_t<Args...> fmt, Args&&... args)
{
    detail::log_impl(spdlog::level::err, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log_critical(located_format_t<Args...> fmt, Args&&... args)
{
    detail::log_impl(spdlog::level::critical, fmt, std::forward<Args>(args)...);
}

} // namespace usip::common
