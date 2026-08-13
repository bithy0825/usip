#include "main_window.hpp"
#include "menu_bar.hpp"

namespace usip::ui {

main_window::main_window(
    cbuspp::bus<common::executor>& bus,
    QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
}

main_window::~main_window() = default;

void main_window::setup_ui()
{
    menu_bar_ = new menu_bar(bus_, this);
    setMenuBar(menu_bar_);
}

void main_window::setup_subscriptions() { }

}
