// ==============================================================================
// canvas.cpp — 画布:事件订阅与状态管理;paintEvent 按模式组织层调用与几何;
// 滚轮锚点缩放 / 右键平移;对比页选取与校验
// ==============================================================================

#include "canvas.hpp"

#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QSlider>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <utility>

#include "config.hpp"
#include "event.hpp"

namespace usip::ui {
namespace {

    // 构造时从 config 播种 options(部分键;未注册的保留结构默认)
    [[nodiscard]] auto snapshot_options() -> options
    {
        const auto* cfg = core::config::global();

        options opts;
        opts.pseudocolor_enabled = cfg->get<bool>("pseudocolor.enabled"); // 未注册 → false
        opts.pseudocolor_colormap = core::colormap_from_string(
            cfg->get<std::string>("pseudocolor.colormap"))
                                        .value_or(core::colormap_type::jet);
        opts.zero_is_black = cfg->get<bool>("pseudocolor.zero_is_black");
        opts.mask_color = QColor(QString::fromStdString(cfg->get<std::string>("mask.color")));
        opts.mask_opacity = cfg->get<double>("mask.opacity");
        opts.line_width = cfg->get<int>("measure.line_width");
        opts.line_color
            = QColor(QString::fromStdString(cfg->get<std::string>("measure.line_color")));
        return opts;
    }

} // namespace

canvas::canvas(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
    , options_(snapshot_options())
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

canvas::~canvas() = default;

void canvas::setup_ui()
{
    setMinimumSize(200, 200); // 防止 dock 挤压成零尺寸
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus); // 框选会话的数字键 1-4 需键盘焦点

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 100);
    slider_->setValue(50);
    slider_->hide(); // 仅 slider 模式可见

    ants_timer_ = new QTimer(this); // 蚂蚁线相位(仅动画可见时重绘,见 setup_connections)
    ants_timer_->setInterval(100);
    ants_timer_->start();
}

void canvas::setup_subscriptions()
{
    bus_.on<core::event::document_ready>().call(*this, &canvas::on_document_ready);
    bus_.on<core::event::document_switch>().call(*this, &canvas::on_document_switch);
    bus_.on<core::event::view_mode_change_requested>()
        .call(*this, &canvas::on_view_mode_change_requested);
    bus_.on<core::event::compare_page_selected>()
        .call(*this, &canvas::on_compare_page_selected);
    bus_.on<core::event::pseudocolor_enabled_toggled>()
        .call(*this, &canvas::on_pseudocolor_enabled_toggled);
    bus_.on<core::event::pseudocolor_colormap_changed>()
        .call(*this, &canvas::on_pseudocolor_colormap_changed);
    bus_.on<core::event::pseudocolor_zero_is_black_toggled>()
        .call(*this, &canvas::on_pseudocolor_zero_is_black_toggled);
    bus_.on<core::event::mask_visible_toggled>().call(*this, &canvas::on_mask_visible_toggled);
    bus_.on<core::event::roi_visible_toggled>().call(*this, &canvas::on_roi_visible_toggled);
    bus_.on<core::event::annotation_visible_toggled>()
        .call(*this, &canvas::on_annotation_visible_toggled);
    bus_.on<core::event::mask_color_changed>().call(*this, &canvas::on_mask_color_changed);
    bus_.on<core::event::mask_opacity_changed>().call(*this, &canvas::on_mask_opacity_changed);
    bus_.on<core::event::mask_floor_changed>().call(*this, &canvas::on_mask_floor_changed);
    bus_.on<core::event::mask_ceiling_changed>().call(*this, &canvas::on_mask_ceiling_changed);
    bus_.on<core::event::threshold_segment_requested>()
        .call(*this, &canvas::on_threshold_segment_requested);
    bus_.on<core::event::tool_result_applied>().call(*this, &canvas::on_tool_result_applied);
    bus_.on<core::event::tool_result_canceled>().call(*this, &canvas::on_tool_result_canceled);
    bus_.on<core::event::measure_requested>().call(*this, &canvas::on_measure_requested);
    bus_.on<core::event::rectangle_draw_requested>()
        .call(*this, &canvas::on_rectangle_draw_requested);
    bus_.on<core::event::ellipse_draw_requested>().call(*this, &canvas::on_ellipse_draw_requested);
    bus_.on<core::event::polygon_draw_requested>().call(*this, &canvas::on_polygon_draw_requested);
    bus_.on<core::event::rois_clear_requested>().call(*this, &canvas::on_rois_clear_requested);
    bus_.on<core::event::roi_delete_requested>().call(*this, &canvas::on_roi_delete_requested);
    bus_.on<core::event::roi_highlight_changed>()
        .call(*this, &canvas::on_roi_highlight_changed);
    bus_.on<core::event::step_x_changed>().call(*this, &canvas::on_step_x_changed);
    bus_.on<core::event::step_y_changed>().call(*this, &canvas::on_step_y_changed);
    bus_.on<core::event::measurements_clear_requested>()
        .call(*this, &canvas::on_measurements_clear_requested);
    bus_.on<core::event::measure_line_width_changed>()
        .call(*this, &canvas::on_measure_line_width_changed);
    bus_.on<core::event::measure_line_color_changed>()
        .call(*this, &canvas::on_measure_line_color_changed);
}

void canvas::setup_connections()
{
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        view_.split = value / 100.0; // 缝是视图态,只动 clip 不动内容
        update();
    });
    connect(ants_timer_, &QTimer::timeout, this, [this] {
        ants_offset_ = (ants_offset_ + 1) % ants_offset_cycle;
        if (ants_animated())
            update();
    });
}

