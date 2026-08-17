#pragma once

#include <cbuspp/cbuspp.hpp>

#include <memory>

#include "document.hpp"
#include "error.hpp"
#include "tiff.hpp"

namespace usip::core {

#define CBUSPP_EVENT(NAME, TAG_STRING, VALUE_TYPE) \
    struct NAME : ::cbuspp::event_tag<TAG_STRING, ::cbuspp::value<VALUE_TYPE>> { }

namespace event {

    namespace trace_id {
        constexpr const char* file_service { "file_service" }; // NOLINT(readability-identifier-naming)
        constexpr const char* document_service { "document_service" }; // NOLINT(readability-identifier-naming)
    }

    CBUSPP_EVENT(file_open_requested, "file_open_requested", void);
    CBUSPP_EVENT(file_selected, "file.selected", std::filesystem::path);

    CBUSPP_EVENT(document_ready, "document.ready", std::shared_ptr<document>);
    CBUSPP_EVENT(document_switch, "document.switch", std::shared_ptr<document>);

    CBUSPP_EVENT(threshold_segment_requested, "threshold_segment_requested", void);

    CBUSPP_EVENT(error_occurred, "error.occurred", common::error&);
}

}
