#pragma once

// ==============================================================================
// tool.hpp — 画布工具接口(CRTP 基类)
//
// 生命周期:exec(args...) 启动工具(不定输入随工具而定:如阈值工具带初始
// range 与主/副页)→ 运行期间 UI 输入(框选、阈值、标注等)只落临时层
// (L6),不碰 page/doc 持久数据 → apply() 写入持久化数据并结束;cancel()
// 无痕丢弃。Apply/Cancel 由 options_tool_bar 的共享 action 触发
// (tool_result_applied / tool_result_canceled 事件),具体工具自行订阅。
//
// CRTP 契约(与 ui_protocol 同用法):派生类必须实现同名三方法,基类负责
// 转发;基类析构受保护,禁止经基类指针多态删除。
// ==============================================================================

#include <utility>

namespace usip::ui {

template <typename Derived>
class tool {
public:
    // 启动工具(不定输入随工具而定)
    template <typename... Args>
    decltype(auto) exec(Args&&... args)
    {
        return static_cast<Derived*>(this)->exec(std::forward<Args>(args)...);
    }

    // 应用:把工具结果写入持久化数据
    void apply()
    {
        static_cast<Derived*>(this)->apply();
    }

    // 取消:丢弃全部临时状态,不产生任何副作用
    void cancel()
    {
        static_cast<Derived*>(this)->cancel();
    }

protected:
    ~tool() = default; // CRTP 非多态基类:禁止经基类删除
};

}
