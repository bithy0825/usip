#include "hist_dock.hpp"
#include "icon_registry.hpp"

#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QGridLayout>
#include <QLegend>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>
#include <QValueAxis>
#include <QWheelEvent>

#include <algorithm>
#include <numeric>
#include <span>

namespace usip::ui {

namespace {

    constexpr double zoom_step { 1.25 };
    constexpr double wheel_step { 1.1 };

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
    chart->legend()->hide(); // 图表标题省略(dock 标题栏已表意,省下纵向空间)
    chart->setContentsMargins(0, 0, 0, 0);
    chart->setBackgroundVisible(false);

    auto* series = new QBarSeries(chart);
    series->setBarWidth(0.6);
    bars_ = new QBarSet(QString(), chart);
    bars_->setColor(QColor { 0x4D, 0x4D, 0x4D }); // 默认主题条形过浅,固定深灰
    // 默认白描边:全宽视图下每条约 1px,描边吃掉填色 → 整片白;描边同填色
    bars_->setBorderColor(QColor { 0x4D, 0x4D, 0x4D });
    series->append(bars_);

    x_axis_ = new QValueAxis(chart);
    x_axis_->setRange(0, 256); // bin i 条形占 [i, i+1)
    x_axis_->setLabelFormat("%d");
    x_axis_->setTickInterval(64);
    x_axis_->setMinorTickCount(0);
    x_axis_->setTitleText(tr("Pixel"));

    y_axis_ = new QValueAxis(chart);
    y_axis_->setRange(0, 100); // 初始无数据:0..100,整数刻度 0/25/50/75/100
    y_axis_->setTickCount(5);
    y_axis_->setLabelFormat("%.0f");
    y_axis_->setTitleText(tr("Statistics"));

    chart->addSeries(series);
    chart->addAxis(x_axis_, Qt::AlignBottom);
    chart->addAxis(y_axis_, Qt::AlignLeft);
    series->attachAxis(x_axis_);
    series->attachAxis(y_axis_);

    hist_view_ = new QChartView(chart, container);
    hist_view_->setRenderHint(QPainter::Antialiasing);
    hist_view_->setRubberBand(QChartView::NoRubberBand); // 左键留给拖动平移
    hist_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    hist_view_->viewport()->installEventFilter(this); // 滚轮缩放 + 左键拖动平移