// ─── 总线回调 ─────────────────────────────────────────────────────────────────

void canvas::on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc)
        return;
    cancel_threshold_session(); // 会话属于旧页:丢弃(内部自判活跃)
    cancel_annotation_session();
    cancel_roi_session();
    doc_ = doc;
    resolve_pages();
    // 页/文档变 → 全部层缓存重建(L5/L6 实现时同清)
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { };
    l3_img_ = { };
    l6_img_ = { };
    l6c_img_ = { };
    view_dirty_ = true; // 新文档:延迟到尺寸有效时适配居中
    // 对比模式下新页无有效对比页 → 询问或回退 single(工具栏经订阅回同步)
    if (options_.mode != core::view_mode::single && compare_page_.expired()
        && !ensure_compare_page()) {
        options_.mode = core::view_mode::single;
        slider_->hide();
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { core::view_mode::single })
            .sync();
    }
    update();
}

void canvas::on_document_switch(
    const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc) // 空载荷(如重复打开的提醒)不切
        return;
    cancel_threshold_session(); // 页切换亦经本事件广播:会话属于旧页
    cancel_annotation_session();
    cancel_roi_session();
    doc_ = doc;
    resolve_pages();
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { };
    l3_img_ = { };
    l6_img_ = { };
    l6c_img_ = { };
    view_dirty_ = true;
    if (options_.mode != core::view_mode::single && compare_page_.expired()
        && !ensure_compare_page()) {
        options_.mode = core::view_mode::single;
        slider_->hide();
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { core::view_mode::single })
            .sync();
    }
    update();
}

void canvas::on_view_mode_change_requested(const cbuspp::value<core::view_mode>& value)
{
    const auto mode = *value;
    if (mode == options_.mode)
        return; // 同值请求:无状态变化(勾选本就在位)

    // 进对比模式前:须存在已校验的对比页(未设弹对话框);
    // 取消/失败 → 不下发新模式,仅以下发"当前模式"覆写工具栏的乐观勾选
    if (mode != core::view_mode::single && !page_.expired() && !ensure_compare_page()) {
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { options_.mode })
            .sync();
        return;
    }

    options_.mode = mode;
    // 对比三式(slider/highlight/difference)禁用 mask 工具:会话中则取消
    if (mode != core::view_mode::single && mode != core::view_mode::split)
        cancel_threshold_session();
    slider_->setVisible(mode == core::view_mode::slider);
    l2_img_ = { }; // 运算层内容随模式变
    view_dirty_ = true; // 视口定义变(整视口 ↔ 半区),重新适配
    // 状态下发:勾选态/模式轴/对比页输入统一随此(请求事件仅本类订阅,无重入覆盖)
    bus_.post<core::event::view_mode_changed>(
        cbuspp::value<core::view_mode> { options_.mode })
        .sync();
    update();
}

void canvas::on_compare_page_selected(const cbuspp::value<int>& value)
{
    const auto page = page_.lock();
    const auto doc = doc_.lock();
    if (!page || !doc)
        return;
    const auto idx = *value; // 0 起页序
    if (idx < 0 || std::cmp_greater_equal(idx, doc->info.pages.size()))
        return;

    page->compare_to = doc->info.pages[static_cast<std::size_t>(idx)].id; // 显式选择,写入页
    cancel_threshold_session(); // 会话掩膜属于旧主副配对,换了副图须重来
    l1c_img_ = { };
    l2_img_ = { };
    // 已在对比模式:立刻校验新对比页,失败回退 single
    if (options_.mode != core::view_mode::single && !validate_compare()) {
        options_.mode = core::view_mode::single;
        slider_->hide();
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { core::view_mode::single })
            .sync();
    }
    update();
}

void canvas::on_pseudocolor_enabled_toggled(const cbuspp::value<bool>& value)
{
    options_.pseudocolor_enabled = *value;
    l1_img_ = { };
    l1c_img_ = { };
    update();
}

void canvas::on_pseudocolor_colormap_changed(const cbuspp::value<core::colormap_type>& value)
{
    options_.pseudocolor_colormap = *value;
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { }; // difference 的 diff LUT 由所选 colormap 派生
    update();
}

void canvas::on_pseudocolor_zero_is_black_toggled(const cbuspp::value<bool>& value)
{
    options_.zero_is_black = *value;
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { };
    update();
}

void canvas::on_mask_visible_toggled(const cbuspp::value<bool>& value)
{
    options_.mask_enabled = *value; // 可见性不动 L3 缓存(重开时原图直接用)
    update();
}

void canvas::on_roi_visible_toggled(const cbuspp::value<bool>& value)
{
    options_.roi_visible = *value; // 可见性不动数据,矢量直绘直接生效
    update();
}

void canvas::on_annotation_visible_toggled(const cbuspp::value<bool>& value)
{
    options_.annotation_visible = *value;
    update();
}

void canvas::on_mask_color_changed(const cbuspp::value<QColor>& value)
{
    options_.mask_color = *value;
    l3_img_ = { };
    l6_img_ = { }; // 临时层同换色
    l6c_img_ = { };
    update();
}

void canvas::on_mask_opacity_changed(const cbuspp::value<double>& value)
{
    options_.mask_opacity = *value;
    l3_img_ = { };
    l6_img_ = { };
    l6c_img_ = { };
    update();
}

