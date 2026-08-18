#include "hist_dock.hpp"
#include "icon_registry.hpp"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QToolButton>

namespace usip::ui {

hist_dock::hist_dock(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

hist_dock::~hist_dock() = default;

void hist_dock::setup_ui()
{
    setWindowTitle(tr("Histogram"));
    setWindowIcon(icon_registry::instance().icon("histogram").value_or(QIcon()));
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* container = new QWidget(this);

    // ── 按钮行 ───────────────────────────────────────────────────────
    add_btn_ = new QToolButton(container);
    add_btn_->setText("+");
    add_btn_->setToolTip(tr("Add"));

    sub_btn_ = new QToolButton(container);
    sub_btn_->setText("−");
    sub_btn_->setToolTip(tr("Subtract"));

    reset_btn_ = new QToolButton(container);
    reset_btn_->setText(tr("Reset"));

    mode_combo_ = new QComboBox(container);
    mode_combo_->addItem(tr("Count"));
    mode_combo_->addItem(tr("Percent"));

    hist_view_ = new QWidget(container);
    hist_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QGridLayout(container);
    layout->addWidget(add_btn_, 0, 0);
    layout->addWidget(sub_btn_, 0, 1);
    layout->addWidget(reset_btn_, 0, 2);
    layout->addWidget(mode_combo_, 0, 4);
    layout->addWidget(hist_view_, 1, 0, 1, 5);

    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 1);
    layout->setColumnStretch(0, 0);
    layout->setColumnStretch(1, 0);
    layout->setColumnStretch(2, 0);
    layout->setColumnStretch(3, 1);
    layout->setColumnStretch(4, 0);

    setWidget(container);
}

void hist_dock::setup_subscriptions() { }

void hist_dock::setup_connections() { }

} // namespace usip::ui
