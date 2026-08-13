#include "logger.hpp"

#include <chrono>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace usip::common {

auto logger::create(log_config cfg) -> result<logger>
{
    // capture 收编 spdlog 抛出的 spdlog_ex/filesystem 异常
    return capture([&cfg]() -> result<logger> {
        std::vector<spdlog::sink_ptr> sinks;

        if (cfg.to_console) {
            auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console->set_pattern(cfg.pattern);
            sinks.push_back(std::move(console));
        }

        if (cfg.to_file) {
            if (cfg.file_path.has_parent_path())
                std::filesystem::create_directories(cfg.file_path.parent_path());

            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                cfg.file_path.string(),
                cfg.max_file_size_mb * 1024 * 1024,
                cfg.max_files);
            file->set_pattern(cfg.pattern);
            sinks.push_back(std::move(file));
        }

        if (sinks.empty())
            return fail(errc::invalid_argument,
                "log_config: at least one sink (console/file) must be enabled");

        auto lg = std::make_shared<spdlog::logger>(cfg.name, sinks.begin(), sinks.end());
        lg->set_level(cfg.level);
        lg->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(lg);
        spdlog::flush_every(std::chrono::seconds(3));

        return logger { std::move(lg) };
    });
}

logger::~logger()
{
    if (!logger_)
        return;

    logger_->flush();
    if (spdlog::default_logger() == logger_)
        spdlog::set_default_logger(nullptr);
}

logger::logger(logger&&) noexcept = default;
auto logger::operator=(logger&&) noexcept -> logger& = default;

void logger::set_level(spdlog::level::level_enum lvl) noexcept
{
    if (logger_)
        logger_->set_level(lvl);
}

void logger::flush() noexcept
{
    if (logger_)
        logger_->flush();
}

} // namespace usip::common
