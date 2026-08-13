#pragma once

#include <cbuspp/cbuspp.hpp>

#include "executor.hpp"

namespace usip::service {

class file_service {
public:
    explicit file_service(common::executor& executor, cbuspp::bus<common::executor>& bus);
    ~file_service();

    file_service(const file_service&) = delete;
    file_service& operator=(const file_service&) = delete;
    file_service(file_service&&) = delete;
    file_service& operator=(file_service&&) = delete;

private:
    void setup_subscriptions();

    void on_file_open_requested();

private:
    common::executor& executor_;
    cbuspp::bus<common::executor>& bus_;
};

}
