#include "top_tool_bar.hpp"
#include "menu_bar.hpp"

#include <QAction>
#include <QWidget>

namespace usip::ui {

top_tool_bar::top_tool_bar(menu_bar& menu, cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol<top_tool_bar, QToolBar>(bus, parent)
    , menu_bar_(menu)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

top_tool_bar::~top_tool_bar() = default;

void top_tool_bar::setup_ui()
{
    setMovable(false);

    // ── 文件操作(共享 menu_bar 的 action) ──────────────────────────────
    addAction(menu_bar_.open_action());
    addAction(menu_bar_.save_action());
    addSeparator();

    // ── 视图开关(共享 menu_bar 的 action) ──────────────────────────────
    addAction(menu_bar_.pseudocolor_action());
    addAction(menu_bar_.mask_action());
    addSeparator();

    // ── 弹簧 ───────────────────────────────────────────────────────────
    auto* spring = new QWidget(this);
    spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(spring);

    addSeparator();

    // ── 关于(共享 menu_bar 的 action) ──────────────────────────────────
    addAction(menu_bar_.about_action());
}

void top_tool_bar::setup_subscriptions() { }

void top_tool_bar::setup_connections() { }

} // namespace usip::ui