    auto* layout = new QGridLayout(container);
    layout->setContentsMargins(4, 4, 4, 4); // 紧凑:按钮行/图表少留白
    layout->setSpacing(4);
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
    bus_.on<core::event::document_closed>().call(*this, &hist_dock::on_document_closed);
}

void hist_dock::setup_connections()
{
    connect(add_btn_, &QToolButton::clicked, this, [this]() { zoom_view(1.0 / zoom_step); });
    connect(sub_btn_, &QToolButton::clicked, this, [this]() { zoom_view(zoom_step); });
    connect(reset_btn_, &QToolButton::clicked, this, [this]() { reset_view(); });
    connect(mode_combo_, &QComboBox::activated, this, [this](int index) {
        const bool new_percent = index == 1;
        if (new_percent == percent_) // 重选同项:不动(activated 在同项也发)
            return;
        percent_ = new_percent;
        const double denom = std::max<double>(total_, 1.0);
        const double lo = y_axis_->min(), hi = y_axis_->max();
        if (new_percent)
            y_axis_->setRange(lo / denom * 100.0, hi / denom * 100.0);
        else
            y_axis_->setRange(lo * denom / 100.0, hi * denom / 100.0);
        refill_bars();
    });

    // 悬停读数:QBarSet::hovered 内建信号 → tooltip(像素值 + 当前口径计数)
    connect(bars_, &QBarSet::hovered, this, [this](bool status, int index) {
        if (!status) {
            QToolTip::hideText();
            return;
        }
        const double count = static_cast<double>(bins_[index]);
        const QString value = percent_mode()
            ? QString::number(count / std::max<double>(total_, 1.0) * 100.0, 'f', 2) + '%'
            : QString::number(count, 'f', 0);
        QToolTip::showText(QCursor::pos(), tr("Pixel %1: %2").arg(index).arg(value), hist_view_);
    });
}

bool hist_dock::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != hist_view_->viewport())
        return QDockWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Wheel: { // 滚轮缩放
        const auto* wheel = static_cast<const QWheelEvent*>(event);
        hist_view_->chart()->zoom(wheel->angleDelta().y() > 0 ? wheel_step : 1.0 / wheel_step);
        return true;
    }
    case QEvent::MouseButtonPress: { // 左键按下:开始拖动平移
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            panning_ = true;
            pan_last_ = mouse->position();
            hist_view_->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: // 拖动:内容随手(scroll 的 x 反向、y 同向)
        if (panning_) {
            const auto* mouse = static_cast<const QMouseEvent*>(event);
            const QPointF delta = mouse->position() - pan_last_;
            pan_last_ = mouse->position();
            hist_view_->chart()->scroll(-delta.x(), delta.y());
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        if (panning_ && static_cast<const QMouseEvent*>(event)->button() == Qt::LeftButton) {
            panning_ = false;
            hist_view_->unsetCursor();
            return true;
        }
        break;
    default:
        break;
    }
    return QDockWidget::eventFilter(watched, event);
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
        refill_bars();
        reset_view();
        return;
    }

    // 通道 0(灰度主用例;彩色页暂显首通道);u16 的 bin = v>>8,与画布同域
    const auto bins = it->second->info.hist->bins_of(0);
    std::ranges::copy(bins, bins_.begin());
    total_ = std::accumulate(bins.begin(), bins.end(), std::uint64_t { 0 });
    refill_bars();
    reset_view(); // 新页新数据,复位视野
}

void hist_dock::on_document_closed(const cbuspp::value<cuuidpp::uuid>&)
{
    // 就地清空:无剩余文档时不会有后续 switch 重喂(回空白基态);
    // 有剩余则 document_switch 紧随其后重喂,先清无害
    bins_ = { };
    total_ = 0;
    refill_bars();
    reset_view();
}

bool hist_dock::percent_mode() const
{
    return percent_;
}

auto hist_dock::display(double count) const -> double
{
    return percent_mode() ? count / std::max<double>(total_, 1.0) * 100.0 : count;
}

void hist_dock::refill_bars()
{
    const double denom = std::max<double>(total_, 1.0);
    bars_->remove(0, bars_->count());
    for (const auto count : bins_)
        bars_->append(percent_mode() ? count / denom * 100.0 : static_cast<double>(count));
}

void hist_dock::zoom_view(double factor)
{
    if (total_ == 0) [[unlikely]] // 无数据:保持 0..100 占位
        return;
    // 只调 y 顶值(0 起);夹在 [display(1), display(total)]:下限保细节,上限防条形消失
    const double lo = display(1.0), hi = display(static_cast<double>(total_));
    y_axis_->setMax(std::clamp(y_axis_->max() * factor, std::min(lo, hi), std::max(lo, hi)));
}

void hist_dock::reset_view()
{
    x_axis_->setRange(0, 256); // 框选/滚轮可能挪过 x,复位一并还原
    y_axis_->setMin(0);
    if (total_ == 0) [[unlikely]] {
        y_axis_->setMax(100.0); // 无数据:0..100 占位,两种模式刻度同为 0/25/50/75/100
        return;
    }
    // 排除 0 像素(bin 0 常为背景,否则其余条形被压扁);最高条 ≈ 90% 高
    const auto peak = std::ranges::max(std::span<const std::uint64_t> { bins_ }.subspan(1));
    y_axis_->setMax(std::max(display(static_cast<double>(peak)) / 0.9, display(1.0)));
}

}
