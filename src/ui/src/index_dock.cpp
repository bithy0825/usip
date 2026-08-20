#include "index_dock.hpp"
#include "icon_registry.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <vector>

namespace usip::ui {

namespace {

    // 页统计摘要(全部由直方图导出;bin 域为 8 位显示域,与 hist_dock 同口径)
    struct page_stats {
        std::uint64_t valid { 0 }; // 非 0 像素(bin 0 之外的样本数)
        double mean { 0.0 };
        double std_dev { 0.0 };
        double min { 0.0 };
        double max { 0.0 };
    };

    // 多通道页合并全部通道样本;channels == 0(无直方图)→ 全零,调用方留空单元格
    [[nodiscard]] auto stats_from_hist(const common::histogram& hist) -> page_stats
    {
        page_stats st;
        if (hist.channels == 0) [[unlikely]]
            return st;

        std::array<std::uint64_t, common::histogram::bin_count> merged { };
        for (std::uint16_t c = 0; c < hist.channels; ++c) {
            const auto bins = hist.bins_of(c);
            for (std::size_t b = 0; b < merged.size(); ++b)
                merged[b] += bins[b];
        }

        std::uint64_t total { 0 };
        double sum { 0.0 };
        for (std::size_t b = 0; b < merged.size(); ++b) {
            total += merged[b];
            if (b != 0) [[likely]]
                st.valid += merged[b];
            sum += static_cast<double>(b) * static_cast<double>(merged[b]);
        }
        if (total == 0) [[unlikely]]
            return st;

        st.mean = sum / static_cast<double>(total);
        double acc { 0.0 };
        for (std::size_t b = 0; b < merged.size(); ++b) {
            const double d = static_cast<double>(b) - st.mean;
            acc += static_cast<double>(merged[b]) * d * d;
        }
        st.std_dev = std::sqrt(acc / static_cast<double>(total));

        // Min/Max 亦由 bins 推出(首/末非零 bin):整行统一 8 位显示域;
        // total > 0 已保证存在非零 bin(find_last_if 按标准还回命中处的
        // subrange,故取其 begin 再求下标)
        const auto nonzero = [](std::uint64_t count) { return count != 0; };
        const auto first = std::ranges::find_if(merged.begin(), merged.end(), nonzero);
        const auto last = std::ranges::find_last_if(merged.begin(), merged.end(), nonzero);
        st.min = static_cast<double>(first - merged.begin());
        st.max = static_cast<double>(last.begin() - merged.begin());
        return st;
    }

    // 每页一行:Index/ROIs/Pixel 恒有,直方图导出的五列仅在有直方图时填;
    // Index 单元格 UserRole 绑页 uuid(行→uuid 方向,与 combobox 同款),
    // ROIs 单元格登记进 page_items(uuid→行方向:反查 row/tableWidget)
    void fill_table(QTableWidget& table, const core::document& doc,
        std::unordered_map<cuuidpp::uuid, QTableWidgetItem*>& page_items)
    {
        // pages 是 uuid 键的哈希表:按页序(从 0)排好再落行
        std::vector<const core::page*> ordered;
        ordered.reserve(doc.pages.size());
        for (const auto& page : doc.pages | std::views::values)
            ordered.push_back(page.get());
        std::ranges::sort(ordered, { }, &core::page::index);

        table.setRowCount(static_cast<int>(ordered.size()));
        for (std::size_t row = 0; row < ordered.size(); ++row) {
            const auto* pg = ordered[row];
            const auto set_cell = [&table, row](int col, auto value) -> QTableWidgetItem* {
                auto* item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                item->setData(Qt::DisplayRole, value);
                table.setItem(static_cast<int>(row), col, item);
                return item;
            };

            set_cell(0, pg->index)->setData(Qt::UserRole, QVariant::fromValue(pg->info.id));
            page_items.emplace(pg->info.id, set_cell(1, pg->rois.size()));
            set_cell(2, static_cast<std::uint64_t>(pg->info.width) * pg->info.height);

            if (!pg->info.hist)
                continue;
            const auto st = stats_from_hist(*pg->info.hist);
            set_cell(3, st.valid);
            set_cell(4, st.mean);
            set_cell(5, st.std_dev);
            set_cell(6, st.min);
            set_cell(7, st.max);
        }
    }

} // namespace

index_dock::index_dock(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

index_dock::~index_dock() = default;

void index_dock::setup_ui()
{
    setWindowTitle(tr("File"));
    setWindowIcon(icon_registry::instance().icon("scene_collection").value_or(QIcon()));
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* container = new QWidget(this);
    doc_ = new QComboBox(container);
    x_step_ = new QDoubleSpinBox(container);
    y_step_ = new QDoubleSpinBox(container);
    for (auto* step : { x_step_, y_step_ }) { // mm/像素:初值 1.00,范围 [0.01,100.0],步进 0.25
        step->setDecimals(2);
        step->setRange(0.01, 100.0);
        step->setSingleStep(0.25);
        step->setValue(1.00);
    }
    stacked_ = new QStackedWidget(container);
    empty_ = make_table(container);
    stacked_->addWidget(empty_);
    stacked_->setCurrentWidget(empty_);
    stacked_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QGridLayout(container);
    layout->addWidget(new QLabel(tr("Doc:")), 0, 0, 1, 1);
    layout->addWidget(doc_, 0, 1, 1, 3);
    layout->addWidget(new QLabel(tr("X Step:")), 1, 0);
    layout->addWidget(x_step_, 1, 1);
    layout->addWidget(new QLabel(tr("Y Step:")), 1, 2);
    layout->addWidget(y_step_, 1, 3);
    layout->addWidget(stacked_, 2, 0, 1, 4);

    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 0);
    layout->setRowStretch(2, 1);
    layout->setColumnStretch(0, 0);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 0);
    layout->setColumnStretch(3, 1);
    setWidget(container);
}

