// ==============================================================================
// roi_tool.cpp — 框选工具(矩形/椭圆)实现
// ==============================================================================

#include "roi_tool.hpp"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace usip::ui {
namespace {

    // 手势有效边长(图像像素):过短视为误触
    constexpr double min_rect_px { 2.0 };

    // 包围盒 → 闭合多边形(矩形;图像像素坐标)
    [[nodiscard]] auto rect_path(const QRectF& rect) -> Clipper2Lib::PathD
    {
        return { { rect.left(), rect.top() }, { rect.right(), rect.top() },
            { rect.right(), rect.bottom() }, { rect.left(), rect.bottom() } };
    }

    // 包围盒 → 内切椭圆 → 闭合多边形:顶点数按周长自适应(段长约 8px,
    // 夹在 [48, 384] —— 小图不锯齿、大图不多边形化)
    [[nodiscard]] auto ellipse_path(const QRectF& rect) -> Clipper2Lib::PathD
    {
        const double cx = rect.center().x();
        const double cy = rect.center().y();
        const double rx = rect.width() / 2.0;
        const double ry = rect.height() / 2.0;
        const double perimeter = std::numbers::pi * (rect.width() + rect.height()) / 2.0;
        const auto n = static_cast<int>(
            std::clamp(perimeter / 8.0, 48.0, 384.0));

        Clipper2Lib::PathD path;
        path.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double t = 2.0 * std::numbers::pi * static_cast<double>(i) / n;
            path.push_back({ cx + rx * std::cos(t), cy + ry * std::sin(t) });
        }
        return path;
    }

    [[nodiscard]] auto shape_path(const QRectF& rect, roi_shape shape) -> Clipper2Lib::PathD
    {
        return shape == roi_shape::ellipse ? ellipse_path(rect) : rect_path(rect);
    }

    // 单步布尔合并(NonZero:Clipper2 输出孔洞与外圈方向相反,两种填充规则皆有效)
    [[nodiscard]] auto merge(const Clipper2Lib::PathsD& subject,
        const Clipper2Lib::PathsD& clip, roi_op op) -> Clipper2Lib::PathsD
    {
        using enum roi_op;
        using enum Clipper2Lib::FillRule;
        switch (op) {
        case union_:
            return Clipper2Lib::Union(subject, clip, NonZero);
        case intersection:
            return Clipper2Lib::Intersect(subject, clip, NonZero);
        case difference:
            return Clipper2Lib::Difference(subject, clip, NonZero);
        case xor_:
            return Clipper2Lib::Xor(subject, clip, NonZero);
        }
        std::unreachable();
    }

} // namespace

auto roi_tool::exec(roi_shape shape) -> result<void>
{
    release(); // 上一会话残留防御性清空(cancel 语义);形状/模式回到默认
    shape_ = shape;
    active_ = true;
    return {};
}

auto roi_tool::shape() const noexcept -> roi_shape
{
    return shape_;
}

void roi_tool::set_op(roi_op op) noexcept
{
    op_ = op;
}

auto roi_tool::op() const noexcept -> roi_op
{
    return op_;
}

void roi_tool::begin_rect(const QPointF& start)
{
    if (!active_)
        return;
    draft_.emplace(start, start);
}

void roi_tool::move_rect(const QPointF& current)
{
    if (!draft_)
        return;
    draft_->second = current;
}

void roi_tool::end_rect(const QPointF& end)
{
    if (!draft_)
        return;
    draft_->second = end;

    const QRectF rect = QRectF { draft_->first, draft_->second }.normalized();
    draft_.reset();
    if (rect.width() < min_rect_px || rect.height() < min_rect_px)
        return; // 误触丢弃

    history_.emplace_back(rect, op_);
    replay();
}

void roi_tool::undo_rect()
{
    if (!active_ || draft_ || history_.empty())
        return;
    history_.pop_back();
    replay();
}

auto roi_tool::active() const noexcept -> bool
{
    return active_;
}

auto roi_tool::preview() const noexcept -> const Clipper2Lib::PathsD&
{
    return accumulated_;
}

auto roi_tool::draft() const noexcept -> std::optional<QRectF>
{
    if (!draft_)
        return std::nullopt;
    return QRectF { draft_->first, draft_->second }.normalized();
}

auto roi_tool::apply() -> result<outcome>
{
    if (!active_)
        return common::fail(common::errc::failed_precondition,
            "roi tool has no active session");

    draft_.reset();
    outcome o { core::roi { std::move(accumulated_) } };
    release();
    return o;
}

void roi_tool::cancel() noexcept
{
    release();
}

void roi_tool::replay()
{
    accumulated_.clear();
    for (const auto& [rect, op] : history_) {
        const Clipper2Lib::PathsD clip { shape_path(rect, shape_) };
        accumulated_ = merge(accumulated_, clip, op);
    }
}

void roi_tool::release() noexcept
{
    active_ = false;
    shape_ = roi_shape::rectangle;
    op_ = roi_op::union_;
    history_.clear();
    history_.shrink_to_fit();
    accumulated_.clear();
    accumulated_.shrink_to_fit();
    draft_.reset();
}

}
