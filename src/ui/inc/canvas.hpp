#pragma once

// ==============================================================================
// canvas.hpp — 中央画布:状态与事件的唯一管理者;draw 只是层绘制方法
//
// 持有:options(定义在 draw.hpp,本类拥有唯一实例:构造时从 config 播种部分
// 键,总线事件改字段)、view_state、各层绘制结果缓存(一层一个 QImage;事件
// 变更某层内容时清该层缓存,下次 draw 自动重建)、激活文档/激活页/对比页
// (weak_ptr 非拥有,不认识其它文档)。
//
// 模式几何(怎么画,全部在本类):
//   single      整视口 L1+L3
//   split       左右半区各自 clip:L1(S)|L1(C),两半自为独立视口,共享
//               zoom/offset,中缝 cosmetic
//   slider      整视口单一坐标系(与 single 同布局):缝左画 S、缝右画 C
//               (同一变换,两侧仅 clip 不同);缝在 t×宽度,底部 QSlider
//               输入(仅该模式可见),拖动零重建
//   highlight   整视口 L2(同区灰底+异区固定色);不画 L1
//   difference  整视口 L2(同区灰底+异区伪彩,伪彩固有)
// 非 single 模式 mask(L3)永不显示;进入前须存在已校验的对比页(未设弹对话
// 框选页,尺寸不一致直接报错)。
//
// 交互(旧版 usip0.12.0 同款):滚轮锚点缩放(步进 1.1,clamp [0.1,10])、
// 右键拖动平移、新文档适配居中;split 以半区为视口。
// ==============================================================================

#include <memory>

#include <QWidget>

#include "annotation_tool.hpp"
#include "draw.hpp"
#include "event.hpp"
#include "roi_tool.hpp"
#include "threshold_tool.hpp"
#include "ui_protocol.hpp"

class QSlider;
class QTimer;

namespace usip::ui {

// ─── 视图状态:画布持有的交互态 ────────────────────────────────────────────────
struct view_state {
    double zoom { 1.0 }; // 图像→屏幕缩放(clamp [0.1, 10])
    QPointF offset { }; // 屏幕平移(设备像素)
    double split { 0.5 }; // slider 缝比例 t ∈ [0,1](split 恒半区)
};

class canvas : public ui_protocol<canvas, QWidget> {
    Q_OBJECT
    friend class ui_protocol<canvas, QWidget>;

public:
    explicit canvas(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~canvas() override;

    canvas(const canvas&) = delete;
    canvas& operator=(const canvas&) = delete;
    canvas(canvas&&) = delete;
    canvas& operator=(canvas&&) = delete;

protected:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // ── 总线回调 ───────────────────────────────────────────────────────────
    // 文档
    void on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value);
    void on_document_switch(const cbuspp::value<std::shared_ptr<core::document>>& value);
    // 模式与对比页
    void on_view_mode_change_requested(const cbuspp::value<core::view_mode>& value);
    void on_compare_page_selected(const cbuspp::value<int>& value); // 0 起页序
    // L1 伪彩
    void on_pseudocolor_enabled_toggled(const cbuspp::value<bool>& value);
    void on_pseudocolor_colormap_changed(const cbuspp::value<core::colormap_type>& value);
    void on_pseudocolor_zero_is_black_toggled(const cbuspp::value<bool>& value);
    // L3 mask(仅 single 显示)
    void on_mask_visible_toggled(const cbuspp::value<bool>& value);
    // L4/L5 可见开关(五模式皆受控;开关只管持久层,L6 会话预览不受影响)
    void on_roi_visible_toggled(const cbuspp::value<bool>& value);
    void on_annotation_visible_toggled(const cbuspp::value<bool>& value);
    void on_mask_color_changed(const cbuspp::value<QColor>& value);
    void on_mask_opacity_changed(const cbuspp::value<double>& value);
    void on_mask_floor_changed(const cbuspp::value<double>& value);
    void on_mask_ceiling_changed(const cbuspp::value<double>& value);
    // 阈值分割工具(canvas 编排:输入转换 → exec;事件 → apply/cancel)
    void on_threshold_segment_requested();
    void on_tool_result_applied();
    void on_tool_result_canceled();
    // 标注工具(measure 按钮触发;不消费像素,exec 只带 step)
    void on_measure_requested();
    // 框选工具(rectangle/ellipse/polygon 按钮触发;几何工具,五模式皆合法)
    void on_rectangle_draw_requested();
    void on_ellipse_draw_requested();
    void on_polygon_draw_requested();
    // 框选会话公共入口:页校验 → 会话排他 → exec(形状) → 抢占键盘焦点
    // (多边形额外开 mouse tracking:悬停连线预览)
    void begin_roi_session(roi_shape shape);
    // L5 标注参数(线宽/线色改即时重绘)
    void on_measure_line_width_changed(const cbuspp::value<int>& value);
    void on_measure_line_color_changed(const cbuspp::value<QColor>& value);
    // 采集步长(写 document.step,全文档标注失效;会话中顺带清主副两页并重算临时标签)
    void on_step_x_changed(const cbuspp::value<double>& value);
    void on_step_y_changed(const cbuspp::value<double>& value);
    void apply_step_change(bool x_axis, double value);
    // 清除主、副两页全部标注数据(含未渲染的)
    void on_measurements_clear_requested();
    // 清除主、副两页全部选区数据(Clear Constituency)
    void on_rois_clear_requested();
    // 选区删除(infodock 行右键):擦除 page.rois 项,经 page_rois_changed 收口
    void on_roi_delete_requested(const cbuspp::value<core::roi_ref>& value);
    // infodock 行选中高亮:仅渲染态(不落数据);nullopt = 清除
    void on_roi_highlight_changed(const cbuspp::value<std::optional<core::roi_ref>>& value);
    // 该页当前应高亮的选区下标(无/不属于该页 = -1;draw_rois 末参)
    [[nodiscard]] auto highlight_index(const core::page& page) const -> int;
    // 光标取样广播(status_bar 右下):屏幕坐标 → 显示域像素 → 主/副灰度;
    // 越界/无页 → nullopt(状态栏清空)
    void emit_pixel_sample(const QPointF& screen);
    // 显示域像素坐标 → 该页存储域灰度(orient 逆映射;非灰度页 → nullopt)
    [[nodiscard]] static auto sample_gray(const core::page& page, const QPoint& disp)
        -> std::optional<int>;

