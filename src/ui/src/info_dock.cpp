#include "info_dock.hpp"
#include "colormap.hpp"
#include "icon_registry.hpp"

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QGridLayout>
#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVariant>

#include <algorithm>

namespace usip::ui {

namespace {

    // 列布局(表头见 make_table):Index 格 UserRole 绑页 uuid,Number 格
    // UserRole 存选区编号;百分比 % 在标题行,单元格只写 4 位小数数值
    namespace col {
        inline constexpr int index { 0 };
        inline constexpr int number { 1 };
        inline constexpr int floor { 2 }; // 可编辑
        inline constexpr int ceil { 3 }; // 可编辑
        inline constexpr int pixel { 4 };
        inline constexpr int valid { 5 };
        inline constexpr int percent { 6 };
        inline constexpr int mean { 7 };
        inline constexpr int stddev { 8 };
        inline constexpr int min { 9 };
        inline constexpr int max { 10 };
    } // namespace col

} // namespace

info_dock::info_dock(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

info_dock::~info_dock() = default;

void info_dock::setup_ui()
{
    setWindowTitle(tr("Info"));
    setWindowIcon(icon_registry::instance().icon("info").value_or(QIcon()));
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* container = new QWidget(this);
    stacked_ = new QStackedWidget(container);
    empty_ = make_table(container);
    stacked_->addWidget(empty_);
    stacked_->setCurrentWidget(empty_);
    stacked_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 行右键菜单:删除该选区(本 dock 先行同步表格,数据删除经事件由 canvas 裁决)
    context_menu_ = new QMenu(this);
    delete_action_ = context_menu_->addAction(tr("Delete Constituency"));

    auto* layout = new QGridLayout(container);
    layout->addWidget(stacked_, 0, 0, 1, 1);
    layout->setRowStretch(0, 1);
    layout->setColumnStretch(0, 1);
    setWidget(container);
}

void info_dock::setup_subscriptions()
{
    bus_.on<core::event::document_ready>().call(*this, &info_dock::on_document_ready);
    bus_.on<core::event::document_switch>().call(*this, &info_dock::on_document_switch);
    bus_.on<core::event::page_rois_changed>().call(*this, &info_dock::on_page_rois_changed);
    // 工具会话:开启禁用(观察者:订阅原始 request 自推导),结束经 canvas 广播解禁
    bus_.on<core::event::threshold_segment_requested>()
        .call(*this, &info_dock::on_tool_session_started);
    bus_.on<core::event::measure_requested>().call(*this, &info_dock::on_tool_session_started);
    bus_.on<core::event::tool_session_ended>().call(*this, &info_dock::on_tool_session_ended);
}

void info_dock::setup_connections()
{
    // 删除:表格行是本 dock 的呈现状态(删行/重编号本地完成);
    // 数据变更(canvas 擦除 rois 项 + 重绘)经请求事件裁决
    connect(delete_action_, &QAction::triggered, this, [this] {
        auto* table = qobject_cast<QTableWidget*>(stacked_->currentWidget());
        if (!table || table == empty_)
            return;
        const int deleted_row = table->currentRow(); // 右键已选中该行
        const auto ref = row_ref(*table, deleted_row);
        if (!ref)
            return;

        {
            const QSignalBlocker blocker { table }; // 选择联动末次经 sync_highlight 统一广播
            table->removeRow(deleted_row);
            // 同页后续行:编号回移 + 颜色随编号(与渲染侧 vector 下标迁移同口径)
            for (int row = 0; row < table->rowCount(); ++row) {
                const auto other = row_ref(*table, row);
                if (!other || other->page_id != ref->page_id
                    || other->roi_index <= ref->roi_index)
                    continue;
                const auto moved = other->roi_index - 1;
                auto* numer = table->item(row, col::number);
                numer->setData(Qt::DisplayRole, static_cast<int>(moved));
                numer->setData(Qt::UserRole, static_cast<int>(moved));
                numer->setForeground(QBrush { core::roi_color(moved) });
            }
        }
        sync_highlight(); // 删除即失选:广播清除高亮

        bus_.post<core::event::roi_delete_requested>(cbuspp::value<core::roi_ref> { *ref })
            .sync();
    });
}

void info_dock::on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc) [[unlikely]]
        return;

    auto* table = make_table(stacked_);
    tables_.emplace(doc->info.id, table);
    stacked_->addWidget(table);
    stacked_->setCurrentWidget(table);
    doc_ = doc;
    sync_highlight(); // 新文档无选中:清除旧高亮
}

void info_dock::on_document_switch(const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc) // 空载荷(如重复打开的提醒)不切
        return;

    doc_ = doc;
    if (const auto it = tables_.find(doc->info.id); it != tables_.end())
        stacked_->setCurrentWidget(it->second);
    sync_highlight(); // 页/文档切换:高亮跟随当前表选中(同表同选则幂等)
}

