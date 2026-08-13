#pragma once

#include <cbuspp/cbuspp.hpp>

#include "executor.hpp"

#include <utility>

class QWidget;

namespace usip::ui {

template <typename Derived, typename BaseWidget>
class ui_protocol : public BaseWidget {
public:
    explicit ui_protocol(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr)
        : BaseWidget(parent)
        , bus_(bus)
    {
    }

    ui_protocol(const ui_protocol&) = delete;
    ui_protocol& operator=(const ui_protocol&) = delete;
    ui_protocol(ui_protocol&&) = delete;
    ui_protocol& operator=(ui_protocol&&) = delete;

protected:
    template <typename... Args>
    void setup_ui(Args&&... args)
    {
        static_cast<Derived*>(this)->setup_ui(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void setup_subscriptions(Args&&... args)
    {
        static_cast<Derived*>(this)->setup_subscriptions(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void setup_connections(Args&&... args)
    {
        static_cast<Derived*>(this)->setup_connections(std::forward<Args>(args)...);
    }

protected:
    cbuspp::bus<common::executor>& bus_;
};

}