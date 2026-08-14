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

private:
    QAction* rectangle_ { nullptr };
    QAction* ellipse_ { nullptr };
    QAction* polygon_ { nullptr };

    QAction* mask_ { nullptr };
    QAction* measure_ { nullptr };
};

}