    // doc_ → page_(active_page)与 compare_page_(compare_to);无则置空
    void resolve_pages();
    // document_ready/switch 统一入口(空载荷内挡)
    void handle_document(const std::shared_ptr<core::document>& doc);
    // error_occurred 统一出口
    void post_error(common::error& err);
    // 进对比模式前:无 compare_to 弹对话框选页(确认才写入 page);再校验尺寸
    // (不一致发 error_occurred)。成功 → compare_page_ 就绪
    [[nodiscard]] auto ensure_compare_page() -> bool;
    // 校验并缓存对比页指针(失败发 error_occurred)
    [[nodiscard]] auto validate_compare() -> bool;
    // 结束阈值会话(工具取消 + 清双 L6 缓存 + 广播 canceled 回同步侧边栏按钮)
    void cancel_threshold_session();
    // 结束标注会话(同上;标注无层缓存,直接 update)
    void cancel_annotation_session();
    // 结束框选会话(同上;矢量直绘,无层缓存)
    void cancel_roi_session();
    // L4:工具临时掩膜叠加(index 对应 preview() 序号;orient 取该半区页面)
    void draw_temp_mask(QPainter& painter, std::size_t index, const core::page& subject,
        QImage& cache);
    // L6:标注工具临时预览(已落 + 拖拽中;矢量直绘,无缓存)
    void draw_temp_annotations(QPainter& painter);
    // L6:框选工具临时预览(累积路径 + 拖拽草稿;矢量直绘,无缓存)
    void draw_temp_rois(QPainter& painter);
    // 蚂蚁线是否在动(任一可见页有选区,或框选会话进行中)
    [[nodiscard]] auto ants_animated() const -> bool;
    // 屏幕 → 图像像素(标注手势;origin:split 右半 = seam,其余 0);端点钳制在图像范围内
    [[nodiscard]] auto image_pos(const QPointF& screen, double origin) const -> QPointF;
    // 手势所在半区的视口 origin(split 右半 = seam,其余 0;标注/框选/多边形共用)
    [[nodiscard]] auto gesture_origin(const QPointF& screen) const -> double;
    // 图像坐标钳制到图像范围(鼠标可在界外,坐标钉在边界内;框选正方形约束后复钳)
    [[nodiscard]] auto clamped_image(QPointF p) const -> QPointF;
    // Shift 按下时把框选角点约束为正方形包围盒(相对锚点取 max 边;越界回钳,
    // 贴边被截断 —— 内切椭圆即正圆)
    [[nodiscard]] auto squared_end(const QPointF& end, Qt::KeyboardModifiers mods) const
        -> QPointF;
    // Shift 按下时把线末端吸附到水平/垂直主轴(相对草稿起点)
    [[nodiscard]] auto aligned_end(const QPointF& end, Qt::KeyboardModifiers mods) const
        -> QPointF;

