#include "index_dock.hpp"
#include "icon_registry.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QTableWidget>

namespace usip::ui {

index_dock::index_dock(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

index_dock::~index_dock() = default;

void index_dock::setup_ui()
{
    setWindowTitle(tr("File"));
    setWindowIcon(icon_registry::instance().icon("scene_collection").value_or(QIcon()));
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* container = new QWidget(this);
    doc_ = new QComboBox(container);
    x_step_ = new QDoubleSpinBox(container);
    y_step_ = new QDoubleSpinBox(container);
    stacked_ = new QStackedWidget(container);
    empty_ = make_table(container);
    stacked_->addWidget(empty_);
    stacked_->setCurrentWidget(empty_);
    stacked_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QGridLayout(container);
    layout->addWidget(new QLabel(tr("Doc:")), 0, 0, 1, 1);
    layout->addWidget(doc_, 0, 1, 1, 3);
    layout->addWidget(new QLabel(tr("X Step:")), 1, 0);
    layout->addWidget(x_step_, 1, 1);
    layout->addWidget(new QLabel(tr("Y Step:")), 1, 2);
    layout->addWidget(y_step_, 1, 3);
    layout->addWidget(stacked_, 2, 0, 1, 4);

    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 0);
    layout->setRowStretch(2, 1);
    layout->setColumnStretch(0, 0);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 0);
    layout->setColumnStretch(3, 1);
    setWidget(container);
}

void index_dock::setup_subscriptions()
{
}

void index_dock::setup_connections()
{
}

QTableWidget* index_dock::make_table(QWidget* parent)
{
    QTableWidget* table = new QTableWidget(parent);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({ tr("Index"), tr("Num_ROI"), tr("Num_Pixel"), tr("Mean"), tr("Std") });
    return table;
}

}
