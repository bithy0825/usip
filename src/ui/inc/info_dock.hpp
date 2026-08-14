#pragma once

#include <QDockWidget>
#include <cuuidpp/cuuidpp.hpp>
#include <unordered_map>

#include "ui_protocol.hpp"

class QStackedWidget;
class QTableWidget;
class QWidget;

namespace usip::ui {

class info_dock : public ui_protocol<info_dock, QDockWidget> {
    Q_OBJECT
    friend class ui_protocol<info_dock, QDockWidget>;

public:
    explicit info_dock(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~info_dock() override;

    info_dock(const info_dock&) = delete;
    info_dock& operator=(const info_dock&) = delete;
    info_dock(info_dock&&) = delete;
    info_dock& operator=(info_dock&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    QTableWidget* make_table(QWidget* parent);

private:
    QStackedWidget* stacked_ { nullptr };
    QTableWidget* empty_ { nullptr };

    std::unordered_map<cuuidpp::uuid, QTableWidget*> tables_ { };
};

}