void canvas::on_mask_floor_changed(const cbuspp::value<double>& value)
{
    if (!threshold_tool_.active()) // 阈值只属于会话;数据仅经 apply 落盘
        return;
    auto range = threshold_tool_.range();
    range.first = *value;
    threshold_tool_.set_range(range);
    l6_img_ = { };
    l6c_img_ = { };
    update();
}

void canvas::on_mask_ceiling_changed(const cbuspp::value<double>& value)
{
    if (!threshold_tool_.active())
        return;
    auto range = threshold_tool_.range();
    range.second = *value;
    threshold_tool_.set_range(range);
    l6_img_ = { };
    l6c_img_ = { };
    update();
}

// ─── 阈值分割工具(canvas 编排)───────────────────────────────────────────────

void canvas::on_threshold_segment_requested()
{
    const auto page = page_.lock();
    if (!page) { // 无页:无从分割,按钮回落
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }

    // 会话排他:任一工具运行中不得开新工具(按钮回落)
    if (threshold_tool_.active() || annotation_tool_.active() || roi_tool_.active()) {
        auto err = common::error::make(common::errc::unavailable,
            "another tool session is active");
        post_error(err);
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }

    // slider/highlight/difference 禁用 mask 工具
    if (options_.mode != core::view_mode::single && options_.mode != core::view_mode::split) {
        auto err = common::error::make(common::errc::validation_failed,
            "threshold segmentation is disabled in this view mode");
        post_error(err);
        bus_.post<core::event::tool_result_canceled>().sync(); // 侧边栏按钮回落
        return;
    }

    // single:主图 ×1;split:主+副 ×2(副图缺位按 1 张退化,预览只画主侧)
    std::array<QImage, 2> images { page->image, QImage { } };
    std::size_t count = 1;
    if (options_.mode == core::view_mode::split) {
        if (const auto compare = compare_page_.lock())
            images[count++] = compare->image;
    }
    // 起始域取主页面当前 mask 域(未建则全量程)
    const auto range
        = page->mask ? page->mask->range : std::pair<double, double> { 0.0, 255.0 };

    if (auto started = threshold_tool_.exec(
            std::span<const QImage> { images }.first(count), range);
        !started) {
        bus_.post<core::event::error_occurred>(
            cbuspp::value<common::error&> { started.error() })
            .sync();
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }

    l6_img_ = { };
    l6c_img_ = { };
    // 滑条跟随会话起始域(mask_options 阻断回设,不回发事件)
    bus_.post<core::event::mask_range_echo>(
        cbuspp::value<core::event::mask_range> { range })
        .sync();
    update();
}

void canvas::on_tool_result_applied()
{
    if (threshold_tool_.active()) { // 阈值:仅落盘主页,secondary 仅预览语义
        if (auto r = threshold_tool_.apply(); r) {
            if (const auto page = page_.lock()) {
                page->mask = std::move(r->primary);
                l3_img_ = { }; // 持久 mask 内容变
            } // 页已失效:结果丢弃(副页本就仅预览)
        }
        l6_img_ = { };
        l6c_img_ = { };
    } else if (annotation_tool_.active()) {
        if (auto r = annotation_tool_.apply(); r) {
            if (const auto page = page_.lock()) {
                // 落盘时盖所属文档当前 step 快照(权威值;与工具内快照一致,防漂移)
                if (const auto doc = doc_.lock())
                    for (auto& a : r->annotations)
                        a.step = doc->step;
                // 对比模式:同值双写(L5 的"完全相同"过滤天然成立);single 只写主页
                if (options_.mode != core::view_mode::single) {
                    if (const auto compare = compare_page_.lock())
                        compare->annotations.insert(compare->annotations.end(),
                            r->annotations.begin(), r->annotations.end());
                }
                page->annotations.insert(page->annotations.end(),
                    std::make_move_iterator(r->annotations.begin()),
                    std::make_move_iterator(r->annotations.end()));
            } // 页已失效:结果丢弃
        }
        annot_dragging_ = false;
    } else if (roi_tool_.active()) {
        if (auto r = roi_tool_.apply(); r) {
            // 空结果(会话内未落任何形状)不落盘
            if (const auto page = page_.lock(); page && !r->region.path.empty()) {
                page->rois.push_back(std::move(r->region));
                bus_.post<core::event::page_rois_changed>(
                    cbuspp::value<std::shared_ptr<core::page>> { page })
                    .sync();
                // 对比模式:同值双写(两侧公有,各半同现);single 只写主页
                if (options_.mode != core::view_mode::single) {
                    if (const auto compare = compare_page_.lock()) {
                        compare->rois.push_back(page->rois.back());
                        bus_.post<core::event::page_rois_changed>(
                            cbuspp::value<std::shared_ptr<core::page>> { compare })
                            .sync();
                    }
                }
            } // 页已失效:结果丢弃
        }
        roi_dragging_ = false;
        roi_right_armed_ = false;
        setMouseTracking(false); // 多边形悬停预览随会话结束关闭
    }

    // 统一出口:apply 已处理(含无会话的误触)→ 广播"会话结束 + 当前模式"
    bus_.post<core::event::tool_session_ended>(
        cbuspp::value<core::view_mode> { options_.mode })
        .sync();
    update();
}

void canvas::on_tool_result_canceled()
{
    if (threshold_tool_.active()) {
        threshold_tool_.cancel();
        l6_img_ = { };
        l6c_img_ = { };
    } else if (annotation_tool_.active()) {
        annotation_tool_.cancel();
        annot_dragging_ = false;
    } else if (roi_tool_.active()) {
        roi_tool_.cancel();
        roi_dragging_ = false;
        roi_right_armed_ = false;
        setMouseTracking(false);
    }

    // 统一出口:cancel 已处理(含拒绝路径的按钮回落)→ 广播"会话结束 + 当前模式"
    bus_.post<core::event::tool_session_ended>(
        cbuspp::value<core::view_mode> { options_.mode })
        .sync();
    update();
}

