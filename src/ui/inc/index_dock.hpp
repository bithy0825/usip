#pragma once

#include <QDockWidget>
#include <cuuidpp/cuuidpp.hpp>
#include <unordered_map>

#include "event.hpp"
#include "ui_protocol.hpp"

class QComboBox;
class QDoubleSpinBox;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

namespace usip::ui {

class index_dock : public ui_protocol<index_dock, QDockWidget> {
    Q_OBJECT
    friend class ui_protocol<index_dock, QDockWidget>;

public:
    explicit index_dock(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~index_dock() override;

    index_dock(const index_dock&) = delete;
    index_dock& operator=(const index_dock&) = delete;
    index_dock(index_dock&&) = delete;
    index_dock& operator=(index_dock&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    void on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value);
    void on_document_switch(const cbuspp::value<std::shared_ptr<core::document>>& value);
    void on_page_rois_changed(const cbuspp::value<std::shared_ptr<core::page>>& value);
    // 工具会话开启(任一):禁用(doc/step/页切换全在其中);结束(canvas 广播):解禁
    void on_tool_session_started();
    void on_tool_session_ended(const cbuspp::value<core::view_mode>& value);

private:
    QTableWidget* make_table(QWidget* parent);

    // 选中该页所在行(阻断信号:程序性选择不发 page_switch_requested)
    void select_page_row(const cuuidpp::uuid& page_id);
    // step 控件回显 document.step(阻断,不回发事件)
    void sync_step(const core::document& doc);

private:
    QComboBox* doc_ { nullptr };
    QDoubleSpinBox* x_step_ { nullptr };
    QDoubleSpinBox* y_step_ { nullptr };
    QStackedWidget* stacked_ { nullptr };
    QTableWidget* empty_ { nullptr };

    std::unordered_map<cuuidpp::uuid, QTableWidget*> tables_ { };
    std::unordered_map<cuuidpp::uuid, QTableWidgetItem*> page_items_ { };
};

}
