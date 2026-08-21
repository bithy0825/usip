#pragma once

#include <QToolBar>

#include "ui_protocol.hpp"
#include "view_mode.hpp"

namespace usip::ui {

class side_tool_bar : public ui_protocol<side_tool_bar, QToolBar> {
    Q_OBJECT
    friend class ui_protocol<side_tool_bar, QToolBar>;

public:
    explicit side_tool_bar(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~side_tool_bar() override;

    side_tool_bar(const side_tool_bar&) = delete;
    side_tool_bar& operator=(const side_tool_bar&) = delete;
    side_tool_bar(side_tool_bar&&) = delete;
    side_tool_bar& operator=(side_tool_bar&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    // 工具会话开启:会话排他(其余工具禁用)
    void on_threshold_segment_requested();
    void on_measure_requested();
    void on_rectangle_draw_requested();
    void on_ellipse_draw_requested();
    void on_polygon_draw_requested();
    // 会话结束(canvas 广播,携带模式):解禁 + 回落勾选(阻断,不回发事件)
    void on_tool_session_ended(const cbuspp::value<core::view_mode>& value);
    // 模式互斥:对比三模式禁用阈值分割(本地状态,随模式事件推导)
    void on_view_mode_changed(const cbuspp::value<core::view_mode>& value);

    // 双轴推导启用面:会话排他(其余工具禁)+ 模式互斥(对比三模式禁阈值);
    // 当前勾选的工具保持可点(再点即取消)
    void refresh_enables();

private:
    QAction* rectangle_ { nullptr };
    QAction* ellipse_ { nullptr };
    QAction* polygon_ { nullptr };

    QAction* threshold_seg_ { nullptr };
    QAction* measure_ { nullptr };

    core::view_mode mode_ { core::view_mode::single }; // 模式轴(随 view_mode_changed)
    bool session_active_ { false }; // 会话轴(随 request/ended)
};

}