void canvas::on_measure_requested()
{
    const auto doc = doc_.lock();
    const auto page = page_.lock();
    if (!doc || !page) { // 无页:无从标注,按钮回落
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }

    // 会话排他:任一工具运行中不得开新工具(按钮回落)
    if (threshold_tool_.active() || annotation_tool_.active() || roi_tool_.active()) {
        auto err = common::error::make(common::errc::unavailable,
            "another tool session is active");
        post_error(err);
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }

    if (auto started = annotation_tool_.exec(doc->step); !started) {
        bus_.post<core::event::error_occurred>(
            cbuspp::value<common::error&> { started.error() })
            .sync();
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }
    update(); // L6 进入标注预览态
}

void canvas::on_rectangle_draw_requested()
{
    begin_roi_session(roi_shape::rectangle);
}

void canvas::on_ellipse_draw_requested()
{
    begin_roi_session(roi_shape::ellipse);
}

void canvas::on_polygon_draw_requested()
{
    begin_roi_session(roi_shape::polygon);
}

void canvas::begin_roi_session(roi_shape shape)
{
    const auto page = page_.lock();
    if (!page) { // 无页:无从框选,按钮回落
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }

    // 会话排他:任一工具运行中不得开新工具(按钮回落);
    // 框选不消费像素,五种视图模式皆合法(同标注)
    if (threshold_tool_.active() || annotation_tool_.active() || roi_tool_.active()) {
        auto err = common::error::make(common::errc::unavailable,
            "another tool session is active");
        post_error(err);
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }

    if (auto started = roi_tool_.exec(shape); !started) {
        bus_.post<core::event::error_occurred>(
            cbuspp::value<common::error&> { started.error() })
            .sync();
        bus_.post<core::event::tool_result_canceled>().sync();
        return;
    }
    setFocus(); // 数字键 1-4(运算模式)经键盘事件流入画布
    setMouseTracking(shape == roi_shape::polygon); // 多边形:悬停连线预览需要移动事件
    update(); // L6 进入框选预览态
}

void canvas::on_measure_line_width_changed(const cbuspp::value<int>& value)
{
    options_.line_width = *value;
    update(); // L5/L6 同款样式,改动即刻生效
}

void canvas::on_measure_line_color_changed(const cbuspp::value<QColor>& value)
{
    options_.line_color = *value;
    update();
}

void canvas::on_step_x_changed(const cbuspp::value<double>& value)
{
    apply_step_change(true, *value);
}

void canvas::on_step_y_changed(const cbuspp::value<double>& value)
{
    apply_step_change(false, *value);
}

void canvas::apply_step_change(bool x_axis, double value)
{
    const auto doc = doc_.lock();
    if (!doc)
        return;
    // 只写当前文档;变更即删其每一页的全部标注(其余文档的 step 独立,互不影响)
    (x_axis ? doc->step.first : doc->step.second) = value;
    for (auto& [id, page] : doc->pages)
        page->annotations.clear();

    if (annotation_tool_.active()) // 会话中:临时层一并清空,后续新线用新 step
        annotation_tool_.reset_for_step(doc->step);
    update();
}

void canvas::on_measurements_clear_requested()
{
    // single 只清当前页;对比模式清主、副两页(均含未渲染的;不碰文档其余页)
    bool touched = false;
    if (const auto page = page_.lock(); page && !page->annotations.empty()) {
        page->annotations.clear();
        touched = true;
    }
    if (options_.mode != core::view_mode::single) {
        if (const auto compare = compare_page_.lock();
            compare && !compare->annotations.empty()) {
            compare->annotations.clear();
            touched = true;
        }
    }
    if (touched)
        update();
}

void canvas::on_rois_clear_requested()
{
    // 同 measurements_clear 语义:single 只清当前页,对比模式清主、副两页
    bool touched = false;
    if (const auto page = page_.lock(); page && !page->rois.empty()) {
        page->rois.clear();
        bus_.post<core::event::page_rois_changed>(
            cbuspp::value<std::shared_ptr<core::page>> { page })
            .sync();
        touched = true;
    }
    if (options_.mode != core::view_mode::single) {
        if (const auto compare = compare_page_.lock(); compare && !compare->rois.empty()) {
            compare->rois.clear();
            bus_.post<core::event::page_rois_changed>(
                cbuspp::value<std::shared_ptr<core::page>> { compare })
                .sync();
            touched = true;
        }
    }
    if (touched)
        update(); // 高亮随 infodock 删行后的选择联动重广播,此处无需干预
}

void canvas::on_roi_delete_requested(const cbuspp::value<core::roi_ref>& value)
{
    const auto doc = doc_.lock();
    if (!doc)
        return;
    const auto it = doc->pages.find(value->page_id);
    if (it == doc->pages.end()) [[unlikely]]
        return;
    const auto& page = it->second;
    if (value->roi_index >= page->rois.size()) [[unlikely]]
        return; // 编号滞后(防御;正常链路表格行与 rois 一一对应)

    page->rois.erase(page->rois.begin() + static_cast<std::ptrdiff_t>(value->roi_index));

    // 高亮回移:命中即清,同页后续编号随 vector 下标收敛(渲染侧颜色随之迁移)
    if (roi_highlight_ && roi_highlight_->page_id == value->page_id) {
        if (roi_highlight_->roi_index == value->roi_index)
            roi_highlight_.reset();
        else if (roi_highlight_->roi_index > value->roi_index)
            --roi_highlight_->roi_index;
    }

    // 复用 page_rois_changed 收口:index_dock 计数/各订阅方随此更新
    bus_.post<core::event::page_rois_changed>(
            cbuspp::value<std::shared_ptr<core::page>> { page })
        .sync();
    update();
}

