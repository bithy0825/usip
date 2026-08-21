#include "menu_bar.hpp"
#include "config.hpp"
#include "event.hpp"
#include "icon_registry.hpp"
#include "logger.hpp"
#include "utility.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
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
    open_->setStatusTip(tr("Open a TIFF file (Ctrl+O)"));
    recent_ = file_menu->addMenu(tr("&Recent"));
    file_menu->addSeparator();
    save_ = file_menu->addAction(reg.icon("save").value_or(QIcon { }), tr("&Save"));
    save_->setShortcut(QKeySequence::Save);
    save_->setStatusTip(tr("Save a window screenshot to the default path (Ctrl+S)"));
    save_as_ = file_menu->addAction(reg.icon("save_as").value_or(QIcon { }), tr("Save &As..."));
    save_as_->setShortcut(QKeySequence::SaveAs);
    save_as_->setStatusTip(tr("Save a window screenshot to a chosen path (Ctrl+Shift+S)"));
    export_ = file_menu->addAction(reg.icon("export").value_or(QIcon { }), tr("&Export..."));
    export_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    export_->setStatusTip(tr("Advanced export: content, pages and format (Ctrl+E)"));
    file_menu->addSeparator();
    close_ = file_menu->addAction(reg.icon("close").value_or(QIcon { }), tr("&Close"));
    close_->setShortcut(QKeySequence::Close);
    exit_ = file_menu->addAction(reg.icon("exit").value_or(QIcon { }), tr("E&xit"));
    exit_->setShortcut(QKeySequence::Quit);
    exit_->setStatusTip(tr("Exit USIP (Ctrl+Q)"));

    auto view_menu = addMenu(tr("&View"));
    pseudocolor_ = view_menu->addAction(reg.icon("pseudocolor").value_or(QIcon { }), tr("&Pseudocolor"));
    pseudocolor_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S));
    pseudocolor_->setStatusTip(tr("Toggle pseudocolor (Shift+S)"));
    pseudocolor_->setCheckable(true);
    pseudocolor_->setChecked(false);
    zero_is_black_ = view_menu->addAction(reg.icon("zero_is_black").value_or(QIcon { }), tr("&Zero is Black"));
    zero_is_black_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_B));
    zero_is_black_->setStatusTip(tr("Map zero to black in pseudocolor (Shift+B)"));
    zero_is_black_->setCheckable(true);
    zero_is_black_->setChecked(core::config::global()->get<bool>("pseudocolor.zero_is_black"));
    mask_ = view_menu->addAction(reg.icon("mask").value_or(QIcon { }), tr("&Mask"));
    mask_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
    mask_->setStatusTip(tr("Toggle threshold mask overlay (Shift+M)"));
    mask_->setCheckable(true);
    mask_->setChecked(false);
    roi_ = view_menu->addAction(reg.icon("roi").value_or(QIcon { }), tr("&ROI"));
    roi_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_R));
    roi_->setStatusTip(tr("Toggle ROI overlay (Shift+R)"));
    roi_->setCheckable(true);
    roi_->setChecked(true); // L4 默认可见(开关控制渲染,五模式皆受控)
    annotation_ = view_menu->addAction(reg.icon("annotation").value_or(QIcon { }), tr("&Annotation"));
    annotation_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A));
    annotation_->setStatusTip(tr("Toggle annotation overlay (Shift+A)"));
    annotation_->setCheckable(true);
    annotation_->setChecked(true); // L5 默认可见(同上)
    view_menu->addSeparator();
    clear_constituency_ = view_menu->addAction(reg.icon("clear_constituency").value_or(QIcon { }), tr("Clear &Constituency"));
    clear_constituency_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    clear_constituency_->setStatusTip(tr("Clear all ROIs of the current page(s) (Ctrl+Shift+C)"));
    clear_measurements_ = view_menu->addAction(reg.icon("clear_measurement").value_or(QIcon { }), tr("Clear &Measurements"));
    clear_measurements_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    clear_measurements_->setStatusTip(tr("Clear all measurements of the current page(s) (Ctrl+Shift+M)"));

    // 语言(View 底部;互斥勾选;重启后生效 —— 翻译在启动时装配)
    view_menu->addSeparator();
    auto* language_menu = view_menu->addMenu(tr("&Language"));
    language_group_ = new QActionGroup(this);
    language_group_->setExclusive(true);
    const auto current_lang = core::config::global()->get<std::string>("ui.language");
    for (const auto& [lang, label] :
        { std::pair { "en", tr("&English") }, std::pair { "zh_CN", QString::fromUtf8("中文") } }) {
        auto* action = language_menu->addAction(label);
        action->setCheckable(true);
        action->setChecked(current_lang == lang);
        action->setData(QString::fromLatin1(lang));
        language_group_->addAction(action);
    }

    auto help_menu = addMenu(tr("&Help"));
    about_ = help_menu->addAction(reg.icon("about").value_or(QIcon { }), tr("&About"));
    about_->setShortcut(QKeySequence::HelpContents); // F1
    about_->setStatusTip(tr("About USIP (F1)"));

    update_recent_menu(); // 启动即从 config 恢复最近文件,而非等到首次打开
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
    // 退出 = 结束应用事件循环(配置等收尾在 application 析构)
    connect(exit_, &QAction::triggered, qApp, &QCoreApplication::quit);

    // 视图开关 → 细粒度渲染事件(画布订阅;已注册的键顺手持久化)
    connect(pseudocolor_, &QAction::triggered, this, [this](bool checked) {
        bus_.post<core::event::pseudocolor_enabled_toggled>(cbuspp::value<bool> { checked })
            .sync();
    });
    connect(mask_, &QAction::triggered, this, [this](bool checked) {
        bus_.post<core::event::mask_visible_toggled>(cbuspp::value<bool> { checked }).sync();
    });
    // L4/L5 可见开关(菜单与 top_tool_bar 共享同一 action;五模式皆受控)
    connect(roi_, &QAction::triggered, this, [this](bool checked) {
        bus_.post<core::event::roi_visible_toggled>(cbuspp::value<bool> { checked }).sync();
    });
    connect(annotation_, &QAction::triggered, this, [this](bool checked) {
        bus_.post<core::event::annotation_visible_toggled>(
                cbuspp::value<bool> { checked })
            .sync();
    });
    connect(clear_measurements_, &QAction::triggered, this, [this] {
        bus_.post<core::event::measurements_clear_requested>().sync();
    });
    connect(clear_constituency_, &QAction::triggered, this, [this] {
        bus_.post<core::event::rois_clear_requested>().sync();
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

    // 语言切换:写 config(重启生效;翻译装配在启动期)并提示
    connect(language_group_, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto lang = action->data().toString().toStdString();
        if (auto r = core::config::global()->set<std::string>("ui.language", lang); !r) {
            common::log_warn("set ui.language failed: {}", r.error());
            return;
        }
        bus_.post<core::event::status_message>(cbuspp::value<std::string> {
                                                   "Language will take effect after restart. / "
                                                   "语言将在重启后生效。" })
            .sync();
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
