#include "hist_dock.hpp"
#include "icon_registry.hpp"

#include <QBarSet>
#include <QBarSeries>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QGridLayout>
#include <QLegend>
#include <QPainter>
#include <QToolButton>
#include <QValueAxis>

#include <algorithm>
#include <numeric>
#include <span>

namespace usip::ui {

namespace {

    // 纵向视野:每步缩放系数;顶值夹在 [1, total](下限保细节,上限防条形消失)
    constexpr double zoom_step { 1.25 };

}

hist_dock::hist_dock(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

hist_dock::~hist_dock() = default;

void hist_dock::setup_ui()
{
    setWindowTitle(tr("Histogram"));
    setWindowIcon(icon_registry::instance().icon("histogram").value_or(QIcon()));
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* container = new QWidget(this);

    // ── 按钮行 ───────────────────────────────────────────────────────
    add_btn_ = new QToolButton(container);
    add_btn_->setText("+");
    add_btn_->setToolTip(tr("Add"));

    sub_btn_ = new QToolButton(container);
    sub_btn_->setText("−");
    sub_btn_->setToolTip(tr("Subtract"));

    reset_btn_ = new QToolButton(container);
    reset_btn_->setText(tr("Reset"));

    mode_combo_ = new QComboBox(container);
    mode_combo_->addItem(tr("Count"));
    mode_combo_->addItem(tr("Percent"));

    // ── QChart 直方图:256 bin 条形,主题/字体随 Qt 全局风格 ──────────
    auto* chart = new QChart();
    chart->legend()->hide();
    chart->setContentsMargins(0, 0, 0, 0);
    chart->setBackgroundVisible(false);

    auto* series = new QBarSeries(chart);
    bars_ = new QBarSet(QString(), chart);
    series->append(bars_);

    auto* x_axis = new QValueAxis(chart);
    x_axis->setRange(0, 256); // bin i 条形占 [i, i+1)
    x_axis->setLabelFormat("%d");
    x_axis->setTickInterval(64);
    x_axis->setMinorTickCount(0);

    y_axis_ = new QValueAxis(chart);
    y_axis_->setRange(0, 1); // 由 apply_y_axis() 接管
    y_axis_->setLabelFormat("%.0f");

    chart->addSeries(series);
    chart->addAxis(x_axis, Qt::AlignBottom);
    chart->addAxis(y_axis_, Qt::AlignLeft);
    series->attachAxis(x_axis);
    series->attachAxis(y_axis_);

    hist_view_ = new QChartView(chart, container);
    hist_view_->setRenderHint(QPainter::Antialiasing);
    hist_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QGridLayout(container);
    layout->addWidget(add_btn_, 0, 0);
    layout->addWidget(sub_btn_, 0, 1);
    layout->addWidget(reset_btn_, 0, 2);
    layout->addWidget(mode_combo_, 0, 4);
    layout->addWidget(hist_view_, 1, 0, 1, 5);

    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 1);
    layout->setColumnStretch(0, 0);
    layout->setColumnStretch(1, 0);
    layout->setColumnStretch(2, 0);
    layout->setColumnStretch(3, 1);
    layout->setColumnStretch(4, 0);

    setWidget(container);
}

void hist_dock::setup_subscriptions()
{
    // 页切换链路复用 document_switch,翻页自动重喂
    bus_.on<core::event::document_ready>().call(*this, &hist_dock::on_document_changed);
    bus_.on<core::event::document_switch>().call(*this, &hist_dock::on_document_changed);
}

void hist_dock::setup_connections()
{
    connect(add_btn_, &QToolButton::clicked, this, [this]() {
        top_raw_ = std::clamp(top_raw_ / zoom_step, 1.0, std::max<double>(total_, 1.0));
        apply_y_axis();
    });
    connect(sub_btn_, &QToolButton::clicked, this, [this]() {
        top_raw_ = std::clamp(top_raw_ * zoom_step, 1.0, std::max<double>(total_, 1.0));
        apply_y_axis();
    });
    connect(reset_btn_, &QToolButton::clicked, this, [this]() { reset_view(); });
    connect(mode_combo_, &QComboBox::activated, this, [this](int) {
        refill_bars(); // 换算口径变,条形数值重填
        apply_y_axis();
    });
}

void hist_dock::on_document_changed(const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc) [[unlikely]] // 空载荷(重复打开的提醒)不动
        return;

    const auto it = doc->pages.find(doc->active_page);
    if (it == doc->pages.end() || !it->second->info.hist) [[unlikely]] {
        bins_ = { };
        total_ = 0;
        top_raw_ = 1.0;
        refill_bars();
        apply_y_axis();
        return;
    }

    // 通道 0(灰度主用例;彩色页暂显首通道);u16 的 bin = v>>8,与画布同域
    const auto bins = it->second->info.hist->bins_of(0);
    std::ranges::copy(bins, bins_.begin());
    total_ = std::accumulate(bins.begin(), bins.end(), std::uint64_t { 0 });
    refill_bars();
    reset_view(); // 新页新数据,复位视野
}

void hist_dock::refill_bars()
{
    const bool percent = mode_combo_->currentIndex() == 1;
    const double denom = std::max<double>(total_, 1.0);
    bars_->remove(0, bars_->count());
    for (const auto count : bins_)
        bars_->append(percent ? count / denom * 100.0 : static_cast<double>(count));
}

void hist_dock::apply_y_axis()
{
    const bool percent = mode_combo_->currentIndex() == 1;
    y_axis_->setMin(0);
    y_axis_->setMax(percent ? top_raw_ / std::max<double>(total_, 1.0) * 100.0 : top_raw_);
    y_axis_->setLabelFormat(percent ? "%.1f" : "%.0f");
}

void hist_dock::reset_view()
{
    // 排除 0 像素(bin 0 常为背景,否则其余条形被压扁);最高条 ≈ 90% 高
    const auto peak = std::ranges::max(std::span<const std::uint64_t> { bins_ }.subspan(1));
    top_raw_ = std::max(static_cast<double>(peak) / 0.9, 1.0);
    apply_y_axis();
}

}
