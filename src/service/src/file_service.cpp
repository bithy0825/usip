#include "file_service.hpp"
#include "config.hpp"
#include "event.hpp"
#include "platform/system.hpp"
#include "utility.hpp"

#include <array>

namespace usip::service {

file_service::file_service(common::executor& executor, cbuspp::bus<common::executor>& bus)
    : executor_(executor)
    , bus_(bus)
{
    setup_subscriptions();
}

file_service::~file_service() = default;

void file_service::setup_subscriptions()
{
    bus_.on<core::event::file_open_requested>().call(*this, &file_service::on_file_open_requested);
}

void file_service::on_file_open_requested()
{
    const auto recent_files = core::config::global()->get<std::vector<std::string>>("file.recent_files");
    const auto last_path = recent_files.empty()
        ? std::filesystem::path { }
        : common::path_from_utf8(recent_files.front());

    std::array filters {
        platform::file_filter {
            .description = "TIFF Images",
            .pattern = "*.tif;*.tiff",
        },
    };

    const auto initial_filename = common::path_to_utf8(last_path.filename());

    platform::file_dialog_desc desc {
        .title = "Select a TIFF image",
        .initial_path = last_path.parent_path(),
        .initial_filename = initial_filename,
        .initial_extension = "tif",
        .filters = filters,
        .options = platform::dialog_option::path_must_exist
            | platform::dialog_option::file_must_exist,
    };

    auto result = platform::system::show_file_dialog(
        platform::dialog_type::open_file, desc);

    if (!result) {
        if (result.error().code() != common::errc::cancelled) {
            auto err = result.error();
            bus_.post<core::event::error_occurred>(
                    cbuspp::value<common::error&> { err })
                .sync();
        }
        return;
    }

    if (result.value().empty()) {
        return;
    }

    const auto& path = result.value().front();
    bus_.post<core::event::file_selected>(
            cbuspp::value<std::filesystem::path> { path })
        .with_trace_id(core::event::trace_id::file_service)
        .sync();
}

}
