#include "status_bar.hpp"

namespace usip::ui {

status_bar::status_bar(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

status_bar::~status_bar() = default;

void status_bar::setup_ui()
{
    setSizeGripEnabled(false);
}

void status_bar::setup_subscriptions()
{
}

void status_bar::setup_connections()
{
}

} // namespace usip::ui