void canvas::on_roi_highlight_changed(
    const cbuspp::value<std::optional<core::roi_ref>>& value)
{
    roi_highlight_ = *value; // 仅渲染态:L4 矢量直绘,无层缓存需清
    update();
}

auto canvas::highlight_index(const core::page& page) const -> int
{
    if (!roi_highlight_ || roi_highlight_->page_id != page.info.id)
        return -1;
    return static_cast<int>(roi_highlight_->roi_index);
}

// ─── 页解析与校验 ─────────────────────────────────────────────────────────────

void canvas::post_error(common::error& err)
{
    bus_.post<core::event::error_occurred>(cbuspp::value<common::error&> { err }).sync();
}

void canvas::resolve_pages()
{
    page_.reset();
    compare_page_.reset();
    const auto doc = doc_.lock();
    if (!doc)
        return;
    if (const auto it = doc->pages.find(doc->active_page); it != doc->pages.end()) {
        page_ = it->second;
        if (const auto& ct = it->second->compare_to) { // 顺带解析已设置的对比页
            if (const auto cit = doc->pages.find(*ct); cit != doc->pages.end())
                compare_page_ = cit->second;
        }
    }
}

auto canvas::ensure_compare_page() -> bool
{
    const auto page = page_.lock();
    const auto doc = doc_.lock();
    if (!page || !doc)
        return false;

    if (!page->compare_to) { // 未设对比页 → 对话框选页(确认才写入)
        const auto last = static_cast<int>(doc->info.pages.size()) - 1;
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Select Compare Page"),
            tr("Page (0-%1):").arg(last), 0, 0, std::max(0, last), 1, &ok);
        if (!ok)
            return false;
        page->compare_to = doc->info.pages[static_cast<std::size_t>(n)].id;
        // 对话框路径同样走事件:options_tool_bar 经订阅回显输入框
        // (画布自身订阅幂等:同值重写 + 同态清理,无递归)
        bus_.post<core::event::compare_page_selected>(cbuspp::value<int> { n }).sync();
    }
    return validate_compare();
}

auto canvas::validate_compare() -> bool
{
    compare_page_.reset();

    const auto page = page_.lock();
    const auto doc = doc_.lock();
    if (!page || !doc || !page->compare_to)
        return false;

    const auto it = doc->pages.find(*page->compare_to);
    if (it == doc->pages.end()) {
        auto err = common::error::make(common::errc::not_found,
            "compare page not found in document");
        post_error(err);
        return false;
    }
    if (it->second->image.size() != page->image.size()) { // 主副必须同尺寸
        auto err = common::error::make(common::errc::validation_failed,
            "compare page size mismatch: {}x{} vs {}x{}", page->image.width(),
            page->image.height(), it->second->image.width(), it->second->image.height());
        post_error(err);
        return false;
    }
    compare_page_ = it->second;
    return true;
}

void canvas::cancel_threshold_session()
{
    if (!threshold_tool_.active())
        return;
    threshold_tool_.cancel();
    l6_img_ = { };
    l6c_img_ = { };
    // 广播 canceled:侧边栏按钮回落(本事件回流到自身订阅时工具已取消,无递归)
    bus_.post<core::event::tool_result_canceled>().sync();
    update();
}

void canvas::draw_temp_mask(QPainter& painter, std::size_t index, const core::page& subject,
    QImage& cache)
{
    const auto masks = threshold_tool_.preview();
    if (index >= masks.size() || masks[index].isNull())
        return; // 会话外 / 该侧无掩膜(非灰度副图)
    if (cache.isNull())
        cache = mask_overlay(masks[index], subject.info, options_);
    if (!cache.isNull())
        painter.drawImage(0, 0, cache);
}

void canvas::cancel_annotation_session()
{
    if (!annotation_tool_.active())
        return;
    annotation_tool_.cancel();
    annot_dragging_ = false;
    // 广播 canceled:侧边栏按钮回落与选项页复位(回流时工具已取消,无递归)
    bus_.post<core::event::tool_result_canceled>().sync();
    update();
}

void canvas::cancel_roi_session()
{
    if (!roi_tool_.active())
        return;
    roi_tool_.cancel();
    roi_dragging_ = false;
    roi_right_armed_ = false;
    setMouseTracking(false);
    // 广播 canceled:侧边栏按钮回落与选项页复位(回流时工具已取消,无递归)
    bus_.post<core::event::tool_result_canceled>().sync();
    update();
}

void canvas::draw_temp_annotations(QPainter& painter)
{
    if (!annotation_tool_.active())
        return;
    draw_annotations(painter, annotation_tool_.preview(), options_);
    if (const auto* d = annotation_tool_.draft()) {
        const std::array<const core::annotation, 1> draft { *d };
        draw_annotations(painter, std::span<const core::annotation> { draft }, options_, true);
    }
}

void canvas::draw_temp_rois(QPainter& painter)
{
    if (!roi_tool_.active())
        return;
    const auto page = page_.lock();
    if (!page)
        return;
    // 临时选区颜色 = 落盘后起始编号色(apply 追加为主页 rois 尾项)
    const QColor color = core::roi_color(page->rois.size());
    draw_session_rois(
        painter, roi_tool_.preview(), roi_tool_.shape(), roi_tool_.draft(), color);
    if (roi_tool_.shape() == roi_shape::polygon) // 多边形:顶点 + 悬停连线预览
        draw_session_poly(painter, roi_tool_.poly_draft(), roi_tool_.poly_hover(), color);
}