void index_dock::setup_subscriptions()
{
    bus_.on<core::event::document_ready>().call(*this, &index_dock::on_document_ready);
    bus_.on<core::event::document_switch>().call(*this, &index_dock::on_document_switch);
    bus_.on<core::event::page_rois_changed>().call(*this, &index_dock::on_page_rois_changed);
    // 工具会话:开启禁用(观察者:订阅原始 request 自推导),结束经 canvas 广播解禁
    bus_.on<core::event::threshold_segment_requested>()
        .call(*this, &index_dock::on_tool_session_started);
    bus_.on<core::event::measure_requested>().call(*this, &index_dock::on_tool_session_started);
    bus_.on<core::event::tool_session_ended>().call(*this, &index_dock::on_tool_session_ended);
}

void index_dock::setup_connections()
{
    // activated 仅由用户选择触发(程序性 setCurrentIndex 不发);请求服务按
    // uuid 解析并广播 document_switch,各处(画布/本 dock)随之同步
    connect(doc_, &QComboBox::activated, this, [this](int index) {
        bus_.post<core::event::document_switch_requested>(
                cbuspp::value<cuuidpp::uuid> { doc_->itemData(index).value<cuuidpp::uuid>() })
            .sync();
    });

    // 采集步长(逐轴):画布写 document.step 并按简化语义清除标注
    connect(x_step_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        bus_.post<core::event::step_x_changed>(cbuspp::value<double> { value }).sync();
    });
    connect(y_step_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        bus_.post<core::event::step_y_changed>(cbuspp::value<double> { value }).sync();
    });
}

void index_dock::sync_step(const core::document& doc)
{
    // 阻断:程序性回设不得触发 valueChanged 回发 step 事件
    const QSignalBlocker x { x_step_ }, y { y_step_ };
    x_step_->setValue(doc.step.first);
    y_step_->setValue(doc.step.second);
}

void index_dock::on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc) [[unlikely]]
        return;

    auto* table = make_table(stacked_);
    fill_table(*table, *doc, page_items_);
    tables_.emplace(doc->info.id, table);
    stacked_->addWidget(table);
    stacked_->setCurrentWidget(table);
    select_page_row(doc->active_page); // 选中激活页行(阻断,不发请求)

    // 阻断信号:程序性添加/选中不得触发 activated(否则回发切换请求)
    const QSignalBlocker blocker { doc_ };
    doc_->addItem(QString::fromStdString(doc->info.path.filename().string()),
        QVariant::fromValue(doc->info.id));
    doc_->setCurrentIndex(doc_->count() - 1);
    sync_step(*doc); // 新文档读入自己的 step
}

void index_dock::on_document_switch(const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc) // 空载荷(如重复打开的提醒)不切
        return;

    // 同步 combobox(阻断防回环;findData 按 UserRole 的 uuid 直找索引)与表格页
    const QSignalBlocker blocker { doc_ };
    if (const int index = doc_->findData(QVariant::fromValue(doc->info.id)); index >= 0)
        doc_->setCurrentIndex(index);
    if (const auto it = tables_.find(doc->info.id); it != tables_.end())
        stacked_->setCurrentWidget(it->second);
    select_page_row(doc->active_page); // 页切换链路回Sync行选择(阻断,不回发)
    sync_step(*doc); // 切文档读入新 step(各文档 step 独立)
}

void index_dock::on_page_rois_changed(const cbuspp::value<std::shared_ptr<core::page>>& value)
{
    const auto& page = *value;
    if (!page) [[unlikely]]
        return;

    if (const auto it = page_items_.find(page->info.id); it != page_items_.end() && it->second)
        it->second->setData(Qt::DisplayRole, page->rois.size());
}

void index_dock::on_tool_session_started()
{
    setEnabled(false); // 会话期禁用:doc 切换、step、页切换全在其中
}

void index_dock::on_tool_session_ended(const cbuspp::value<core::view_mode>&)
{
    setEnabled(true);
}

QTableWidget* index_dock::make_table(QWidget* parent)
{
    QTableWidget* table = new QTableWidget(parent);
    table->setColumnCount(8);
    table->setHorizontalHeaderLabels({ tr("Index"), tr("ROIs"), tr("Pixel"), tr("Valid"), tr("Mean"), tr("Std"),
        tr("Min"), tr("Max") });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 一次只选整行
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);

    table->verticalHeader()->hide();
    // 行选中 → 请求切换激活页(uuid 绑在 Index 单元格 UserRole)
    connect(table, &QTableWidget::itemSelectionChanged, this, [this, table]() {
        const int row = table->currentRow();
        if (row < 0)
            return;
        if (const auto* item = table->item(row, 0))
            bus_.post<core::event::page_switch_requested>(cbuspp::value<cuuidpp::uuid> {
                                                              item->data(Qt::UserRole).value<cuuidpp::uuid>() })
                .sync();
    });
    return table;
}

void index_dock::select_page_row(const cuuidpp::uuid& page_id)
{
    // page_items 反查行与所属表;阻断防程序性选择回发请求
    const auto it = page_items_.find(page_id);
    if (it == page_items_.end() || !it->second)
        return;
    auto* table = it->second->tableWidget();
    if (!table)
        return;
    const QSignalBlocker blocker { table };
    table->selectRow(it->second->row());
}

}
