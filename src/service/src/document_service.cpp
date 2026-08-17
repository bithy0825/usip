#include "document_service.hpp"

#include "event.hpp"
#include "logger.hpp"
#include "tiff.hpp"
#include "tiff_loader.hpp"

namespace usip::service {

document_service::document_service(common::executor& executor,
    cbuspp::bus<common::executor>& bus)
    : bus_ { bus }
    , executor_ { executor }
{
    setup_subscriptions();
}

document_service::~document_service() = default;

void document_service::setup_subscriptions()
{
    bus_.on<core::event::file_selected>().call(*this, &document_service::on_file_selected);
}

void document_service::on_file_selected(const cbuspp::value<std::filesystem::path>& value)
{
    const auto& path = *value;

    // 主线程直读:同步完成加载(当前文档唯一,替换即释放旧像素)
    auto loaded = io::load_tiff(path);
    if (!loaded) {
        auto err = loaded.error();
        common::log_error("failed to load '{}': {}", path.string(), err);
        bus_.post<core::event::error_occurred>(cbuspp::value<common::error&> { err })
            .with_trace_id(core::event::trace_id::document_service)
            .sync();
        return;
    }
}

}
