#pragma once

#include <cbuspp/cbuspp.hpp>

#include <filesystem>
#include <memory>

#include "document.hpp"
#include "executor.hpp"

namespace usip::common {

struct loaded_tiff;

}

namespace usip::service {

class document_service {
public:
    document_service(common::executor& executor, cbuspp::bus<common::executor>& bus);
    ~document_service();

    document_service(const document_service&) = delete;
    document_service& operator=(const document_service&) = delete;
    document_service(document_service&&) = delete;
    document_service& operator=(document_service&&) = delete;

private:
    void setup_subscriptions();
    void on_file_selected(const cbuspp::value<std::filesystem::path>& value);

    cbuspp::bus<common::executor>& bus_;
    common::executor& executor_;

    std::unordered_map<cuuidpp::uuid, core::document> docs_ { };
    std::unordered_map<cuuidpp::uuid, core::page> pages_ { };
};

}
