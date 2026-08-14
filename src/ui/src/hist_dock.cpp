#include "hist_dock.hpp"
#include "icon_registry.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

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

    auto* btn_row = new QHBoxLayout;
    // btn_row->setContentsMargins(2, 2, 2, 2);
    // btn_row->setSpacing(2);
    btn_row->addWidget(add_btn_);
    btn_row->addWidget(sub_btn_);
    btn_row->addWidget(reset_btn_);
    btn_row->addStretch();
    btn_row->addWidget(mode_combo_);

    hist_view_ = new QWidget(container);
    hist_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(btn_row);
    layout->addWidget(hist_view_, 1);

    setWidget(container);
}

void hist_dock::setup_subscriptions() { }

void hist_dock::setup_connections() { }

} // namespace usip::ui
