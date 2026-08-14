#include "options_tool_bar.hpp"
#include "menu_bar.hpp"
#include <icon_registry.hpp>

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QWidget>

namespace usip::ui {

options_tool_bar::options_tool_bar(menu_bar& menu, cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol<options_tool_bar, QToolBar>(bus, parent)
    , menu_bar_(menu)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

options_tool_bar::~options_tool_bar() = default;

void options_tool_bar::setup_ui()
{
    setMovable(false);

    auto& reg = icon_registry::instance();

    // ── 对比视图模式(互斥 radio group,None = 单图视图) ────────────────
    view_group_ = new QActionGroup(this);
    view_group_->setExclusive(true);

    view_none_ = view_group_->addAction(reg.icon("none").value_or(QIcon { }), tr("&None"));
    view_none_->setCheckable(true);
    view_none_->setChecked(true);
    addAction(view_none_);

    view_split_ = view_group_->addAction(reg.icon("split").value_or(QIcon { }), tr("&Split"));
    view_split_->setCheckable(true);
    addAction(view_split_);

    view_slider_ = view_group_->addAction(reg.icon("slider").value_or(QIcon { }), tr("Sli&der"));
    view_slider_->setCheckable(true);
    addAction(view_slider_);

    view_highlight_ = view_group_->addAction(reg.icon("highlight").value_or(QIcon { }), tr("&Highlight"));
    view_highlight_->setCheckable(true);
    addAction(view_highlight_);

    view_difference_ = view_group_->addAction(reg.icon("difference").value_or(QIcon { }), tr("&Difference"));
    view_difference_->setCheckable(true);
    addAction(view_difference_);

    // ── 页帧控制 ───────────────────────────────────────────────────────
    auto* page_label = new QLabel(tr("Compared Page"), this);
    addWidget(page_label);

    page_control_ = new QSpinBox(this);
    page_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    page_control_->setMinimum(1);
    page_control_->setValue(1);
    addWidget(page_control_);

    addSeparator();

    // ── 上下文选项面板(根据当前工具显示不同参数) ───────────────────────
    options_stack_ = new QStackedWidget(this);
    addWidget(options_stack_);

    // ── 弹簧 ───────────────────────────────────────────────────────────
    auto* spring = new QWidget(this);
    spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(spring);

    addSeparator();

    // ── 清除操作(共享 menu_bar 的 action) ──────────────────────────────
    addAction(menu_bar_.clear_constituency_action());
    addAction(menu_bar_.clear_measurements_action());
}

void options_tool_bar::setup_subscriptions() { }

void options_tool_bar::setup_connections() { }

} // namespace usip::ui
