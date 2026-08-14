#include "main_window.hpp"
#include "icon_registry.hpp"
#include "index_dock.hpp"
#include "hist_dock.hpp"
#include "logger.hpp"
#include "menu_bar.hpp"
#include "options_tool_bar.hpp"
#include "side_tool_bar.hpp"
#include "top_tool_bar.hpp"
#include "info_dock.hpp"

namespace usip::ui {

main_window::main_window(
    cbuspp::bus<common::executor>& bus,
    QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
}

main_window::~main_window() = default;

void main_window::setup_ui()
{
    if (auto r = icon_registry::instance().scan(); !r) {
        common::log_warn("icon scan failed: {}", r.error());
    }

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
}

void main_window::setup_subscriptions() { }

}