    // ── 视图约束(旧版同款;split 以 S 所在半区为视口,slider 为整视口)─────────
    void clamp_offset();
    void fit_view();
    void zoom_at(const QPointF& anchor, double delta);
    [[nodiscard]] auto display_size() -> QSize;
    // S 视口宽度(split 取半,其余全宽)
    [[nodiscard]] auto half_width() const -> double;
    // 缩放锚点:split = 光标所在半区的中点(两半共享 offset);其余 = 光标
    [[nodiscard]] auto zoom_anchor(const QPointF& cursor) const -> QPointF;
    // 中缝横坐标(split = 半宽;slider = t×宽度)
    [[nodiscard]] int seam_x() const;

    view_state view_ { };
    options options_ { }; // 唯一一份(定义于 draw.hpp,本类拥有)

    // 层结果缓存:事件变更哪层清哪层(C 后缀 = 对比页侧;L3 只属 single)
    QImage l1_img_ { }, l1c_img_ { }; // L1:主/副底图
    QImage l2_img_ { }; // L2:运算层合成图(highlight/difference)
    QImage l3_img_ { }; // L3:mask 着色
    QImage l5_img_ { }; // L5:标注(draw 模板签名占位;矢量直绘不落缓存)
    QImage l6_img_ { }, l6c_img_ { }; // L6:工具临时掩膜(主/副侧)

    threshold_tool threshold_tool_ { }; // 阈值分割(canvas 编排,工具不持总线)
    annotation_tool annotation_tool_ { }; // 标注(同上)
    roi_tool roi_tool_ { }; // 矩形框选(同上)

    bool annot_dragging_ { false }; // 标注手势进行中(左键按住)
    double annot_origin_ { 0.0 }; // 手势所在半区的视口 origin(split 右半 = seam)

    bool roi_dragging_ { false }; // 框选手势进行中(左键按住)
    double roi_origin_ { 0.0 }; // 手势所在半区的视口 origin(split 右半 = seam)
    QPointF roi_anchor_ { }; // 手势锚点(起笔图像坐标;正方形约束的参考)
    bool roi_right_armed_ { false }; // 会话期右键待裁决(位移超阈值转平移,否则撤销)
    QPointF roi_right_press_ { }; // 右键按下位置(位移判别基准)

    QTimer* ants_timer_ { nullptr }; // 蚂蚁线动画(100ms 推进相位)
    int ants_offset_ { 0 }; // 蚂蚁线相位(模 ants_offset_cycle)

    std::optional<core::roi_ref> roi_highlight_ { }; // infodock 行选中高亮(仅渲染)

    std::weak_ptr<core::document> doc_ { }; // 激活文档(非拥有,实体在 service)
    std::weak_ptr<core::page> page_ { }; // 激活页(非拥有)
    std::weak_ptr<core::page> compare_page_ { }; // 对比页(非拥有)

    QSlider* slider_ { nullptr }; // slider 模式缝输入(仅该模式可见)
    QPointF pan_last_ { };
    bool panning_ { false }; // 右键拖动平移
    bool view_dirty_ { false }; // 文档就绪置位,首次有效尺寸 paint 前执行 fit_view
};

}