auto canvas::ants_animated() const -> bool
{
    if (roi_tool_.active())
        return true; // 会话预览虽非蚂蚁线,落盘产物是 —— 相位保持推进
    if (!options_.roi_visible)
        return false; // L4 隐藏:已落选区不画,动画无需推进
    if (const auto page = page_.lock(); page && !page->rois.empty())
        return true;
    if (options_.mode != core::view_mode::single) {
        if (const auto compare = compare_page_.lock(); compare && !compare->rois.empty())
            return true;
    }
    return false;
}

auto canvas::image_pos(const QPointF& screen, double origin) const -> QPointF
{
    QPointF p { (screen.x() - origin - view_.offset.x()) / view_.zoom,
        (screen.y() - view_.offset.y()) / view_.zoom };
    return clamped_image(p); // 鼠标可在界外,坐标钉在边界内
}

auto canvas::gesture_origin(const QPointF& screen) const -> double
{
    return options_.mode == core::view_mode::split
            && screen.x() >= static_cast<double>(seam_x())
        ? static_cast<double>(seam_x())
        : 0.0;
}

auto canvas::clamped_image(QPointF p) const -> QPointF
{
    if (const auto page = page_.lock()) {
        const QSize s = oriented_size(*page);
        p.setX(std::clamp(p.x(), 0.0, static_cast<double>(s.width()) - 1.0));
        p.setY(std::clamp(p.y(), 0.0, static_cast<double>(s.height()) - 1.0));
    }
    return p;
}

auto canvas::squared_end(const QPointF& end, Qt::KeyboardModifiers mods) const -> QPointF
{
    if (!(mods & Qt::ShiftModifier))
        return end;
    // 正方形约束:短边拉到长边(符号保留);可能越出图像 → 回钳(贴边截断)
    const double dx = end.x() - roi_anchor_.x();
    const double dy = end.y() - roi_anchor_.y();
    const double side = std::max(std::abs(dx), std::abs(dy));
    return clamped_image({ roi_anchor_.x() + (dx < 0.0 ? -side : side),
        roi_anchor_.y() + (dy < 0.0 ? -side : side) });
}

auto canvas::aligned_end(const QPointF& end, Qt::KeyboardModifiers mods) const -> QPointF
{
    const auto* d = annotation_tool_.draft();
    if (d == nullptr || !(mods & Qt::ShiftModifier)) // Shift:吸附主轴(水平/垂直)
        return end;
    const QPointF s = d->line.first;
    return std::abs(end.x() - s.x()) >= std::abs(end.y() - s.y())
        ? QPointF { end.x(), s.y() }
        : QPointF { s.x(), end.y() };
}

// ─── 视图约束(旧版同款)────────────────────────────────────────────────────────

auto canvas::display_size() -> QSize
{
    const auto page = page_.lock();
    return page ? oriented_size(*page) : QSize { };
}

auto canvas::half_width() const -> double
{
    if (options_.mode == core::view_mode::split)
        return width() / 2.0;
    return static_cast<double>(width()); // slider 为整视口(缝只切显示侧)
}

auto canvas::zoom_anchor(const QPointF& cursor) const -> QPointF
{
    if (options_.mode != core::view_mode::split)
        return cursor; // 单视口(含 slider):光标即锚点(旧版同款)

    // split 两半共享 offset,单光标无法对称锚定两半 → 各半绕自身中点缩放
    const double seam = seam_x();
    const auto h = static_cast<double>(height());
    return cursor.x() < seam ? QPointF { seam / 2.0, h / 2.0 } // 左半中点
                             : QPointF { seam + (width() - seam) / 2.0, h / 2.0 }; // 右半中点
}

int canvas::seam_x() const
{
    if (options_.mode == core::view_mode::slider)
        return static_cast<int>(width() * view_.split);
    return width() / 2;
}

void canvas::clamp_offset()
{
    const QSize img = display_size();
    if (img.isEmpty())
        return;
    const auto hw = half_width();
    const auto h = static_cast<double>(height());
    const double sw = img.width() * view_.zoom;
    const double sh = img.height() * view_.zoom;

    // 横轴:以 S 所在半区为视口,小于居中、大于则边缘不离开;纵轴恒全高(不变)
    view_.offset.setX(sw <= hw ? (hw - sw) / 2.0 : std::clamp(view_.offset.x(), hw - sw, 0.0));
    view_.offset.setY(sh <= h ? (h - sh) / 2.0 : std::clamp(view_.offset.y(), h - sh, 0.0));
}

void canvas::fit_view()
{
    const QSize img = display_size();
    if (img.isEmpty()) {
        view_ = { };
        return;
    }
    view_.zoom = std::clamp(
        std::min(half_width() / img.width(), static_cast<double>(height()) / img.height()),
        0.1, 10.0);
    view_.offset = { 0.0, 0.0 };
    clamp_offset(); // 居中(适配缩放下即旧版 resetView)
}

// 旧版 QtRenderer::zoom 同款:ratio 锚点数学,缩放生效才 clamp_offset
void canvas::zoom_at(const QPointF& anchor, double delta)
{
    if (display_size().isEmpty())
        return;

    const QPointF local = zoom_anchor(anchor);
    const double old_zoom = view_.zoom;
    const double factor = delta > 0 ? 1.1 : 1.0 / 1.1;
    view_.zoom = std::clamp(view_.zoom * factor, 0.1, 10.0);

    if (view_.zoom != old_zoom) {
        const double ratio = view_.zoom / old_zoom;
        view_.offset = local - (local - view_.offset) * ratio;
        clamp_offset();
    }
}

