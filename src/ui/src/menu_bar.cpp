#include "menu_bar.hpp"
#include "config.hpp"
#include "event.hpp"
#include "icon_registry.hpp"
#include "logger.hpp"
#include "utility.hpp"

#include <QAction>
#include <QMenu>
#include <qnamespace.h>

namespace usip::ui {

menu_bar::menu_bar(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

menu_bar::~menu_bar() = default;

void menu_bar::setup_ui()
{
    auto& reg = icon_registry::instance();

    auto file_menu = addMenu(tr("&File"));
    open_ = file_menu->addAction(reg.icon("open").value_or(QIcon { }), tr("&Open..."));
    open_->setShortcut(QKeySequence::Open);
    recent_ = file_menu->addMenu(tr("&Recent"));
    file_menu->addSeparator();
    save_ = file_menu->addAction(reg.icon("save").value_or(QIcon { }), tr("&Save"));
    save_->setShortcut(QKeySequence::Save);
    save_as_ = file_menu->addAction(reg.icon("save_as").value_or(QIcon { }), tr("Save &As..."));
    save_as_->setShortcut(QKeySequence::SaveAs);
    export_ = file_menu->addAction(reg.icon("export").value_or(QIcon { }), tr("&Export..."));
    export_->setShortcut(QKeySequence::Print);
    file_menu->addSeparator();
    close_ = file_menu->addAction(reg.icon("close").value_or(QIcon { }), tr("&Close"));
    close_->setShortcut(QKeySequence::Close);
    exit_ = file_menu->addAction(reg.icon("exit").value_or(QIcon { }), tr("E&xit"));
    exit_->setShortcut(QKeySequence::Quit);

    auto view_menu = addMenu(tr("&View"));
    pseudocolor_ = view_menu->addAction(reg.icon("pseudocolor").value_or(QIcon { }), tr("&Pseudocolor"));
    pseudocolor_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S));
    pseudocolor_->setCheckable(true);
    pseudocolor_->setChecked(false);
    mask_ = view_menu->addAction(reg.icon("mask").value_or(QIcon { }), tr("&Mask"));
    mask_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
    mask_->setCheckable(true);
    mask_->setChecked(true);
    zero_is_black_ = view_menu->addAction(reg.icon("zero_is_black").value_or(QIcon { }), tr("&Zero is Black"));
    zero_is_black_->setCheckable(true);
    zero_is_black_->setChecked(core::config::global()->get<bool>("pseudocolor.zero_is_black"));
    view_menu->addSeparator();
    clear_constituency_ = view_menu->addAction(reg.icon("clear_constituency").value_or(QIcon { }), tr("Clear &Constituency"));
    clear_measurements_ = view_menu->addAction(reg.icon("clear_measurement").value_or(QIcon { }), tr("Clear &Measurements"));

    auto help_menu = addMenu(tr("&Help"));
    about_ = help_menu->addAction(reg.icon("about").value_or(QIcon { }), tr("&About"));
}

void menu_bar::setup_subscriptions()
{
    bus_.on<core::event::file_selected>().require_trace_id(core::event::trace_id::file_service).call(this, &menu_bar::on_file_selected);
}

void menu_bar::setup_connections()
{
    connect(open_, &QAction::triggered, this, [this]() {
        bus_.post<core::event::file_open_requested>().sync();
    });

    // 视图开关 → 细粒度渲染事件(画布订阅;已注册的键顺手持久化)
    connect(pseudocolor_, &QAction::triggered, this, [this](bool checked) {
        bus_.post<core::event::pseudocolor_enabled_toggled>(cbuspp::value<bool> { checked })
            .sync();
    });
    connect(mask_, &QAction::triggered, this, [this](bool checked) {
        bus_.post<core::event::mask_visible_toggled>(cbuspp::value<bool> { checked }).sync();
    });
    connect(zero_is_black_, &QAction::triggered, this, [this](bool checked) {
        bus_.post<core::event::pseudocolor_zero_is_black_toggled>(
            cbuspp::value<bool> { checked })
            .sync();
        if (auto r = core::config::global()->set<bool>("pseudocolor.zero_is_black", checked);
            !r) {
            common::log_warn("set pseudocolor.zero_is_black failed: {}", r.error());
        }
    });
}

void menu_bar::on_file_selected(const cbuspp::value<std::filesystem::path>& path)
{
    add_recent_file(*path);
    update_recent_menu();
}

void menu_bar::add_recent_file(const std::filesystem::path& path)
{
    auto recent_files = core::config::global()->get<std::vector<std::string>>("file.recent_files");
    const auto entry = common::path_to_utf8(path);

    std::erase(recent_files, entry);
    recent_files.insert(recent_files.begin(), entry);

    const auto max_recent_files = core::config::global()->get<int>("file.max_recent_files");
    if (std::cmp_greater(recent_files.size(), max_recent_files)) {
        recent_files.resize(static_cast<std::size_t>(max_recent_files));
    }

    if (auto r = core::config::global()->set("file.recent_files", recent_files); !r) {
        common::log_warn("add_recent_file: {}", r.error());
    }
}

void menu_bar::open_recent_file(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        auto err = common::error::make(common::errc::not_found,
            "file no longer exists: {}", common::path_to_utf8(path));
        bus_.post<core::event::error_occurred>(
                cbuspp::value<common::error&> { err })
            .sync();

        auto recent_files = core::config::global()->get<std::vector<std::string>>("file.recent_files");
        std::erase(recent_files, common::path_to_utf8(path));
        if (auto r = core::config::global()->set("file.recent_files", recent_files); !r) {
            common::log_warn("open_recent_file: {}", r.error());
        }
        update_recent_menu();
        return;
    }

    add_recent_file(path);
    update_recent_menu();
    bus_.post<core::event::file_selected>(cbuspp::value<std::filesystem::path> { path }).sync();
}

void menu_bar::update_recent_menu()
{
    recent_->clear();

    auto recent_files = core::config::global()->get<std::vector<std::string>>("file.recent_files");
    if (recent_files.empty()) {
        auto* placeholder = recent_->addAction(tr("No Recent Files"));
        placeholder->setEnabled(false);
        return;
    }

    for (const auto& [index, entry] : std::views::enumerate(recent_files)) {
        const auto path = common::path_from_utf8(entry);
        const auto label = std::format("&{} {}", index + 1, common::path_to_utf8(path.filename()));
        auto* action = recent_->addAction(QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size())));
        action->setToolTip(QString::fromUtf8(entry.data(), static_cast<qsizetype>(entry.size())));
        connect(action, &QAction::triggered, this, [this, path]() {
            open_recent_file(path);
        });
    }

    recent_->addSeparator();
    auto* clear = recent_->addAction(tr("&Clear Recent Files"));
    connect(clear, &QAction::triggered, this, [this]() {
        if (auto r = core::config::global()->set("file.recent_files", std::vector<std::string> { }); !r) {
            common::log_warn("update_recent_menu: {}", r.error());
        }
        update_recent_menu();
    });
}

} // namespace usip::ui
