#include "info_dock.hpp"
#include "icon_registry.hpp"
#include "index_dock.hpp"

#include <QGridLayout>
#include <QHeaderView>
#include <QStackedWidget>
#include <QTableWidget>


namespace usip::ui {

info_dock::info_dock(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

info_dock::~info_dock() = default;

void info_dock::setup_ui()
{
    setWindowTitle(tr("Info"));
    setWindowIcon(icon_registry::instance().icon("info").value_or(QIcon()));
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* container = new QWidget(this);
    stacked_ = new QStackedWidget(container);
    empty_ = make_table(container);
    stacked_->addWidget(empty_);
    stacked_->setCurrentWidget(empty_);
    stacked_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QGridLayout(container);
    layout->addWidget(stacked_, 0, 0, 1, 1);
    layout->setRowStretch(0, 1);
    layout->setColumnStretch(0, 1);
    setWidget(container);
}

void info_dock::setup_subscriptions()
{
}

void info_dock::setup_connections()
{
}

QTableWidget* info_dock::make_table(QWidget* parent)
{
    auto* table = new QTableWidget(parent);
    table->setColumnCount(11);
    table->setHorizontalHeaderLabels({ tr("Index"), tr("Number"), tr("floor"), tr("ceil"), tr("Num_ROI"),
        tr("Num_Pixel"), tr("Num_Valid"), tr("Mean"), tr("Std"), tr("Min"), tr("Max") });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return table;
}

}
