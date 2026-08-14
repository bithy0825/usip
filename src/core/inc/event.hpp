#pragma once

#include <cbuspp/cbuspp.hpp>

#include "error.hpp"

namespace usip::core {

#define CBUSPP_EVENT(NAME, TAG_STRING, VALUE_TYPE) \
    struct NAME : ::cbuspp::event_tag<TAG_STRING, ::cbuspp::value<VALUE_TYPE>> { }

namespace event {

    namespace trace_id {
        constexpr const char* file_service { "file_service" }; // NOLINT(readability-identifier-naming)
    }

    CBUSPP_EVENT(file_open_requested, "file_open_requested", void);
    CBUSPP_EVENT(file_selected, "file.selected", std::filesystem::path);

    CBUSPP_EVENT(threshold_segment_requested, "threshold_segment_requested", void);

    CBUSPP_EVENT(error_occurred, "error.occurred", common::error&);
}

}
