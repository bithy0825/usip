#pragma once

#include <QToolBar>

#include "ui_protocol.hpp"

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

    // 阈值会话开启:禁用其余工具按钮(阈值按钮保持可点,再点即取消)
    void on_threshold_segment_requested();
    // 标注会话开启:同上禁用其余工具按钮
    void on_measure_requested();
    // 工具会话结束(apply/canceled 广播):解禁 + 回落勾选(阻断,不回发事件)
    void on_tool_session_ended();

private:
    QAction* rectangle_ { nullptr };
    QAction* ellipse_ { nullptr };
    QAction* polygon_ { nullptr };

    QAction* threshold_seg_ { nullptr };
    QAction* measure_ { nullptr };
};

}
