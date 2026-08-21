#pragma once

#include <QDockWidget>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include <cuuidpp/cuuidpp.hpp>

#include "event.hpp"
#include "roi_stats.hpp"
#include "ui_protocol.hpp"

class QAction;
class QMenu;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

namespace usip::ui {

class info_dock : public ui_protocol<info_dock, QDockWidget> {
    Q_OBJECT
    friend class ui_protocol<info_dock, QDockWidget>;

public:
    explicit info_dock(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~info_dock() override;

    info_dock(const info_dock&) = delete;
    info_dock& operator=(const info_dock&) = delete;
    info_dock(info_dock&&) = delete;
    info_dock& operator=(info_dock&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    // 视口按压前快照当前行(选择更新之前),供"再点已选中行 = 取消高亮"裁决
    bool eventFilter(QObject* watched, QEvent* event) override;

    void on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value);
    void on_document_switch(const cbuspp::value<std::shared_ptr<core::document>>& value);
    // 页选区变更(复用既有广播收口):空 → 移除该页全部行;非空 → 行数差额即
    // 尾部新增,按序补行(行 ↔ rois 尾项一一对应:删除由本 dock 先行同步,
    // 到达时恒一致,天然幂等)
    void on_page_rois_changed(const cbuspp::value<std::shared_ptr<core::page>>& value);
    // 工具会话开启(任一):禁用(含后续功能);结束(canvas 广播):解禁
    void on_tool_session_started();
    void on_tool_session_ended(const cbuspp::value<core::view_mode>& value);

private:
    QTableWidget* make_table(QWidget* parent);
    // 追加一行:Index = 页序(UserRole 绑页 uuid),Number = 选区编号(编号色
    // 粗体,UserRole 存编号),Floor/Ceil 可编辑,统计列经 compute_roi_stats
    void append_row(QTableWidget& table, const core::page& page, std::size_t sel,
        std::pair<double, double> range);
    // 统计列落格(信号由调用方阻断):空结果整组留空;valid == 0 时均值组留空
    void fill_stats(QTableWidget& table, int row, const std::optional<core::roi_stats>& stats);
    // 上下限编辑提交:钳 [0,255] 且不得越过另一格(就地钳回),随后重算该行统计列
    void on_range_edited(QTableWidget& table, QTableWidgetItem* item);
    // 主页当前 mask 阈值域(主/副行共用主页阈值):激活文档激活页;
    // 解析失败回落该页自身域
    [[nodiscard]] auto primary_range(const core::page& page) const
        -> std::pair<double, double>;
    // 行 → (页 uuid, 选区编号);行越界/缺格 → nullopt
    [[nodiscard]] static auto row_ref(const QTableWidget& table, int row)
        -> std::optional<core::roi_ref>;
    // 当前表选中行 → 广播高亮(无选中/空表 → nullopt)
    void sync_highlight();

private:
    QStackedWidget* stacked_ { nullptr };
    QTableWidget* empty_ { nullptr };
    QMenu* context_menu_ { nullptr };
    QAction* delete_action_ { nullptr };

    std::weak_ptr<core::document> doc_ { }; // 激活文档(行 → 页解析用)

    // 再点已选中行 = 取消高亮:按压前快照(选择尚未更新),释放时一致即取消;
    // 双击(Floor/Ceil 编辑)期抑制
    int pre_press_row_ { -1 };
    bool dblclk_guard_ { false };

    std::unordered_map<cuuidpp::uuid, QTableWidget*> tables_ { };
};

}
