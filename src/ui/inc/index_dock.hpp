#pragma once

#include <QDockWidget>
#include <cuuidpp/cuuidpp.hpp>
#include <unordered_map>

#include "ui_protocol.hpp"

class QComboBox;
class QDoubleSpinBox;
class QStackedWidget;
class QTableWidget;
class QWidget;

namespace usip::ui {

class index_dock : public ui_protocol<index_dock, QDockWidget> {
    Q_OBJECT
    friend class ui_protocol<index_dock, QDockWidget>;

public:
    explicit index_dock(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~index_dock() override;

    index_dock(const index_dock&) = delete;
    index_dock& operator=(const index_dock&) = delete;
    index_dock(index_dock&&) = delete;
    index_dock& operator=(index_dock&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    QTableWidget* make_table(QWidget* parent);

private:
    QComboBox* doc_ { nullptr };
    QDoubleSpinBox* x_step_ { nullptr };
    QDoubleSpinBox* y_step_ { nullptr };
    QStackedWidget* stacked_ { nullptr };
    QTableWidget* empty_ { nullptr };

    std::unordered_map<cuuidpp::uuid, QTableWidget*> tables_ { };
};

}
