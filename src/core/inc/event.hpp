#pragma once

#include <QColor>

#include <cbuspp/cbuspp.hpp>

#include <cstdint>
#include <memory>
#include <utility>

#include "colormap.hpp"
#include "document.hpp"
#include "error.hpp"
#include "tiff.hpp"
#include "view_mode.hpp"

namespace usip::core {

#define CBUSPP_EVENT(NAME, TAG_STRING, VALUE_TYPE) \
    struct NAME : ::cbuspp::event_tag<TAG_STRING, ::cbuspp::value<VALUE_TYPE>> { }

namespace event {

    using mask_range = std::pair<double, double>;

    namespace trace_id {
        constexpr const char* file_service { "file_service" }; // NOLINT(readability-identifier-naming)
        constexpr const char* document_service { "document_service" }; // NOLINT(readability-identifier-naming)
    }

    CBUSPP_EVENT(file_open_requested, "file_open_requested", void);
    CBUSPP_EVENT(file_selected, "file.selected", std::filesystem::path);

    CBUSPP_EVENT(document_ready, "document.ready", std::shared_ptr<document>);
    CBUSPP_EVENT(document_switch, "document.switch", std::shared_ptr<document>);
    CBUSPP_EVENT(document_switch_requested, "document.switch_requested", cuuidpp::uuid);
    CBUSPP_EVENT(page_switch_requested, "page.switch_requested", cuuidpp::uuid);
    CBUSPP_EVENT(page_rois_changed, "page.rois_changed", std::shared_ptr<page>);

    // 视图模式切换(携带目标模式);对比页选择(0 起页序)
    CBUSPP_EVENT(view_mode_changed, "view_mode.changed", view_mode);
    CBUSPP_EVENT(compare_page_selected, "compare_page.selected", int);

    CBUSPP_EVENT(rectangle_draw_requested, "rectangle_draw_requested", void);
    CBUSPP_EVENT(ellipse_draw_requested, "ellipse_draw_requested", void);
    CBUSPP_EVENT(polygon_draw_requested, "polygon_draw_requested", void);
    CBUSPP_EVENT(threshold_segment_requested, "threshold_segment_requested", void);
    CBUSPP_EVENT(measure_requested, "measure_requested", void);
    CBUSPP_EVENT(tool_result_applied, "tool_result.applied", void);
    CBUSPP_EVENT(tool_result_canceled, "tool_result.canceled", void);

    CBUSPP_EVENT(pseudocolor_colormap_changed, "pseudocolor_colormap.changed", colormap_type);
    CBUSPP_EVENT(pseudocolor_zero_is_black_toggled, "pseudocolor_zero_is_black.toggled", bool);
    CBUSPP_EVENT(pseudocolor_enabled_toggled, "pseudocolor_enabled.toggled", bool);

    CBUSPP_EVENT(mask_color_changed, "mask_color.changed", QColor);
    CBUSPP_EVENT(mask_opacity_changed, "mask_opacity.changed", double);
    CBUSPP_EVENT(mask_visible_toggled, "mask_visible.toggled", bool);
    CBUSPP_EVENT(mask_floor_changed, "mask_floor.changed", double);
    CBUSPP_EVENT(mask_ceiling_changed, "mask_ceiling.changed", double);
    CBUSPP_EVENT(mask_range_echo, "mask_range.echo", mask_range);

    CBUSPP_EVENT(measure_line_width_changed, "measure_line_width.changed", int);
    CBUSPP_EVENT(measure_line_color_changed, "measure_line_color.changed", QColor);

    CBUSPP_EVENT(error_occurred, "error.occurred", common::error&);
}

}
