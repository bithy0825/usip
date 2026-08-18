#pragma once

#include <QStatusBar>

#include "ui_protocol.hpp"

namespace usip::ui {

class status_bar : public ui_protocol<status_bar, QStatusBar> {
    Q_OBJECT
    friend class ui_protocol<status_bar, QStatusBar>;

public:
    explicit status_bar(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~status_bar() override;

    status_bar(const status_bar&) = delete;
    status_bar& operator=(const status_bar&) = delete;
    status_bar(status_bar&&) = delete;
    status_bar& operator=(status_bar&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
};

}