// ─── 绘制与交互 ───────────────────────────────────────────────────────────────

void canvas::paintEvent(QPaintEvent* event)
{
    QPainter painter { this };
    painter.fillRect(event->rect(), palette().color(QPalette::Window)); // 无文档空白背景

    if (view_dirty_ && width() > 0 && height() > 0) { // 文档就绪后的首次适配
        fit_view();
        view_dirty_ = false;
    }

    const auto page = page_.lock();
    if (!page)
        return;

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // 图像→屏幕:p' = zoom·p + offset。注意 QTransform 乘法为行向量约定:
    // A*B = 先 A 后 B,故 scale 在左、translate 在右(写反会把 offset 也缩放);
    // origin_x = 半区原点(split/slider 右半为缝位置)
    const auto apply_view = [this, &painter](double origin_x = 0.0) {
        painter.setTransform(QTransform::fromScale(view_.zoom, view_.zoom)
            * QTransform::fromTranslate(origin_x + view_.offset.x(), view_.offset.y()));
    };

    // 中缝(屏幕坐标,cosmetic,不随缩放变宽)
    const auto draw_seam = [&painter](int seam, int height) {
        painter.save();
        painter.resetTransform();
        QPen pen { QColor { 128, 128, 128 } };
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawLine(seam, 0, seam, height);
        painter.restore();
    };

    switch (options_.mode) {
    case core::view_mode::single:
        apply_view();
        draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
        draw<layer::l3>(painter, *page, nullptr, options_, l3_img_);
        draw_rois(painter, std::span<const core::roi> { page->rois }, nullptr, options_,
            ants_offset_, highlight_index(*page)); // L4
        draw<layer::l5>(painter, *page, nullptr, options_, l5_img_);
        draw_temp_mask(painter, 0, *page, l6_img_); // L6:工具临时掩膜
        draw_temp_annotations(painter); // L6:标注临时预览
        draw_temp_rois(painter); // L6:框选临时预览
        break;

    case core::view_mode::highlight:
    case core::view_mode::difference: {
        apply_view(); // 不画 L1;L2 灰底 + 差异着色
        const auto compare = compare_page_.lock();
        if (compare)
            draw<layer::l2>(painter, *page, compare.get(), options_, l2_img_);
        // L4/L5:仅画主副两页完全相同的项(独有项不显示)
        draw_rois(painter, std::span<const core::roi> { page->rois },
            compare ? &compare->rois : nullptr, options_, ants_offset_, highlight_index(*page));
        draw<layer::l5>(painter, *page, compare.get(), options_, l5_img_);
        draw_temp_annotations(painter); // 标注五模式合法:预览照画
        draw_temp_rois(painter); // 框选五模式合法:预览照画
        break;
    }

    case core::view_mode::split: {
        const auto compare = compare_page_.lock();
        if (!compare) { // 双保险(进模式时已校验):回落 single
            apply_view();
            draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
            break;
        }

        const int seam = seam_x();

        painter.save(); // 左半:S(各半自为独立视口,共享 zoom/offset)
        painter.setClipRect(QRect { 0, 0, seam, height() });
        apply_view();
        draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
        draw_rois(painter, std::span<const core::roi> { page->rois }, &compare->rois, options_,
            ants_offset_, highlight_index(*page)); // L4:公有项
        draw<layer::l5>(painter, *page, compare.get(), options_, l5_img_);
        draw_temp_mask(painter, 0, *page, l6_img_);
        draw_temp_annotations(painter);
        draw_temp_rois(painter);
        painter.restore();

        painter.save(); // 右半:C(origin = 缝)
        painter.setClipRect(QRect { seam, 0, width() - seam, height() });
        apply_view(static_cast<double>(seam));
        draw<layer::l1>(painter, *compare, nullptr, options_, l1c_img_);
        draw_rois(painter, std::span<const core::roi> { compare->rois }, &page->rois, options_,
            ants_offset_, highlight_index(*compare)); // L4:公有项(过滤基准 = 主页)
        draw<layer::l5>(painter, *compare, page.get(), options_, l5_img_);
        draw_temp_mask(painter, 1, *compare, l6c_img_); // L6:副侧自己的掩膜
        draw_temp_annotations(painter); // 同一图像坐标:右半同位预览
        draw_temp_rois(painter);
        painter.restore();

        draw_seam(seam, height());
        break;
    }

    case core::view_mode::slider: {
        const auto compare = compare_page_.lock();
        if (!compare) {
            apply_view();
            draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
            break;
        }

        // 单一坐标系(与 single 同布局):同一变换,缝左 clip 画 S、缝右画 C
        const int seam = seam_x();

        painter.save();
        painter.setClipRect(QRect { 0, 0, seam, height() });
        apply_view();
        draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
        draw_rois(painter, std::span<const core::roi> { page->rois }, &compare->rois, options_,
            ants_offset_, highlight_index(*page)); // L4:公有项
        draw<layer::l5>(painter, *page, compare.get(), options_, l5_img_);
        painter.restore();

        painter.save();
        painter.setClipRect(QRect { seam, 0, width() - seam, height() });
        apply_view(); // 无 origin 偏移:与 S 完全同位
        draw<layer::l1>(painter, *compare, nullptr, options_, l1c_img_);
        draw_rois(painter, std::span<const core::roi> { compare->rois }, &page->rois, options_,
            ants_offset_, highlight_index(*compare)); // L4:公有项(过滤基准 = 主页)
        draw<layer::l5>(painter, *compare, page.get(), options_, l5_img_);
        painter.restore();

        // 标注/框选预览:与 S/C 同一坐标系,整视口一次(两侧本就同位)
        painter.save();
        apply_view();
        draw_temp_annotations(painter);
        draw_temp_rois(painter);
        painter.restore();

        draw_seam(seam, height());
        break;
    }
    }
}

