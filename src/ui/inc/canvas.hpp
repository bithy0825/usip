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

#include "draw.hpp"
#include "event.hpp"
#include "ui_protocol.hpp"

class QSlider;

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
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // ── 总线回调 ───────────────────────────────────────────────────────────
    // 文档
    void on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value);
    void on_document_switch(const cbuspp::value<std::shared_ptr<core::document>>& value);
    // 模式与对比页
    void on_view_mode_changed(const cbuspp::value<core::view_mode>& value);
    void on_compare_page_selected(const cbuspp::value<int>& value); // 0 起页序
    // L1 伪彩
    void on_pseudocolor_enabled_toggled(const cbuspp::value<bool>& value);
    void on_pseudocolor_colormap_changed(const cbuspp::value<core::colormap_type>& value);
    void on_pseudocolor_zero_is_black_toggled(const cbuspp::value<bool>& value);
    // L3 mask(仅 single 显示)
    void on_mask_visible_toggled(const cbuspp::value<bool>& value);
    void on_mask_color_changed(const cbuspp::value<QColor>& value);
    void on_mask_opacity_changed(const cbuspp::value<double>& value);
    void on_mask_floor_changed(const cbuspp::value<double>& value);
    void on_mask_ceiling_changed(const cbuspp::value<double>& value);
    // L5 标注参数(层未实现,暂存)
    void on_measure_line_width_changed(const cbuspp::value<int>& value);
    void on_measure_line_color_changed(const cbuspp::value<QColor>& value);

    // doc_ → page_(active_page)与 compare_page_(compare_to);无则置空
    void resolve_pages();
    // 进对比模式前:无 compare_to 弹对话框选页(确认才写入 page);再校验尺寸
    // (不一致发 error_occurred)。成功 → compare_page_ 就绪
    [[nodiscard]] auto ensure_compare_page() -> bool;
    // 校验并缓存对比页指针(失败发 error_occurred)
    [[nodiscard]] auto validate_compare() -> bool;
    // 按 page_->mask.range 重算 mask 图像并清 L3 缓存
    void rethreshold_mask();

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
    QImage l4_img_ { }, l5_img_ { }, l6_img_ { }; // 预留

    std::weak_ptr<core::document> doc_ { }; // 激活文档(非拥有,实体在 service)
    std::weak_ptr<core::page> page_ { }; // 激活页(非拥有)
    std::weak_ptr<core::page> compare_page_ { }; // 对比页(非拥有)

    QSlider* slider_ { nullptr }; // slider 模式缝输入(仅该模式可见)
    QPointF pan_last_ { };
    bool panning_ { false }; // 右键拖动平移
    bool view_dirty_ { false }; // 文档就绪置位,首次有效尺寸 paint 前执行 fit_view
};

}
