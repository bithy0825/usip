#pragma once

#include <QStatusBar>

#include <optional>
#include <string>

#include "event.hpp"
#include "ui_protocol.hpp"

class QLabel;

namespace usip::ui {

// ─── 状态栏布局约定 ──────────────────────────────────────────────────────────
//   左下(消息区):工具会话操作提示(含快捷键,会话期常驻)| 悬停 statusTip
//                (Qt 原生,临时覆盖后自动恢复)| 保存/导出反馈与错误(限时)
//   右下(常驻):光标像素取样 X/Y/灰度(single 仅主页;对比模式主 S/副 C 同坐标)
class status_bar : public ui_protocol<status_bar, QStatusBar> {
    Q_OBJECT
    friend class ui_protocol<status_bar, QStatusBar>;

public:
    explicit status_bar(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~status_bar() override;

    status_bar(const status_bar&) = delete;
    status_bar& operator=(const status_bar&) = delete;
    status_bar(status_bar&&) = delete;
    status_bar& operator=(status_bar&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    // 右下:光标取样(越界/无页 → 清空)
    void on_pixel_sample_changed(
        const cbuspp::value<std::optional<core::pixel_sample>>& value);
    // 左下:限时反馈(保存/导出等)与错误
    void on_status_message(const cbuspp::value<std::string>& value);
    void on_error_occurred(const cbuspp::value<common::error&>& value);
    // 左下:工具会话操作提示(任一开启 → 常驻;canvas 广播结束 → 清除)
    void on_tool_session_started(const QString& tip);
    void on_tool_session_ended(const cbuspp::value<core::view_mode>& value);

private:
    QLabel* pixel_ { nullptr }; // 右下常驻取样
    QString session_tip_ { }; // 会话期基线消息(限时消息/悬停不冲毁的判定基准)
};

}