void info_dock::on_page_rois_changed(const cbuspp::value<std::shared_ptr<core::page>>& value)
{
    const auto& page = *value;
    if (!page) [[unlikely]]
        return;
    const auto table_it = tables_.find(page->doc_id);
    if (table_it == tables_.end())
        return;
    auto& table = *table_it->second;

    // 该页现有行数(按页 uuid 过滤;一张表混排文档内多页的选区行)
    int rows = 0;
    for (int row = 0; row < table.rowCount(); ++row)
        if (const auto* item = table.item(row, col::index);
            item && item->data(Qt::UserRole).value<cuuidpp::uuid>() == page->info.id)
            ++rows;

    if (page->rois.empty()) { // Clear Constituency:移除该页全部行
        {
            const QSignalBlocker blocker { &table };
            for (int row = table.rowCount() - 1; row >= 0; --row)
                if (const auto* item = table.item(row, col::index);
                    item && item->data(Qt::UserRole).value<cuuidpp::uuid>() == page->info.id)
                    table.removeRow(row);
        }
        sync_highlight();
        return;
    }

    // 新增恒在尾部(apply push 后立即广播):补齐差额行;阈值域取主页(共用)
    const auto total = page->rois.size();
    if (static_cast<std::size_t>(rows) < total) {
        const auto range = primary_range(*page);
        for (auto sel = static_cast<std::size_t>(rows); sel < total; ++sel)
            append_row(table, *page, sel, range);
    }
}

void info_dock::on_tool_session_started()
{
    setEnabled(false); // 会话期禁用(含后续功能)
}

void info_dock::on_tool_session_ended(const cbuspp::value<core::view_mode>&)
{
    setEnabled(true);
}

QTableWidget* info_dock::make_table(QWidget* parent)
{
    auto* table = new QTableWidget(parent);
    table->setColumnCount(11);
    table->setHorizontalHeaderLabels({ tr("Index"), tr("Number"), tr("Floor"), tr("Ceil"),
        tr("Pixel"), tr("Valid"), tr("Percent(%)"), tr("Mean"), tr("Std"), tr("Min"),
        tr("Max") });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); // 列宽自适应(同 index_dock)

    // 一次只选整行;仅 Floor/Ceil 两列可编辑(其余列逐格摘除可编辑标志)
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->verticalHeader()->hide();
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    // 上下限编辑提交 → 重算该行统计列
    connect(table, &QTableWidget::itemChanged, this,
        [this, table](QTableWidgetItem* item) { on_range_edited(*table, item); });
    // 行选中 → 广播高亮(渲染期蒙版;无选中 → 清除)
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] { sync_highlight(); });
    // 再点已选中行 = 取消高亮:按下武装,双击间隔后裁决;双击(Floor/Ceil 编辑)抑制
    connect(table, &QTableWidget::itemPressed, this, [this, table](QTableWidgetItem* item) {
        if (!item || table != sel_table_ || item->row() != sel_row_ || sel_row_ < 0)
            return; // 非"再点已选中行"
        dblclk_guard_ = false;
        const int armed_row = sel_row_;
        QTimer::singleShot(QApplication::doubleClickInterval(), this, [this, table, armed_row] {
            if (dblclk_guard_ || table != sel_table_ || armed_row != sel_row_)
                return; // 双击编辑 / 选择已迁移:不裁决
            table->clearSelection();
            table->setCurrentIndex(QModelIndex { }); // 彻底无当前行(选择联动清高亮)
        });
    });
    connect(table, &QTableWidget::itemDoubleClicked, this, [this] { dblclk_guard_ = true; });
    // 行右键 → 选中该行并弹出删除菜单
    connect(table, &QTableWidget::customContextMenuRequested, this,
        [this, table](const QPoint& pos) {
            if (const auto* item = table->itemAt(pos)) {
                table->setCurrentCell(item->row(), col::index); // 右键即选中整行
                context_menu_->exec(table->viewport()->mapToGlobal(pos));
            }
        });
    return table;
}

void info_dock::append_row(QTableWidget& table, const core::page& page, std::size_t sel,
    std::pair<double, double> range)
{
    const QSignalBlocker blocker { &table }; // 程序性填格不触发 itemChanged/选择联动
    const int row = table.rowCount();
    table.insertRow(row);

    const auto set = [&table, row](int c, const QVariant& value, bool editable = false) {
        auto* item = new QTableWidgetItem;
        item->setTextAlignment(Qt::AlignCenter);
        if (editable)
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        else
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setData(Qt::DisplayRole, value);
        table.setItem(row, c, item);
        return item;
    };

    set(col::index, page.index)->setData(Qt::UserRole, QVariant::fromValue(page.info.id));

    // 选区编号:粗体 + 编号色(与渲染蚂蚁线同色);UserRole 存编号(重编号用)
    auto* numer = set(col::number, static_cast<int>(sel));
    numer->setForeground(QBrush { core::roi_color(sel) });
    QFont font = numer->font();
    font.setBold(true);
    numer->setFont(font);
    numer->setData(Qt::UserRole, static_cast<int>(sel));

    // 上下限:显示与计算同口径的整数域;UserRole 存上次合法值(非法输入回滚用)
    const auto [lo, hi] = core::integral_range(range);
    set(col::floor, lo, true)->setData(Qt::UserRole, lo);
    set(col::ceil, hi, true)->setData(Qt::UserRole, hi);

    fill_stats(table, row, core::compute_roi_stats(page, sel, range));
}

