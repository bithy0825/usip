#pragma once

// ==============================================================================
// annotation_tool.hpp — 标注工具(canvas 的几何侧,无总线/无控件/不消费像素)
//
// 会话语义:exec 带 document 的 step(mm/像素)启动;左键拖拽逐条画线,
// begin/move/end 手势驱动,松开落为临时标注(过短视为误触丢弃),会话内可
// 累积多条;一切产物只经 preview()/draft() 供 L6 临时层显示;apply 移动
// 移交全部标注并结束(落盘范围由 canvas 按模式决定:single 只写主页,
// 对比模式双写主副);cancel 释放全部状态,零副作用。
//
// 距离标签(旧版同源):d = hypot(Δpx·step_x, Δpy·step_y),"{:.4f} mm"。
// ==============================================================================

#include <QPointF>

#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "document.hpp"
#include "error.hpp"
#include "tool.hpp"

namespace usip::ui {

class annotation_tool : public tool<annotation_tool> {
public:
    // apply 载荷:会话内累积的全部标注(移动移交)
    struct outcome {
        std::vector<core::annotation> annotations { };
    };

    // step:document 的 mm/像素换算系数(须为正)
    [[nodiscard]] auto exec(std::pair<double, double> step) -> result<void>;

    // 会话中 step 变更:清空临时层全部标注(旧换算的作废)并更新系数
    void reset_for_step(std::pair<double, double> step);

    // 手势(图像像素坐标):起笔 / 拖动更新 / 松开落线
    void begin_line(const QPointF& start);
    void move_line(const QPointF& current);
    void end_line(const QPointF& end);

    [[nodiscard]] auto active() const noexcept -> bool;

    // 已落临时标注(L6 数据源)
    [[nodiscard]] auto preview() const noexcept -> std::span<const core::annotation>;

    // 拖拽中的线(无则 nullptr)
    [[nodiscard]] auto draft() const noexcept -> const core::annotation*;

    // 结束会话并移动移交结果;无会话 → failed_precondition
    [[nodiscard]] auto apply() -> result<outcome>;

    // 结束会话并释放全部临时状态(无副作用)
    void cancel() noexcept;

private:
    [[nodiscard]] auto label_of(const std::pair<QPointF, QPointF>& line) const -> std::string;
    void release() noexcept; // 会话结束:清全部状态

    bool active_ { false };
    std::pair<double, double> step_ { 1.0, 1.0 };
    std::vector<core::annotation> placed_ { };
    std::optional<core::annotation> draft_ { };
};

}
