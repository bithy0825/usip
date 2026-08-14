#pragma once

#include <QToolBar>

#include "ui_protocol.hpp"

class QAction;
class QActionGroup;
class QSpinBox;
class QStackedWidget;

namespace usip::ui {

class menu_bar;

class options_tool_bar : public ui_protocol<options_tool_bar, QToolBar> {
    Q_OBJECT
    friend class ui_protocol<options_tool_bar, QToolBar>;

public:
    explicit options_tool_bar(menu_bar& menu, cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~options_tool_bar() override;

    options_tool_bar(const options_tool_bar&) = delete;
    options_tool_bar& operator=(const options_tool_bar&) = delete;
    options_tool_bar(options_tool_bar&&) = delete;
    options_tool_bar& operator=(options_tool_bar&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    menu_bar& menu_bar_;

    // 对比视图模式(互斥 radio group,None = 单图视图)
    QActionGroup* view_group_ { nullptr };
    QAction* view_none_ { nullptr };
    QAction* view_split_ { nullptr };
    QAction* view_slider_ { nullptr };
    QAction* view_highlight_ { nullptr };
    QAction* view_difference_ { nullptr };

    // 页帧控制
    QSpinBox* page_control_ { nullptr };

    // 上下文选项面板(根据当前工具显示不同参数:mask→颜色/透明度,measure→线宽等)
    QStackedWidget* options_stack_ { nullptr };
};

} // namespace usip::ui
