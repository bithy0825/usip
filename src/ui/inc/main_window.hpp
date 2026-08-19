#pragma once

#include <QMainWindow>

#include "ui_protocol.hpp"

namespace usip::ui {

class menu_bar;
class top_tool_bar;
class options_tool_bar;
class side_tool_bar;
class index_dock;
class hist_dock;
class info_dock;
class canvas;
class status_bar;

class main_window : public ui_protocol<main_window, QMainWindow> {
    Q_OBJECT
    friend class ui_protocol<main_window, QMainWindow>;

public:
    explicit main_window(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~main_window() override;

    main_window(const main_window&) = delete;
    main_window& operator=(const main_window&) = delete;
    main_window(main_window&&) = delete;
    main_window& operator=(main_window&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();

private:
    menu_bar* menu_bar_ { nullptr };
    top_tool_bar* top_tool_bar_ { nullptr };
    options_tool_bar* options_tool_bar_ { nullptr };
    side_tool_bar* side_tool_bar_ { nullptr };
    index_dock* index_dock_ { nullptr };
    hist_dock* hist_dock_ { nullptr };
    info_dock* info_dock_ { nullptr };
    canvas* canvas_ { nullptr };
    status_bar* status_bar_ { nullptr };
};

}
