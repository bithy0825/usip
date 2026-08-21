#include "app.hpp"

#include <QApplication>
#include <QTranslator>

#include "executor.hpp"
#include "platform/system.hpp"

namespace usip::app {

application::application() = default;

application::~application()
{
    ui_.reset(); // UI 先销毁:可能取消 bus 订阅、可能写日志

    if (service_) {
        service_->stop();
        service_.reset();
    }

    if (bus_)
        bus_.reset(); // 先于 executor:bus 持有其引用
    if (executor_)
        executor_.reset(); // 排空任务(任务可能写日志,须在 logger 关闭前停)

    if (config_ && config_->dirty()) {
        if (auto r = config_->save(); !r && logger_)
            common::log_error("failed to save config: {}", r.error());
    }

    logger_.reset(); // flush + 摘除 spdlog 默认 logger
    spdlog::shutdown(); // 停止周期刷新后台线程(释放其对日志文件的持有)
    config_.reset();
    // qt_app_ 由成员析构销毁(最后:所有 QWidget 已清理完毕)
}

auto application::init(int& argc, char** argv) -> result<>
{
    // ── 1. QApplication(须先于一切 QWidget)──────────────────────────────
    if (auto r = common::capture([&] {
            qt_app_ = std::make_unique<QApplication>(argc, argv);
        });
        !r)
        return std::unexpected(std::move(r).error());

    // ── 2. config(构造 + 注册 → capture;load 自带 result)────────────────
    if (auto r = common::capture([&] {
            core::settings_registry reg;
            core::register_builtin_settings(reg);
            config_ = std::make_unique<core::config>(std::move(reg));
        });
        !r)
        return std::unexpected(std::move(r).error());

    const auto config_path = platform::system::executable_dir() / "config.toml";
    if (auto r = config_->load(config_path); !r)
        return std::unexpected(std::move(r).error()); // 首启自动生成带注释默认文件
    core::config::set_global(*config_);

    // ── 3. logger(create 自带 result,无需 capture)────────────────────────
    common::log_config lc;
    lc.pattern = config_->get<std::string>("log.pattern");
    lc.level = spdlog::level::from_str(config_->get<std::string>("log.level"));
    lc.to_console = config_->get<bool>("log.to_console");
    lc.to_file = config_->get<bool>("log.to_file");
    // 相对路径相对 exe 目录解析,与工作目录无关
    lc.file_path = platform::system::executable_dir()
        / config_->get<std::string>("log.file_path");
    lc.max_file_size_mb = static_cast<std::size_t>(config_->get<int>("log.max_file_size_mb"));
    lc.max_files = static_cast<std::size_t>(config_->get<int>("log.max_files"));

    auto lg = common::logger::create(std::move(lc));
    if (!lg)
        return std::unexpected(std::move(lg).error());
    logger_.emplace(std::move(*lg));

    // config 加载期产生的键级警告,此时才具备记录条件
    for (const auto& w : config_->warnings())
        common::log_warn("{}", w);

    // ── 4. executor(线程数来自 config;0 = 自动用满全部核心)───────────────
    if (auto r = common::capture([&] {
            executor_ = std::make_unique<common::executor>(
                static_cast<std::size_t>(config_->get<int>("executor.thread_count")));
        });
        !r)
        return std::unexpected(std::move(r).error());

    // ── 5. 事件总线(挂在 executor 上)─────────────────────────────────────
    if (auto r = common::capture([&] {
            bus_ = std::make_unique<cbuspp::bus<common::executor>>(*executor_);
        });
        !r)
        return std::unexpected(std::move(r).error());

    if (auto r = common::capture([&] {
            service_ = std::make_unique<service::service>(*executor_, *bus_);
        });
        !r)
        return std::unexpected(std::move(r).error());

    // ── 界面翻译(config ui.language;zh_CN 装资源翻译,en = 英文源串不装)────
    if (config_->get<std::string>("ui.language") == "zh_CN") {
        translator_ = std::make_unique<QTranslator>();
        if (translator_->load(QStringLiteral(":/i18n/usip_zh_CN.qm"))) {
            QCoreApplication::installTranslator(translator_.get());
        } else {
            common::log_warn("failed to load translation: :/i18n/usip_zh_CN.qm");
            translator_.reset();
        }
    }

    if (auto r = common::capture([&] {
            ui_ = std::make_unique<ui::main_window>(*bus_);
        });
        !r)
        return std::unexpected(std::move(r).error());

    if (auto r = service_->start(); !r)
        return std::unexpected(std::move(r).error());

    common::log_info("usip started (config: {}, executor workers: {})",
        config_path.string(), executor_->thread_count());
    return { };
}

auto application::exec() -> int
{
    ui_->show();
    return QApplication::exec();
}

} // namespace usip::app
