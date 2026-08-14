#include "side_tool_bar.hpp"
#include <icon_registry.hpp>

#include <QAction>
#include <qkeysequence.h>
#include <qnamespace.h>

namespace usip::ui {

side_tool_bar::side_tool_bar(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol<side_tool_bar, QToolBar>(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

side_tool_bar::~side_tool_bar() = default;

void side_tool_bar::setup_ui()
{
    setMovable(false);

    auto& reg = icon_registry::instance();

    // ── 绘制工具 ─────────────────────────────────────────────────────
    rectangle_ = addAction(reg.icon("rectangle").value_or(QIcon {}), tr("&Rectangle"));
    rectangle_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
    rectangle_->setCheckable(true);

    ellipse_ = addAction(reg.icon("ellipse").value_or(QIcon {}), tr("&Ellipse"));
    ellipse_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_E));
    ellipse_->setCheckable(true);

    polygon_ = addAction(reg.icon("polygon").value_or(QIcon {}), tr("&Polygon"));
    polygon_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_P));
    polygon_->setCheckable(true);

    addSeparator();

    // ── 叠加工具 ─────────────────────────────────────────────────────
    mask_ = addAction(reg.icon("mask").value_or(QIcon {}), tr("&Mask"));
    mask_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_M));
    mask_->setCheckable(true);

    measure_ = addAction(reg.icon("measure").value_or(QIcon {}), tr("M&easure"));
    measure_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_D));
    measure_->setCheckable(true);
}

void side_tool_bar::setup_subscriptions() { }

void side_tool_bar::setup_connections() { }

} // namespace usip::ui