void info_dock::fill_stats(QTableWidget& table, int row,
    const std::optional<core::roi_stats>& stats)
{
    const auto set = [&table, row](int c, const QVariant& value) {
        auto* item = table.item(row, c);
        if (!item) { // 追加路径:格不存在则新建(恒不可编辑)
            item = new QTableWidgetItem;
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table.setItem(row, c, item);
        }
        item->setData(Qt::DisplayRole, value);
    };

    if (!stats) { // 无阈值域(非灰度页等):统计列整组留空
        for (const int c : { col::pixel, col::valid, col::percent, col::mean, col::stddev,
                 col::min, col::max })
            set(c, { });
        return;
    }
    set(col::pixel, stats->total);
    set(col::valid, stats->valid);
    set(col::percent, QString::number(stats->percent, 'f', 4)); // % 在标题行
    if (stats->valid == 0) { // 无有效像素:均值组无定义,留空
        for (const int c : { col::mean, col::stddev, col::min, col::max })
            set(c, { });
        return;
    }
    set(col::mean, stats->mean);
    set(col::stddev, stats->std_dev);
    set(col::min, stats->min);
    set(col::max, stats->max);
}

void info_dock::on_range_edited(QTableWidget& table, QTableWidgetItem* item)
{
    if (!item)
        return;
    const int c = item->column();
    if (c != col::floor && c != col::ceil)
        return;
    const int row = item->row();
    auto* floor_item = table.item(row, col::floor);
    auto* ceil_item = table.item(row, col::ceil);
    if (!floor_item || !ceil_item) [[unlikely]]
        return;

    // 解析并钳 [0,255];非法输入回滚为该格上次合法值(UserRole)
    const auto read = [](const QTableWidgetItem* it) {
        bool ok = false;
        const int v = it->text().toInt(&ok);
        return ok ? std::clamp(v, 0, 255) : it->data(Qt::UserRole).toInt();
    };
    int lo = read(floor_item);
    int hi = read(ceil_item);
    if (c == col::floor) // 不得越过另一格:编辑格就地钳回(同 QRangeSlider 语义)
        lo = std::min(lo, hi);
    else
        hi = std::max(hi, lo);

    const QSignalBlocker blocker { &table }; // 回写不递归触发 itemChanged
    floor_item->setData(Qt::DisplayRole, lo);
    floor_item->setData(Qt::UserRole, lo);
    ceil_item->setData(Qt::DisplayRole, hi);
    ceil_item->setData(Qt::UserRole, hi);

    // 重算该行统计列:页经激活文档解析(编号滞后/页缺失 → 统计列不动)
    const auto doc = doc_.lock();
    if (!doc)
        return;
    const auto ref = row_ref(table, row);
    if (!ref)
        return;
    const auto it = doc->pages.find(ref->page_id);
    if (it == doc->pages.end()) [[unlikely]]
        return;
    fill_stats(table, row,
        core::compute_roi_stats(*it->second, ref->roi_index,
            { static_cast<double>(lo), static_cast<double>(hi) }));
}

auto info_dock::primary_range(const core::page& page) const -> std::pair<double, double>
{
    // 主页 = 激活文档激活页(apply 时恒为框选所在页);仅同文档才取,跨文档回落
    if (const auto doc = doc_.lock(); doc && doc->info.id == page.doc_id)
        if (const auto it = doc->pages.find(doc->active_page);
            it != doc->pages.end() && it->second->mask)
            return it->second->mask->range;
    return page.mask ? page.mask->range : std::pair<double, double> { 0.0, 255.0 };
}

auto info_dock::row_ref(const QTableWidget& table, int row) -> std::optional<core::roi_ref>
{
    if (row < 0)
        return std::nullopt;
    const auto* index_item = table.item(row, col::index);
    const auto* numer_item = table.item(row, col::number);
    if (!index_item || !numer_item) [[unlikely]]
        return std::nullopt;
    return core::roi_ref { index_item->data(Qt::UserRole).value<cuuidpp::uuid>(),
        static_cast<std::size_t>(numer_item->data(Qt::UserRole).toInt()) };
}

void info_dock::sync_highlight()
{
    // 选中态唯一事实源:选择联动与程序性删改(删行/清空/切表)均经此收口
    auto* table = qobject_cast<QTableWidget*>(stacked_->currentWidget());
    if (table == empty_)
        table = nullptr;
    sel_table_ = table;
    sel_row_ = table ? table->currentRow() : -1;

    std::optional<core::roi_ref> highlight;
    if (table)
        highlight = row_ref(*table, sel_row_);
    bus_.post<core::event::roi_highlight_changed>(
            cbuspp::value<std::optional<core::roi_ref>> { highlight })
        .sync();
}

}
