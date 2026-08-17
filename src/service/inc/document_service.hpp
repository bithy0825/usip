#pragma once

#include <cbuspp/cbuspp.hpp>

#include <filesystem>
#include <memory>

#include "executor.hpp"

namespace usip::common {

struct loaded_tiff;

}

namespace usip::service {

class document_service {
public:
    explicit document_service(common::executor& executor, cbuspp::bus<common::executor>& bus);
    ~document_service();

    document_service(const document_service&) = delete;
    document_service& operator=(const document_service&) = delete;
    document_service(document_service&&) = delete;
    document_service& operator=(document_service&&) = delete;

    // 当前文档(尚未加载过 = nullptr);像素经 shared_ptr 共享,不发生拷贝
    [[nodiscard]] auto current() const noexcept -> std::shared_ptr<common::loaded_tiff>;

private:
    void setup_subscriptions();
    void on_file_selected(const cbuspp::value<std::filesystem::path>& value);

    // 加载会话:后台任务持 weak_ptr,服务析构后过期任务自行失效
    // (app 关停顺序:service 先于 executor 销毁,任务不可捕获 this)
    struct session;
    std::shared_ptr<session> session_ { nullptr };

    common::executor& executor_;
};

}
