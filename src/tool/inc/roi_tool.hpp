#pragma once

// ==============================================================================
// roi_tool.hpp — 框选工具(矩形/椭圆/多边形;canvas 几何侧,无总线/无控件)
//
// 会话语义:形状随会话固定 —— 矩形/椭圆为左键拖拽包围盒(矩形 = 包围盒
// 本身,椭圆 = 内切椭圆);多边形为左键逐点落顶点、双击封闭(悬停点与最后
// 顶点连线预览)。会话内可累积多个,新形状依当前运算模式(数字键 1-4)并
// 进累积选区,右键撤销最后一个已落形状。布尔合并是破坏性的,故累积结果
// 不即时烘焙 —— 工具保留逐项历史(落笔时即采样为多边形路径 + 落下那一刻
// 的运算模式;包围盒形状采样一次,多边形直接存顶点),累积路径始终由历史
// 按序重放得出,撤销即弹出末项再重放;会话中途切换模式不影响已落项的语
// 义。一切产物只经 preview()/draft()/poly_draft()/poly_hover() 供 L6 临
// 时层显示;apply 移动移交合并结果(一个会话 = 一个 roi,落盘即 page.rois
// 的一项,颜色按 vector 编号分配);cancel 释放全部状态,零副作用。
//
// 坐标约定:图像像素;角点/顶点由调用方(canvas)钳制在图像范围内,故选
// 区不越出图像。
// ==============================================================================

#include <QPointF>
#include <QRectF>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "document.hpp"
#include "error.hpp"
#include "tool.hpp"

namespace usip::ui {

// 框选形状:包围盒形状(矩形/椭圆,Shift 约束正方形 → 内切正圆)与逐点
// 多边形(双击封闭);采样为多边形路径后进入布尔运算,存储形态无异
enum class roi_shape : std::uint8_t {
    rectangle,
    ellipse,
    polygon,
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

    // 多边形手势(图像像素坐标;顶点已由 canvas 钳制在图像内):
    // 左键单击落顶点(与上一顶点过近视为误触)/ 悬停更新(预览连线)/
    // 双击封闭落历史并重放(不足三点或面积过小整条丢弃)
    void add_poly_point(const QPointF& pos);
    void move_poly(const QPointF& pos);
    void close_poly();

    // 撤销最后一个已落形状并重放;空历史 / 包围盒拖拽中不响应
    void undo_rect();

    [[nodiscard]] auto active() const noexcept -> bool;

    // 累积选区路径(L6 数据源;历史重放结果)
    [[nodiscard]] auto preview() const noexcept -> const Clipper2Lib::PathsD&;

    // 拖拽中的包围盒(归一化;无则 nullopt;矩形=它本身,椭圆=内切于它)
    [[nodiscard]] auto draft() const noexcept -> std::optional<QRectF>;

    // 进行中的多边形顶点(L6 数据源)
    [[nodiscard]] auto poly_draft() const noexcept -> std::span<const QPointF>;
    // 多边形悬停点(预览连线端点;无则 nullptr)
    [[nodiscard]] auto poly_hover() const noexcept -> const QPointF*;

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
    // 已落形状(落笔时采样好的多边形路径 + 落下时的模式)
    std::vector<std::pair<Clipper2Lib::PathD, roi_op>> history_ { };
    Clipper2Lib::PathsD accumulated_ { }; // preview 数据源
    std::optional<std::pair<QPointF, QPointF>> draft_ { }; // 包围盒手势:起点与当前点
    std::vector<QPointF> poly_ { }; // 多边形手势:已落顶点
    std::optional<QPointF> poly_hover_ { }; // 多边形手势:悬停点
};

}
