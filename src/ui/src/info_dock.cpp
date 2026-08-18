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
    table->setMinimumWidth(520);
    table->setColumnCount(11);
    table->setHorizontalHeaderLabels({ tr("Index"), tr("Numer"), tr("Floor"), tr("Ceil"), tr("Pixel"),
        tr("Valid"), tr("Percent"), tr("Mean"), tr("Std"), tr("Min"), tr("Max") });

    QHeaderView* header = table->horizontalHeader();

    QList<int> fixedCols = { 0, 1, 2, 3, 6, 7, 8, 9, 10 };
    for (int col : fixedCols) {
        table->resizeColumnToContents(col); // 此时无数据，仅依据表头标签
        header->setSectionResizeMode(col, QHeaderView::Fixed); // 锁定宽度，不再变化
    }

    header->setSectionResizeMode(4, QHeaderView::Stretch);
    header->setSectionResizeMode(5, QHeaderView::Stretch);

    header->setStretchLastSection(false);
    return table;
}

}
