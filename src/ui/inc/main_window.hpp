#pragma once

#include <QMainWindow>

#include "ui_protocol.hpp"

namespace usip::ui {

class menu_bar;

class main_window : public ui_protocol<main_window, QMainWindow> {
    Q_OBJECT
    friend class ui_protocol<main_window, QMainWindow>;

public:
    explicit main_window(
        cbuspp::bus<common::executor>& bus,
        QWidget* parent = nullptr);
    ~main_window() override;

private:
    void setup_ui();
    void setup_subscriptions();

private:
    menu_bar* menu_bar_ { nullptr };
};

}