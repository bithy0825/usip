#pragma once

#include <QDockWidget>

#include "ui_protocol.hpp"

class QComboBox;
class QToolButton;
class QWidget;

namespace usip::ui {

class hist_dock : public ui_protocol<hist_dock, QDockWidget> {
    Q_OBJECT
    friend class ui_protocol<hist_dock, QDockWidget>;

public:
    explicit hist_dock(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~hist_dock() override;

    hist_dock(const hist_dock&) = delete;
    hist_dock& operator=(const hist_dock&) = delete;
    hist_dock(hist_dock&&) = delete;
    hist_dock& operator=(hist_dock&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    QToolButton* add_btn_ { nullptr };
    QToolButton* sub_btn_ { nullptr };
    QToolButton* reset_btn_ { nullptr };
    QComboBox* mode_combo_ { nullptr };
    QWidget* hist_view_ { nullptr };
};

}
