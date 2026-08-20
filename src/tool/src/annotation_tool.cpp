// ==============================================================================
// annotation_tool.cpp — 标注工具实现
// ==============================================================================

#include "annotation_tool.hpp"

#include <cmath>
#include <format>
#include <utility>

namespace usip::ui {
namespace {

    // 手势有效长度(图像像素):过短视为误触
    constexpr double min_line_px { 2.0 };

} // namespace

auto annotation_tool::exec(std::pair<double, double> step) -> result<void>
{
    if (!(step.first > 0.0) || !(step.second > 0.0))
        return common::fail(common::errc::invalid_argument,
            "annotation tool expects positive step, got ({}, {})", step.first, step.second);

    step_ = step;
    placed_.clear();
    draft_.reset();
    active_ = true;
    return {};
}

void annotation_tool::reset_for_step(std::pair<double, double> step)
{
    if (!active_ || !(step.first > 0.0) || !(step.second > 0.0))
        return;
    step_ = step;
    placed_.clear(); // 旧换算的临时标注全部作废
    draft_.reset();
}

void annotation_tool::begin_line(const QPointF& start)
{
    if (!active_)
        return;
    draft_.emplace();
    draft_->line = { start, start };
    draft_->label = { };
    draft_->step = step_; // 标签换算所用的 step 快照
}

void annotation_tool::move_line(const QPointF& current)
{
    if (!draft_)
        return;
    draft_->line.second = current;
    draft_->label = label_of(draft_->line);
}

void annotation_tool::end_line(const QPointF& end)
{
    if (!draft_)
        return;
    draft_->line.second = end;

    const auto& [p1, p2] = draft_->line;
    if (std::hypot(p2.x() - p1.x(), p2.y() - p1.y()) < min_line_px) {
        draft_.reset();
        return;
    }
    draft_->label = label_of(draft_->line);
    placed_.push_back(std::move(*draft_));
    draft_.reset();
}

auto annotation_tool::active() const noexcept -> bool
{
    return active_;
}

auto annotation_tool::preview() const noexcept -> std::span<const core::annotation>
{
    return active_ ? std::span<const core::annotation> { placed_ }
                   : std::span<const core::annotation> { };
}

auto annotation_tool::draft() const noexcept -> const core::annotation*
{
    return draft_ ? &*draft_ : nullptr;
}

auto annotation_tool::apply() -> result<outcome>
{
    if (!active_)
        return common::fail(common::errc::failed_precondition,
            "annotation tool has no active session");

    draft_.reset();
    outcome o { std::move(placed_) };
    release();
    return o;
}

void annotation_tool::cancel() noexcept
{
    release();
}

auto annotation_tool::label_of(const std::pair<QPointF, QPointF>& line) const -> std::string
{
    const auto dx = static_cast<double>(line.second.x() - line.first.x()) * step_.first;
    const auto dy = static_cast<double>(line.second.y() - line.first.y()) * step_.second;
    return std::format("{:.4f} mm", std::hypot(dx, dy));
}

void annotation_tool::release() noexcept
{
    active_ = false;
    step_ = { 1.0, 1.0 };
    placed_.clear();
    placed_.shrink_to_fit();
    draft_.reset();
}

}
