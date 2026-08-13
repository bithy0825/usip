#include "service.hpp"
#include "file_service.hpp"

namespace usip::service {

service::service(common::executor& executor, cbuspp::bus<common::executor>& bus)
    : executor_(executor)
    , bus_(bus)
{
}

service::~service() = default;

result<void> service::start()
{
    return common::capture([&] {
        file_service_ = std::make_unique<file_service>(executor_, bus_);
    });
}

void service::stop()
{
    file_service_.reset();
}

}
