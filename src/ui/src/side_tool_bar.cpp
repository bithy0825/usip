#include "side_tool_bar.hpp"
#include "event.hpp"
#include "icon_registry.hpp"

#include <QAction>
#include <QSignalBlocker>
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
    rectangle_ = addAction(reg.icon("rectangle").value_or(QIcon { }), tr("&Rectangle"));
    rectangle_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
    rectangle_->setCheckable(true);

    ellipse_ = addAction(reg.icon("ellipse").value_or(QIcon { }), tr("&Ellipse"));
    ellipse_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_E));
    ellipse_->setCheckable(true);

    polygon_ = addAction(reg.icon("polygon").value_or(QIcon { }), tr("&Polygon"));
    polygon_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_P));
    polygon_->setCheckable(true);

    addSeparator();

    // ── 叠加工具 ─────────────────────────────────────────────────────
    threshold_seg_ = addAction(reg.icon("threshold_segmentation").value_or(QIcon { }), tr("&Threshold Segmentation"));
    threshold_seg_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_M));
    threshold_seg_->setCheckable(true);

    measure_ = addAction(reg.icon("measure").value_or(QIcon { }), tr("M&easure"));
    measure_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_D));
    measure_->setCheckable(true);
}

void side_tool_bar::setup_subscriptions()
{
    bus_.on<core::event::threshold_segment_requested>()
        .call(*this, &side_tool_bar::on_threshold_segment_requested);
    // 工具会话结束 → 解禁 + 按钮回落(取消勾选阻断,不回发 canceled)
    bus_.on<core::event::tool_result_applied>().call(*this, &side_tool_bar::on_tool_session_ended);
    bus_.on<core::event::tool_result_canceled>()
        .call(*this, &side_tool_bar::on_tool_session_ended);
}

void side_tool_bar::setup_connections()
{
    connect(rectangle_, &QAction::triggered, this, [this] {
        bus_.post<core::event::rectangle_draw_requested>().sync();
    });
    connect(ellipse_, &QAction::triggered, this, [this] {
        bus_.post<core::event::ellipse_draw_requested>().sync();
    });
    connect(polygon_, &QAction::triggered, this, [this] {
        bus_.post<core::event::polygon_draw_requested>().sync();
    });
    // 勾选 = 请求会话;取消勾选(再点一次)= 取消会话
    connect(threshold_seg_, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            bus_.post<core::event::threshold_segment_requested>().sync();
        else
            bus_.post<core::event::tool_result_canceled>().sync();
    });
    connect(measure_, &QAction::triggered, this, [this] {
        bus_.post<core::event::measure_requested>().sync();
    });
}

void side_tool_bar::on_threshold_segment_requested()
{
    for (auto* action : { rectangle_, ellipse_, polygon_, measure_ })
        action->setEnabled(false);
}

void side_tool_bar::on_tool_session_ended()
{
    for (auto* action : { rectangle_, ellipse_, polygon_, measure_ })
        action->setEnabled(true);
    if (!threshold_seg_->isChecked())
        return;
    const QSignalBlocker blocker(threshold_seg_);
    threshold_seg_->setChecked(false);
}

} // namespace usip::ui
