#pragma once

#include <QColor>

#include <cbuspp/cbuspp.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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

    // 视图模式:requested = 切换意图(仅 canvas 订阅并裁决);
    // changed = canvas 裁决后的状态下发(工具栏勾选/侧边栏模式轴/对比页输入随此)
    CBUSPP_EVENT(view_mode_change_requested, "view_mode.change_requested", view_mode);
    CBUSPP_EVENT(view_mode_changed, "view_mode.changed", view_mode);
    // 对比页选择(0 起页序)
    CBUSPP_EVENT(compare_page_selected, "compare_page.selected", int);

    CBUSPP_EVENT(rectangle_draw_requested, "rectangle_draw_requested", void);
    CBUSPP_EVENT(ellipse_draw_requested, "ellipse_draw_requested", void);
    CBUSPP_EVENT(polygon_draw_requested, "polygon_draw_requested", void);
    CBUSPP_EVENT(threshold_segment_requested, "threshold_segment_requested", void);
    CBUSPP_EVENT(measure_requested, "measure_requested", void);
    CBUSPP_EVENT(tool_result_applied, "tool_result.applied", void);
    CBUSPP_EVENT(tool_result_canceled, "tool_result.canceled", void);

    // 工具会话结束(canvas 处理完 apply/cancel 后统一广播,含拒绝路径的回落;
    // 携带当前视图模式,各模块据模式自行推导解禁面 —— 如对比三模式下阈值分割保持禁用)
    CBUSPP_EVENT(tool_session_ended, "tool_session.ended", view_mode);

    CBUSPP_EVENT(pseudocolor_colormap_changed, "pseudocolor_colormap.changed", colormap_type);
    CBUSPP_EVENT(pseudocolor_zero_is_black_toggled, "pseudocolor_zero_is_black.toggled", bool);
    CBUSPP_EVENT(pseudocolor_enabled_toggled, "pseudocolor_enabled.toggled", bool);

CBUSPP_EVENT(mask_color_changed, "mask_color.changed", QColor);
CBUSPP_EVENT(mask_opacity_changed, "mask_opacity.changed", double);
CBUSPP_EVENT(mask_visible_toggled, "mask_visible.toggled", bool);
CBUSPP_EVENT(mask_floor_changed, "mask_floor.changed", double);
CBUSPP_EVENT(mask_ceiling_changed, "mask_ceiling.changed", double);
CBUSPP_EVENT(mask_range_echo, "mask_range.echo", mask_range);

// L4/L5 层可见开关(五模式皆受控;非 single 模式本身只渲染两页公有项)
CBUSPP_EVENT(roi_visible_toggled, "roi_visible.toggled", bool);
CBUSPP_EVENT(annotation_visible_toggled, "annotation_visible.toggled", bool);

    CBUSPP_EVENT(measure_line_width_changed, "measure_line_width.changed", int);
    CBUSPP_EVENT(measure_line_color_changed, "measure_line_color.changed", QColor);

    // 采集步长(mm/像素,逐轴):写 document.step,使当前文档全部页的标注失效
    CBUSPP_EVENT(step_x_changed, "step.x.changed", double);
    CBUSPP_EVENT(step_y_changed, "step.y.changed", double);
    // 清除主、副两页的全部标注数据(含因不一致未渲染的;菜单 Clear Measurements)
    CBUSPP_EVENT(measurements_clear_requested, "measurements.clear_requested", void);
    // 清除主、副两页的全部选区数据(菜单 Clear Constituency)
    CBUSPP_EVENT(rois_clear_requested, "rois.clear_requested", void);
    // 选区删除(infodock 行右键):canvas 裁决执行(擦除 page.rois 项)并重绘;
    // 表格删行/重编号由高亮随选择联动,均经既有 page_rois_changed 收口
    CBUSPP_EVENT(roi_delete_requested, "roi.delete_requested", roi_ref);
    // infodock 行选中 → 画布渲染期对该选区加内部蒙版(仅渲染态,不落数据);
    // nullopt = 清除高亮
    CBUSPP_EVENT(roi_highlight_changed, "roi.highlight_changed", std::optional<roi_ref>);
    // 光标像素取样(canvas 鼠标移动广播;status_bar 右下显示):nullopt = 越界/无页
    CBUSPP_EVENT(pixel_sample_changed, "pixel_sample.changed", std::optional<pixel_sample>);
    // 状态栏左下短暂提示(保存/导出结果等非错误反馈;错误仍走 error_occurred)
    CBUSPP_EVENT(status_message, "status.message", std::string);

    CBUSPP_EVENT(error_occurred, "error.occurred", common::error&);
}

}
