#include "status_bar.hpp"

#include <QLabel>

namespace usip::ui {

status_bar::status_bar(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

status_bar::~status_bar() = default;

void status_bar::setup_ui()
{
    setSizeGripEnabled(false);

    pixel_ = new QLabel(this); // 右下常驻:光标像素取样
    addPermanentWidget(pixel_);
}

void status_bar::setup_subscriptions()
{
    bus_.on<core::event::pixel_sample_changed>()
        .call(*this, &status_bar::on_pixel_sample_changed);
    bus_.on<core::event::status_message>().call(*this, &status_bar::on_status_message);
    bus_.on<core::event::error_occurred>().call(*this, &status_bar::on_error_occurred);

    // 工具会话提示(左下,含快捷键;申请即显示,驳回路径紧接 canceled 清除)
    bus_.on<core::event::rectangle_draw_requested>().call([this] {
        on_tool_session_started(tr("Rectangle: Drag to draw | Shift: Square | 1-4: "
                                   "Union/Intersect/Difference/Xor | Right-click: Undo | "
                                   "Enter: Apply | Esc: Cancel"));
    });
    bus_.on<core::event::ellipse_draw_requested>().call([this] {
        on_tool_session_started(tr("Ellipse: Drag to draw | Shift: Circle | 1-4: "
                                   "Union/Intersect/Difference/Xor | Right-click: Undo | "
                                   "Enter: Apply | Esc: Cancel"));
    });
    bus_.on<core::event::polygon_draw_requested>().call([this] {
        on_tool_session_started(tr("Polygon: Click to add vertices | Double-click: Close | "
                                   "1-4: Union/Intersect/Difference/Xor | Right-click: Undo | "
                                   "Enter: Apply | Esc: Cancel"));
    });
    bus_.on<core::event::threshold_segment_requested>().call([this] {
        on_tool_session_started(
            tr("Threshold: Drag the range slider or edit Floor/Ceil | Enter: Apply | "
               "Esc: Cancel"));
    });
    bus_.on<core::event::measure_requested>().call([this] {
        on_tool_session_started(
            tr("Measure: Drag from start to end point | Shift: Align H/V | Enter: Apply | "
               "Esc: Cancel"));
    });
    bus_.on<core::event::tool_session_ended>().call(*this, &status_bar::on_tool_session_ended);
}

void status_bar::setup_connections() { }

void status_bar::on_pixel_sample_changed(
    const cbuspp::value<std::optional<core::pixel_sample>>& value)
{
    if (!*value) { // 越界/无页:清空
        pixel_->clear();
        return;
    }
    const auto& s = **value;
    const auto gray = [](const std::optional<int>& v) {
        return v ? QString::number(*v) : QStringLiteral("-");
    };
    if (s.secondary) { // 对比模式:同坐标主/副并列
        pixel_->setText(QStringLiteral("X %1   Y %2   S %3   C %4   ")
                .arg(s.pos.x(), 5)
                .arg(s.pos.y(), 5)
                .arg(gray(s.primary), 5)
                .arg(gray(s.secondary), 5));
    } else {
        pixel_->setText(QStringLiteral("X %1   Y %2   Gray %3   ")
                .arg(s.pos.x(), 5)
                .arg(s.pos.y(), 5)
                .arg(gray(s.primary), 5));
    }
}

void status_bar::on_status_message(const cbuspp::value<std::string>& value)
{
    showMessage(QString::fromStdString(*value), 5000); // 限时反馈,不扰基线
}

void status_bar::on_error_occurred(const cbuspp::value<common::error&>& value)
{
    showMessage(QString::fromUtf8((*value).message().data(),
                    static_cast<qsizetype>((*value).message().size())),
        8000); // 错误多驻留片刻
}

void status_bar::on_tool_session_started(const QString& tip)
{
    session_tip_ = tip;
    showMessage(tip); // 超时 0 = 常驻,直至会话结束清除
}

void status_bar::on_tool_session_ended(const cbuspp::value<core::view_mode>&)
{
    // 仅当消息区仍显示本会话提示时才清(限时反馈/错误在位则不动)
    if (!session_tip_.isEmpty() && currentMessage() == session_tip_)
        clearMessage();
    session_tip_.clear();
}

}
