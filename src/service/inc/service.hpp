#pragma once

#include <cbuspp/cbuspp.hpp>

#include "error.hpp"
#include "executor.hpp"


namespace usip::service {

class document_service;
class file_service;

class service {
public:
    explicit service(common::executor& executor, cbuspp::bus<common::executor>& bus);
    ~service();

    service(const service&) = delete;
    service& operator=(const service&) = delete;
    service(service&&) = delete;
    service& operator=(service&&) = delete;

    result<void> start();
    void stop();

private:
    common::executor& executor_;
    cbuspp::bus<common::executor>& bus_;

    std::unique_ptr<file_service> file_service_ { nullptr };
    std::unique_ptr<document_service> document_service_ { nullptr };
};

}