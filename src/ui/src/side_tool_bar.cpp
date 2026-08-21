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
    bus_.on<core::event::measure_requested>().call(*this, &side_tool_bar::on_measure_requested);
    bus_.on<core::event::rectangle_draw_requested>()
        .call(*this, &side_tool_bar::on_rectangle_draw_requested);
    bus_.on<core::event::ellipse_draw_requested>()
        .call(*this, &side_tool_bar::on_ellipse_draw_requested);
    // 会话结束经 canvas 统一广播(携带模式;不直接订阅 apply/canceled)
    bus_.on<core::event::tool_session_ended>().call(*this, &side_tool_bar::on_tool_session_ended);
    bus_.on<core::event::view_mode_changed>().call(*this, &side_tool_bar::on_view_mode_changed);
}

void side_tool_bar::setup_connections()
{
    // 勾选 = 请求会话;取消勾选(再点一次)= 取消会话
    connect(rectangle_, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            bus_.post<core::event::rectangle_draw_requested>().sync();
        else
            bus_.post<core::event::tool_result_canceled>().sync();
    });
    connect(ellipse_, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            bus_.post<core::event::ellipse_draw_requested>().sync();
        else
            bus_.post<core::event::tool_result_canceled>().sync();
    });
    connect(polygon_, &QAction::triggered, this, [this] {
        bus_.post<core::event::polygon_draw_requested>().sync();
    });
    connect(threshold_seg_, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            bus_.post<core::event::threshold_segment_requested>().sync();
        else
            bus_.post<core::event::tool_result_canceled>().sync();
    });
    connect(measure_, &QAction::toggled, this, [this](bool checked) {
        if (checked)
            bus_.post<core::event::measure_requested>().sync();
        else
            bus_.post<core::event::tool_result_canceled>().sync();
    });
}

void side_tool_bar::on_threshold_segment_requested()
{
    session_active_ = true;
    refresh_enables();
}

void side_tool_bar::on_measure_requested()
{
    session_active_ = true;
    refresh_enables();
}

void side_tool_bar::on_rectangle_draw_requested()
{
    session_active_ = true;
    refresh_enables();
}

void side_tool_bar::on_ellipse_draw_requested()
{
    session_active_ = true;
    refresh_enables();
}

void side_tool_bar::on_tool_session_ended(const cbuspp::value<core::view_mode>& value)
{
    session_active_ = false;
    mode_ = *value;
    if (threshold_seg_->isChecked() || measure_->isChecked() || rectangle_->isChecked()
        || ellipse_->isChecked()) {
        const QSignalBlocker t(threshold_seg_), m(measure_), r(rectangle_), e(ellipse_);
        threshold_seg_->setChecked(false); // 按钮回落(阻断,不回发 canceled)
        measure_->setChecked(false);
        rectangle_->setChecked(false);
        ellipse_->setChecked(false);
    }
    refresh_enables();
}

void side_tool_bar::on_view_mode_changed(const cbuspp::value<core::view_mode>& value)
{
    mode_ = *value;
    refresh_enables();
}

void side_tool_bar::refresh_enables()
{
    // 双轴推导:会话排他(其余工具禁)+ 模式互斥(对比三模式禁阈值)
    const bool threshold_mode_ok
        = mode_ == core::view_mode::single || mode_ == core::view_mode::split;
    rectangle_->setEnabled(!session_active_);
    ellipse_->setEnabled(!session_active_);
    polygon_->setEnabled(!session_active_);
    measure_->setEnabled(!session_active_);
    threshold_seg_->setEnabled(!session_active_ && threshold_mode_ok);
    if (threshold_seg_->isChecked()) // 当前工具保持可点(再点即取消)
        threshold_seg_->setEnabled(true);
    if (measure_->isChecked())
        measure_->setEnabled(true);
    if (rectangle_->isChecked())
        rectangle_->setEnabled(true);
    if (ellipse_->isChecked())
        ellipse_->setEnabled(true);
}

} // namespace usip::ui
