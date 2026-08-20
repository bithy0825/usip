#pragma once

// ==============================================================================
// tool.hpp — 画布工具接口(CRTP 基类)
//
// 生命周期:exec(args...) 启动工具(输入随工具而定:如阈值工具带初始 range
// 与主/副页)→ 运行期间 UI 输入(阈值、框选、标注等)只落临时层(L6),
// 不碰 page/doc 持久数据 → apply() 返回结果并结束会话,由调用方(canvas)
// 写入持久化数据;cancel() 无痕丢弃。工具不持总线、不订阅事件 ——
// Apply/Cancel 由 options_tool_bar 的共享 action 发 tool_result_applied /
// tool_result_canceled 事件,canvas 收到后调用工具,数据经类型化返回值流动。
//
// 契约(CRTP,与 ui_protocol 同用法):
//   * 派生类实现 exec / preview / apply / cancel(+ active 状态查询);
//   * 操作型方法(exec/apply)的返回值必须用项目统一结果包裹 usip::result
//     (common::result,失败携带 common::error),基类 static_assert 强约束;
//     状态查询(preview/active/range 等)不产生失败,保持裸类型返回;
//   * 会话中输入方法(如阈值工具的 set_range)属工具特有,不入本契约;
//   * 签名与载荷类型由派生类自定义 —— apply 可按工具返回各自的结果类型
//     (阈值 → mask,绘制 → roi……)。preview 是临时层(L6)的统一数据源。
// 基类析构受保护,禁止经基类指针多态删除。
// ==============================================================================

#include <type_traits>
#include <utility>

#include "error.hpp" // usip::result / common::error(操作型返回值的统一包裹)

namespace usip::ui {
namespace detail {

    // 判别"是否为项目统一结果包裹"(接口层约束用)
    template <typename T>
    struct is_result : std::false_type { };
    template <typename T>
    struct is_result<result<T>> : std::true_type { };
    template <typename T>
    inline constexpr bool is_result_v = is_result<std::remove_cvref_t<T>>::value;

} // namespace detail

template <typename Derived>
class tool {
public:
    // 启动工具(不定输入随工具而定)
    template <typename... Args>
    decltype(auto) exec(Args&&... args)
    {
        static_assert(detail::is_result_v<decltype(
                          static_cast<Derived*>(this)->exec(std::declval<Args>()...))>,
            "tool contract: exec must return usip::result<...>");
        return static_cast<Derived*>(this)->exec(std::forward<Args>(args)...);
    }

    // 预览数据:临时层(L6)的数据源(状态查询,不包裹)
    [[nodiscard]] decltype(auto) preview() const
    {
        return static_cast<const Derived*>(this)->preview();
    }

    // 应用:结束会话并返回结果,持久化由调用方完成
    [[nodiscard]] decltype(auto) apply()
    {
        static_assert(
            detail::is_result_v<decltype(static_cast<Derived*>(this)->apply())>,
            "tool contract: apply must return usip::result<...>");
        return static_cast<Derived*>(this)->apply();
    }

    // 取消:丢弃全部临时状态,不产生任何副作用
    void cancel() noexcept
    {
        static_cast<Derived*>(this)->cancel();
    }

protected:
    ~tool() = default; // CRTP 非多态基类:禁止经基类删除
};

}
