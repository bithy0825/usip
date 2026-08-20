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
    void on_document_switch_requested(const cbuspp::value<cuuidpp::uuid>& value);
    void on_page_switch_requested(const cbuspp::value<cuuidpp::uuid>& value);

    cbuspp::bus<common::executor>& bus_;
    common::executor& executor_;

    // 文档由共享指针持有:事件的订阅方(canvas 等)以 weak_ptr 引用,
    // 控制块常驻 docs_,weak 语义才成立(空删除器别名会随事件结束失效)
    std::unordered_map<cuuidpp::uuid, std::shared_ptr<core::document>> docs_ { };
    std::unordered_map<cuuidpp::uuid, core::page> pages_ { };
};

}
