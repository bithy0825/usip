#pragma once

#include <QMainWindow>

#include <memory>

#include "event.hpp"
#include "ui_protocol.hpp"

namespace usip::ui {

class menu_bar;
class top_tool_bar;
class options_tool_bar;
class side_tool_bar;
class index_dock;
class hist_dock;
class info_dock;
class canvas;
class status_bar;

class main_window : public ui_protocol<main_window, QMainWindow> {
    Q_OBJECT
    friend class ui_protocol<main_window, QMainWindow>;

public:
    explicit main_window(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~main_window() override;

    main_window(const main_window&) = delete;
    main_window& operator=(const main_window&) = delete;
    main_window(main_window&&) = delete;
    main_window& operator=(main_window&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    // 激活文档跟踪(save 默认名 / export 数据源)
    void on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value);
    void on_document_switch(const cbuspp::value<std::shared_ptr<core::document>>& value);
    // Save:主窗口截图直存默认路径(首个未占用名);Save As:同图弹框选径
    void on_save_screenshot();
    void on_save_screenshot_as();
    // Export:高级导出对话框;About:关于对话框
    void on_export();
    void on_about();

    // 默认截图基名(无扩展名):<源目录>/<stem>_screenshot;无文档 → 图片目录/usip_screenshot
    [[nodiscard]] auto screenshot_default_base() const -> QString;
    // 抓取主窗口存 PNG 并反馈(成功 → status_message;失败 → error_occurred)
    void save_screenshot_to(const QString& path);

private:
    menu_bar* menu_bar_ { nullptr };
    top_tool_bar* top_tool_bar_ { nullptr };
    options_tool_bar* options_tool_bar_ { nullptr };
    side_tool_bar* side_tool_bar_ { nullptr };
    index_dock* index_dock_ { nullptr };
    hist_dock* hist_dock_ { nullptr };
    info_dock* info_dock_ { nullptr };
    canvas* canvas_ { nullptr };
    status_bar* status_bar_ { nullptr };

    std::weak_ptr<core::document> doc_ { }; // 激活文档(非拥有,实体在 service)
};

}
