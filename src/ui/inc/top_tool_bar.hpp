#pragma once

#include <QToolBar>
#include <QToolButton>

#include "ui_protocol.hpp"

class QComboBox;
class QCheckBox;

namespace usip::ui {

class menu_bar;

class top_tool_bar : public ui_protocol<top_tool_bar, QToolBar> {
    Q_OBJECT
    friend class ui_protocol<top_tool_bar, QToolBar>;

public:
    explicit top_tool_bar(menu_bar& menu, cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~top_tool_bar() override;

    top_tool_bar(const top_tool_bar&) = delete;
    top_tool_bar& operator=(const top_tool_bar&) = delete;
    top_tool_bar(top_tool_bar&&) = delete;
    top_tool_bar& operator=(top_tool_bar&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    menu_bar& menu_bar_;

    // colormap 色条按钮(弹簧与 about 之间;点击弹菜单,无下拉三角)
    QToolButton* colormap_button_ { nullptr };
};

} // namespace usip::ui
