#include "main_window.hpp"
#include "about_dialog.hpp"
#include "canvas.hpp"
#include "export_dialog.hpp"
#include "hist_dock.hpp"
#include "icon_registry.hpp"
#include "index_dock.hpp"
#include "info_dock.hpp"
#include "logger.hpp"
#include "menu_bar.hpp"
#include "options_tool_bar.hpp"
#include "side_tool_bar.hpp"
#include "status_bar.hpp"
#include "top_tool_bar.hpp"
#include "utility.hpp"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>

#include <format>

namespace usip::ui {

main_window::main_window(
    cbuspp::bus<common::executor>& bus,
    QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

main_window::~main_window() = default;

void main_window::setup_ui()
{
    if (auto r = icon_registry::instance().scan(); !r) {
        common::log_warn("icon scan failed: {}", r.error());
    }
    if (const auto app_icon = icon_registry::instance().icon("app"))
        setWindowIcon(*app_icon);

    canvas_ = new canvas(bus_, this); // 中央画布(渲染管线宿主)
    setCentralWidget(canvas_);

    menu_bar_ = new menu_bar(bus_, this);
    setMenuBar(menu_bar_);

    top_tool_bar_ = new top_tool_bar(*menu_bar_, bus_, this);
    addToolBar(top_tool_bar_);

    addToolBarBreak(Qt::TopToolBarArea);

    options_tool_bar_ = new options_tool_bar(*menu_bar_, bus_, this);
    addToolBar(options_tool_bar_);

    side_tool_bar_ = new side_tool_bar(bus_, this);
    addToolBar(Qt::LeftToolBarArea, side_tool_bar_);

    index_dock_ = new index_dock(bus_, this);
    addDockWidget(Qt::RightDockWidgetArea, index_dock_);

    info_dock_ = new info_dock(bus_, this);
    addDockWidget(Qt::RightDockWidgetArea, info_dock_);

    hist_dock_ = new hist_dock(bus_, this);
    addDockWidget(Qt::RightDockWidgetArea, hist_dock_);

    status_bar_ = new status_bar(bus_, this);
    setStatusBar(status_bar_);

    resize(1440, 900); // 默认窗口尺寸(图像分析工作区;用户可再调)
    // 右侧 dock 初始宽度:表格列多,过窄会大面积省略表头与数据
    resizeDocks({ index_dock_, info_dock_, hist_dock_ }, { 560, 560, 560 }, Qt::Horizontal);
    // 禁掉主窗口默认的右键菜单(显隐 toolbar/dock;不需要)
    setContextMenuPolicy(Qt::NoContextMenu);
}

void main_window::setup_subscriptions()
{
    bus_.on<core::event::document_ready>().call(*this, &main_window::on_document_ready);
    bus_.on<core::event::document_switch>().call(*this, &main_window::on_document_switch);
    bus_.on<core::event::document_closed>().call(*this, &main_window::on_document_closed);
}

void main_window::setup_connections()
{
    // 文件动作(save/export/close 须触碰窗口/画布/激活文档,归本类;about 同理)
    connect(menu_bar_->save_action(), &QAction::triggered, this,
        [this] { on_save_screenshot(); });
    connect(menu_bar_->save_as_action(), &QAction::triggered, this,
        [this] { on_save_screenshot_as(); });
    connect(menu_bar_->export_action(), &QAction::triggered, this, [this] { on_export(); });
    connect(menu_bar_->about_action(), &QAction::triggered, this, [this] { on_about(); });
    // Close:关闭激活文档(服务端释放全部资源并广播;无文档则无操作)
    connect(menu_bar_->close_action(), &QAction::triggered, this, [this] {
        if (const auto doc = doc_.lock())
            bus_.post<core::event::document_close_requested>(
                    cbuspp::value<cuuidpp::uuid> { doc->info.id })
                .sync();
    });
}

void main_window::on_document_ready(
    const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    if (*value)
        doc_ = *value;
}

void main_window::on_document_switch(
    const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    if (*value) // 空载荷(如重复打开的提醒)不改跟踪
        doc_ = *value;
}

void main_window::on_document_closed(const cbuspp::value<cuuidpp::uuid>& value)
{
    if (const auto doc = doc_.lock(); doc && doc->info.id == *value)
        doc_.reset(); // 被关即当前:解除跟踪;剩余文档经 document_switch 重建
}

auto main_window::screenshot_default_base() const -> QString
{
    if (const auto doc = doc_.lock()) {
        const auto& src = doc->info.path;
        return QString::fromStdString(common::path_to_utf8(src.parent_path())) + "/"
            + QString::fromStdString(common::path_to_utf8(src.stem())) + "_screenshot";
    }
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
        + "/usip_screenshot";
}

void main_window::on_save_screenshot()
{
    // 首个未占用名:base.png → base_1.png …(直存不弹框,不覆盖旧件)
    const QString base = screenshot_default_base();
    for (int n = 0; n < 1000; ++n) {
        const QString path = n == 0 ? base + ".png"
                                    : QStringLiteral("%1_%2.png").arg(base).arg(n);
        if (!QFileInfo::exists(path)) {
            save_screenshot_to(path);
            return;
        }
    }
}

void main_window::on_save_screenshot_as()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"),
        screenshot_default_base() + ".png", tr("PNG Image (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";
    save_screenshot_to(path);
}

void main_window::save_screenshot_to(const QString& path)
{
    if (grab().toImage().save(path, "PNG")) {
        bus_.post<core::event::status_message>(cbuspp::value<std::string> {
                                                   std::format("Screenshot saved: {}",
                                                       path.toUtf8().toStdString()) })
            .sync();
        return;
    }
    auto err = common::error::make(common::errc::io, "failed to save screenshot: {}",
        path.toUtf8().toStdString());
    bus_.post<core::event::error_occurred>(cbuspp::value<common::error&> { err }).sync();
}

void main_window::on_export()
{
    export_dialog dlg { bus_, doc_.lock(), canvas_, this };
    dlg.exec();
}

void main_window::on_about()
{
    about_dialog dlg { this };
    dlg.exec();
}

}
