#pragma once

#include <memory>
#include <optional>

#include <cbuspp/cbuspp.hpp>

#include "config.hpp" // usip::core::config / settings_registry
#include "error.hpp"
#include "executor.hpp" // usip::common::executor
#include "logger.hpp" // usip::common::logger
#include "main_window.hpp"
#include "service.hpp"

class QApplication; // 前置:qt_app_ 以 unique_ptr 持有,完整析构定义在 .cpp

namespace usip::app {

class application {
public:
    application() = default;
    ~application();

    application(const application&) = delete;
    auto operator=(const application&) -> application& = delete;

    [[nodiscard]] auto init(int& argc, char** argv) -> result<>;
    auto exec() -> int;

private:
    // 声明序 = 逆序析构序;qt_app_ 最先声明 → 最后析构(QWidget 须先于 QApplication 销毁)
    std::unique_ptr<QApplication> qt_app_ { nullptr };
    std::unique_ptr<core::config> config_ { nullptr }; // config 含 atomic 成员不可移动,指针持有
    std::optional<common::logger> logger_ { }; // logger 可移动,optional 持有
    std::unique_ptr<common::executor> executor_ { nullptr };
    std::unique_ptr<cbuspp::bus<common::executor>> bus_ { nullptr }; // 持有 executor 引用,须先销毁
    std::unique_ptr<service::service> service_ { nullptr }; // 持有 bus 引用,须先于 bus 销毁
    std::unique_ptr<ui::main_window> ui_ { nullptr };
};

} // namespace usip::app