void canvas::wheelEvent(QWheelEvent* event)
{
    zoom_at(event->position(), event->angleDelta().y() > 0 ? 1.0 : -1.0);
    update();
}

void canvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        if (roi_tool_.active()) {
            // 会话期右键二义:先按"单击撤销"仲裁,拖动超阈值才转平移
            roi_right_armed_ = true;
            roi_right_press_ = event->position();
        } else {
            panning_ = true; // 右键拖动平移(非会话:按下即平移)
            pan_last_ = event->position();
        }
        event->accept();
    } else if (event->button() == Qt::LeftButton && annotation_tool_.active()) {
        // 标注起笔:以按下点定半区(split),整条手势锁定同一视口 origin
        annot_origin_ = gesture_origin(event->position());
        annot_dragging_ = true;
        annotation_tool_.begin_line(image_pos(event->position(), annot_origin_));
        update();
        event->accept();
    } else if (event->button() == Qt::LeftButton && roi_tool_.active()) {
        if (roi_tool_.shape() == roi_shape::polygon) {
            // 多边形:左键单击落顶点(每次点击按所在半区映射,可跨缝)
            roi_tool_.add_poly_point(
                image_pos(event->position(), gesture_origin(event->position())));
        } else {
            // 包围盒形状:起笔,半区锁定视口 origin,锚点即起笔角
            roi_origin_ = gesture_origin(event->position());
            roi_dragging_ = true;
            roi_anchor_ = image_pos(event->position(), roi_origin_);
            roi_tool_.begin_rect(roi_anchor_);
        }
        update();
        event->accept();
    }
}

// 双击的第二击不产生独立 press(Qt 以 dblClick 取代),多边形在此封闭
void canvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && roi_tool_.active()
        && roi_tool_.shape() == roi_shape::polygon) {
        roi_tool_.close_poly();
        update();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void canvas::mouseMoveEvent(QMouseEvent* event)
{
    if (annot_dragging_) {
        annotation_tool_.move_line(
            aligned_end(image_pos(event->position(), annot_origin_), event->modifiers()));
        update();
        event->accept();
        return;
    }
    if (roi_dragging_) {
        roi_tool_.move_rect(
            squared_end(image_pos(event->position(), roi_origin_), event->modifiers()));
        update();
        event->accept();
        return;
    }
    if (roi_right_armed_ && !panning_) {
        // 会话期右键待裁决:超阈值才转平移(纯点击不产生微平移)
        const QPointF d = event->position() - roi_right_press_;
        if (std::hypot(d.x(), d.y()) > 4.0) {
            roi_right_armed_ = false;
            panning_ = true;
            pan_last_ = event->position();
        }
        event->accept();
        return;
    }
    if (!panning_) {
        // 多边形悬停:预览最后顶点到光标的连线(tracking 仅多边形会话开启)
        if (roi_tool_.active() && roi_tool_.shape() == roi_shape::polygon) {
            roi_tool_.move_poly(
                image_pos(event->position(), gesture_origin(event->position())));
            update();
            event->accept();
        }
        return;
    }
    view_.offset += event->position() - pan_last_;
    pan_last_ = event->position();
    clamp_offset();
    update();
    event->accept();
}

void canvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton && panning_) {
        panning_ = false;
        event->accept();
    } else if (event->button() == Qt::RightButton && roi_right_armed_) {
        // 会话期右键单击:撤销最后一个矩形(左键拖拽中不响应)
        roi_right_armed_ = false;
        if (!roi_dragging_)
            roi_tool_.undo_rect();
        update();
        event->accept();
    } else if (event->button() == Qt::LeftButton && annot_dragging_) {
        annot_dragging_ = false;
        annotation_tool_.end_line(
            aligned_end(image_pos(event->position(), annot_origin_), event->modifiers()));
        update();
        event->accept();
    } else if (event->button() == Qt::LeftButton && roi_dragging_) {
        roi_dragging_ = false;
        roi_tool_.end_rect(
            squared_end(image_pos(event->position(), roi_origin_), event->modifiers()));
        update();
        event->accept();
    }
}

void canvas::keyPressEvent(QKeyEvent* event)
{
    if (roi_tool_.active()) {
        switch (event->key()) { // 运算模式:1 并集(默认) 2 交集 3 差集 4 异或
        case Qt::Key_1:
            roi_tool_.set_op(roi_op::union_);
            break;
        case Qt::Key_2:
            roi_tool_.set_op(roi_op::intersection);
            break;
        case Qt::Key_3:
            roi_tool_.set_op(roi_op::difference);
            break;
        case Qt::Key_4:
            roi_tool_.set_op(roi_op::xor_);
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
        }
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void canvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!view_dirty_)
        clamp_offset(); // 维持"边缘不离开视口/居中"约束
    // slider 悬浮于画布底部居中(非布局子控件,手动定位)
    slider_->setGeometry(event->size().width() / 2 - 150, event->size().height() - 36, 300, 20);
}

}
