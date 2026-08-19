#pragma once

#include <cstdint>

namespace usip::core {

// ─── 视图模式(view_mode_set 事件已废弃;枚举保留,options.mode 待用)────────────
enum class view_mode : std::uint8_t {
    single, // 单页层栈
    split, // 视口对半:S 左 C 右
    slider, // 缝在 t×宽度:左 S 右 C
    highlight, // L1(S) + 非零差异平色覆盖
    difference, // L1(S) 的差值替换帧
};

}
