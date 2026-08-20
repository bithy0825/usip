#pragma once

// ==============================================================================
// threshold_tool.hpp — 阈值分割工具(canvas 的算法侧,无总线/无控件)
//
// 会话语义:exec 带图集(1 张=single 主图;2 张=split 主/副)与初始 range
// (取主页面当前 mask 域)启动;逐图各自阈值化出自己的掩膜(同 range、
// 各自像素,8 位显示域,u16 取 >>8 与直方图 bin 同域),运行期一切产物只
// 经 preview() 供 L6 临时层显示;set_range 随滑条重算;apply 移动移交
// {主页结果, 副页结果(可空)} 并结束 —— 副页结果仅供预览对位,canvas 只
// 落盘主页;cancel 释放全部缓冲,零副作用。两页原始像素始终不动。
//
// highway 加速:比较核走 SIMD(项目全局 /arch:AVX2,静态分发);u16 → 8 位
// 缩域在 exec 一次完成,滑条拖动只跑 8 位核。
// ==============================================================================

#include <QImage>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "document.hpp"
#include "error.hpp"
#include "tool.hpp"

namespace usip::ui {

class threshold_tool : public tool<threshold_tool> {
public:
    // apply 载荷:主页结果(落盘这份)+ 副页结果(仅预览语义;single 空)
    struct outcome {
        core::mask primary { };
        std::optional<core::mask> secondary { };
    };

    // images: 1 张=single 主图;2 张=split(主,副)。主图空/非灰度 → 失败,
    // 会话不建立;副图非灰度仅意味着该侧无预览(对应掩膜为空)
    [[nodiscard]] auto exec(std::span<const QImage> images, std::pair<double, double> range)
        -> result<void>;

    // 会话中滑条驱动:同 range 对各图重算掩膜(非会话调用为无操作)
    void set_range(std::pair<double, double> range);

    [[nodiscard]] auto active() const noexcept -> bool;

    // 逐图掩膜(L6 数据源;序号同 exec 输入,不可用的图为 null)
    [[nodiscard]] auto preview() const noexcept -> std::span<const QImage>;

    // 当前会话 range(非会话返回 {0,255})
    [[nodiscard]] auto range() const noexcept -> std::pair<double, double>;

    // 结束会话并移动移交结果;无会话 → failed_precondition
    [[nodiscard]] auto apply() -> result<outcome>;

    // 结束会话并释放全部临时缓冲(无副作用)
    void cancel() noexcept;

private:
    // 单图工作面:exec 时一次换算好的 8 位显示域像素(display 空 = 不可用)
    struct plane {
        std::vector<std::uint8_t> display { };
        int width { 0 };
        int height { 0 };
    };

    void recompute(); // range_ → masks_(逐图)
    void release() noexcept; // 会话结束:清全部缓冲

    bool active_ { false };
    std::pair<double, double> range_ { 0.0, 255.0 };
    std::array<plane, 2> planes_ { };
    std::array<QImage, 2> masks_ { };
    std::size_t count_ { 0 };
};

}
