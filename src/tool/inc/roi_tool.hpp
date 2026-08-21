#pragma once

// ==============================================================================
// roi_tool.hpp — 框选工具(矩形/椭圆;canvas 的几何侧,无总线/无控件/不消费像素)
//
// 会话语义:左键拖拽逐个画包围盒,松开落为历史项(矩形 = 包围盒本身,
// 椭圆 = 包围盒内切椭圆,采样为闭合多边形进入布尔运算);会话内可累积多个,
// 新矩形/椭圆依当前运算模式(数字键 1-4)并进累积选区,右键撤销最后一个。
// 布尔合并是破坏性的,故累积结果不即时烘焙 —— 工具保留逐项历史(包围盒 +
// 落下那一刻的运算模式),累积路径始终由历史按序重放得出,撤销即弹出末项
// 再重放;会话中途切换模式不影响已落项的语义。一切产物只经 preview()/
// draft() 供 L6 临时层显示;apply 移动移交合并结果(一个会话 = 一个
// roi,落盘即 page.rois 的一项,颜色按 vector 编号分配);cancel 释放
// 全部状态,零副作用。
//
// 坐标约定:图像像素;角点由调用方(canvas)钳制在图像范围内,包围盒是两
// 个界内角点的包围盒,故选区不越出图像。
// ==============================================================================

#include <QPointF>
#include <QRectF>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "document.hpp"
#include "error.hpp"
#include "tool.hpp"

namespace usip::ui {

// 框选形状:两种形状都以拖拽包围盒定义(Shift 约束为正方形包围盒,
// 内切椭圆即正圆);采样为多边形后进入布尔运算,存储形态无异
enum class roi_shape : std::uint8_t {
    rectangle,
    ellipse,
};

// 框选布尔运算模式:1=并集(默认) 2=交集 3=差集 4=异或(union/xor 是关键字,加下划线)
enum class roi_op : std::uint8_t {
    union_,
    intersection,
    difference,
    xor_,
};

class roi_tool : public tool<roi_tool> {
public:
    // apply 载荷:会话全部包围盒的布尔合并结果(移动移交)
    struct outcome {
        core::roi region { };
    };

    // 启动会话(形状随会话固定:矩形/椭圆)
    [[nodiscard]] auto exec(roi_shape shape) -> result<void>;

    // 会话形状(草稿渲染用)
    [[nodiscard]] auto shape() const noexcept -> roi_shape;

    // 运算模式(会话中随时切换;只影响之后的落笔)
    void set_op(roi_op op) noexcept;
    [[nodiscard]] auto op() const noexcept -> roi_op;

    // 手势(图像像素坐标;包围盒角点):起笔 / 拖动更新 / 松开落选区
    void begin_rect(const QPointF& start);
    void move_rect(const QPointF& current);
    void end_rect(const QPointF& end);

    // 撤销最后一个选区并重放;空历史 / 拖拽中不响应
    void undo_rect();

    [[nodiscard]] auto active() const noexcept -> bool;

    // 累积选区路径(L6 数据源;历史重放结果)
    [[nodiscard]] auto preview() const noexcept -> const Clipper2Lib::PathsD&;

    // 拖拽中的包围盒(归一化;无则 nullopt;矩形=它本身,椭圆=内切于它)
    [[nodiscard]] auto draft() const noexcept -> std::optional<QRectF>;

    // 结束会话并移动移交结果;无会话 → failed_precondition
    [[nodiscard]] auto apply() -> result<outcome>;

    // 结束会话并释放全部临时状态(无副作用)
    void cancel() noexcept;

private:
    void replay(); // 历史按序重放 → 累积路径(撤销的实现基础)
    void release() noexcept; // 会话结束:清全部状态

    bool active_ { false };
    roi_shape shape_ { roi_shape::rectangle };
    roi_op op_ { roi_op::union_ };
    std::vector<std::pair<QRectF, roi_op>> history_ { }; // 已落包围盒 + 落下时的模式
    Clipper2Lib::PathsD accumulated_ { }; // preview 数据源
    std::optional<std::pair<QPointF, QPointF>> draft_ { }; // 起点与当前点
};

}
