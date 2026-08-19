#include "options_tool_bar.hpp"
#include "QRangeSlider.hpp"
#include "config.hpp"
#include "event.hpp"
#include "icon_registry.hpp"
#include "menu_bar.hpp"

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QBoxLayout>
#include <QColorDialog>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QWidget>
#include <qkeysequence.h>
#include <qnamespace.h>

namespace usip::ui {

namespace {

    // 16×16 色块(4px 圆角),作为蒙版颜色按钮的图标;dpr 保证高分屏下边缘清晰
    QPixmap make_mask_swatch(const QColor& color, qreal dpr)
    {
        QPixmap pm(qRound(12 * dpr), qRound(12 * dpr));
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(0, 0, 12, 12), 2, 2);

        return pm;
    }

} // namespace

options_tool_bar::options_tool_bar(menu_bar& menu, cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol<options_tool_bar, QToolBar>(bus, parent)
    , menu_bar_(menu)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

options_tool_bar::~options_tool_bar() = default;

void options_tool_bar::setup_ui()
{
    setMovable(false);

    auto& reg = icon_registry::instance();

    view_group_ = new QActionGroup(this);
    view_group_->setExclusive(true);

    view_none_ = view_group_->addAction(reg.icon("none").value_or(QIcon { }), tr("&None"));
    view_none_->setCheckable(true);
    view_none_->setChecked(true);
    addAction(view_none_);

    view_split_ = view_group_->addAction(reg.icon("split").value_or(QIcon { }), tr("&Split"));
    view_split_->setCheckable(true);
    addAction(view_split_);

    view_slider_ = view_group_->addAction(reg.icon("slider").value_or(QIcon { }), tr("Sli&der"));
    view_slider_->setCheckable(true);
    addAction(view_slider_);

    view_highlight_ = view_group_->addAction(reg.icon("highlight").value_or(QIcon { }), tr("&Highlight"));
    view_highlight_->setCheckable(true);
    addAction(view_highlight_);

    view_difference_ = view_group_->addAction(reg.icon("difference").value_or(QIcon { }), tr("&Difference"));
    view_difference_->setCheckable(true);
    addAction(view_difference_);

    auto* page_label = new QLabel(tr("Compared Page"), this);
    addWidget(page_label);

    page_control_ = new QSpinBox(this);
    page_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    page_control_->setMinimum(1);
    page_control_->setValue(1);
    addWidget(page_control_);

    addSeparator();

    options_stack_ = new QStackedWidget(this);
    addWidget(options_stack_);

    auto* spring = new QWidget(this);
    spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(spring);

    addSeparator();

    addAction(menu_bar_.clear_constituency_action());
    addAction(menu_bar_.clear_measurements_action());

    apply_ = new QAction(reg.icon("apply").value_or(QIcon { }), tr("&Apply"), this);
    apply_->setShortcut(QKeySequence(Qt::Key_Enter));

    cancel_ = new QAction(reg.icon("cancel").value_or(QIcon { }), tr("&Cancel"), this);
    cancel_->setShortcut(QKeySequence(Qt::Key_Escape));

    empty_ = new QWidget(this);
    draw_options_ = new draw_options(*this, bus_, this);
    mask_options_ = new mask_options(*this, bus_, this);
    measure_options_ = new measure_options(*this, bus_, this);
    options_stack_->addWidget(empty_);
    options_stack_->addWidget(mask_options_);
    options_stack_->addWidget(draw_options_);
    options_stack_->addWidget(measure_options_);
}

void options_tool_bar::setup_subscriptions()
{
    bus_.on<core::event::rectangle_draw_requested>().call(this, &options_tool_bar::on_rectangle_draw_requested);
    bus_.on<core::event::ellipse_draw_requested>().call(this, &options_tool_bar::on_ellipse_draw_requested);
    bus_.on<core::event::polygon_draw_requested>().call(this, &options_tool_bar::on_polygon_draw_requested);
    bus_.on<core::event::threshold_segment_requested>().call(this, &options_tool_bar::on_threshold_segment_requested);
    bus_.on<core::event::measure_requested>().call(this, &options_tool_bar::on_measure_requested);
}

void options_tool_bar::setup_connections()
{
    connect(apply_, &QAction::triggered, this, [this] {
        bus_.post<core::event::tool_result_applied>().sync();
    });
    connect(cancel_, &QAction::triggered, this, [this] {
        bus_.post<core::event::tool_result_canceled>().sync();
    });
}

void options_tool_bar::on_rectangle_draw_requested()
{
    options_stack_->setCurrentWidget(draw_options_);
}

void options_tool_bar::on_ellipse_draw_requested()
{
    options_stack_->setCurrentWidget(draw_options_);
}

void options_tool_bar::on_polygon_draw_requested()
{
    options_stack_->setCurrentWidget(draw_options_);
}

void options_tool_bar::on_threshold_segment_requested()
{
    options_stack_->setCurrentWidget(mask_options_);
}

void options_tool_bar::on_measure_requested()
{
    options_stack_->setCurrentWidget(measure_options_);
}

// ---------------------------------------------------------------------------
// draw_options
// ---------------------------------------------------------------------------
draw_options::draw_options(options_tool_bar& opt_tool_bar, cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
    , opt_tool_bar_(opt_tool_bar)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

draw_options::~draw_options() = default;

void draw_options::setup_ui()
{
    setMovable(false);

    addAction(opt_tool_bar_.apply_action());
    addAction(opt_tool_bar_.cancel_action());
}

void draw_options::setup_subscriptions() { }

void draw_options::setup_connections() { }

// ---------------------------------------------------------------------------
// mask_options
// ---------------------------------------------------------------------------

mask_options::mask_options(options_tool_bar& opt_tool_bar, cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
    , opt_tool_bar_(opt_tool_bar)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

mask_options::~mask_options() = default;

void mask_options::setup_ui()
{
    setMovable(false);

    auto* range_label = new QLabel(tr("Range"), this);
    floor_ = new QSpinBox(this);
    floor_->setRange(0, 255);

    threshold_ = new QRangeSlider(Qt::Horizontal, this);
    threshold_->setRange(0, 255);
    threshold_->setValues(0, 255);
    threshold_->setMinimumWidth(240);

    ceil_ = new QSpinBox(this);
    ceil_->setRange(0, 255);
    ceil_->setValue(255);

    // UI 不持有状态:初始值直接读 config
    auto* color_label = new QLabel(tr("Mask Color"), this);
    const QColor color(QString::fromStdString(core::config::global()->get<std::string>("mask.color")));
    color_ = new QToolButton(this);
    color_->setIcon(make_mask_swatch(color.isValid() ? color : QColor(Qt::red), devicePixelRatioF()));
    color_->setToolTip(tr("Mask color"));

    auto* opacity_label = new QLabel(tr("Opacity"), this);
    // config 存 0.0~1.0,滑块为 0~100
    const int opacity = qRound(core::config::global()->get<float>("mask.opacity") * 100.0F);
    opacity_ = new QSlider(Qt::Horizontal, this);
    opacity_->setRange(0, 100);
    opacity_->setValue(opacity);
    opacity_->setFixedWidth(120);
    opacity_->setToolTip(tr("Mask opacity"));

    // 颜色/不透明度组
    addWidget(color_label);
    addWidget(color_);
    addWidget(opacity_label);
    addWidget(opacity_);

    addSeparator();

    // 范围组:label, floor, slider, ceil
    addWidget(range_label);
    addWidget(floor_);
    addWidget(threshold_);
    addWidget(ceil_);

    // 分割线 + 父级共享的 apply/cancel(事件由 options_tool_bar 管理)
    addSeparator();
    addAction(opt_tool_bar_.apply_action());
    addAction(opt_tool_bar_.cancel_action());
}

void mask_options::setup_subscriptions() { }

void mask_options::setup_connections()
{
    auto reg = core::config::global();
    connect(threshold_, &QRangeSlider::lowerValueChanged, this, [this](int value) {
        floor_->setValue(value);
        bus_.post<core::event::mask_floor_changed>(cbuspp::value<double>(value)).sync();
    });
    connect(threshold_, &QRangeSlider::upperValueChanged, this, [this](int value) {
        ceil_->setValue(value);
        bus_.post<core::event::mask_ceiling_changed>(cbuspp::value<double>(value)).sync();
    });
    connect(floor_, &QSpinBox::valueChanged, this, [this](int value) {
        threshold_->setLowerValue(value);
        bus_.post<core::event::mask_floor_changed>(cbuspp::value<double>(value)).sync();
    });
    connect(ceil_, &QSpinBox::valueChanged, this, [this](int value) {
        threshold_->setUpperValue(value);
        bus_.post<core::event::mask_ceiling_changed>(cbuspp::value<double>(value)).sync();
    });
    connect(opacity_, &QSlider::valueChanged, this, [this, reg](int value) {
        bus_.post<core::event::mask_opacity_changed>(cbuspp::value<double>(value / 100.0)).sync();
        [[maybe_unused]] auto res = reg->set<double>("mask.opacity", value / 100.0);
    });

    connect(color_, &QToolButton::clicked, this, [this, reg] {
        const QColor initial(QString::fromStdString(reg->get<std::string>("mask.color")));
        const QColor picked = QColorDialog::getColor(initial, this, tr("Mask Color"));
        if (!picked.isValid())
            return;
        bus_.post<core::event::mask_color_changed>(cbuspp::value<QColor>(picked)).sync();
        [[maybe_unused]] auto res = reg->set<std::string>("mask.color", picked.name().toStdString());
        color_->setIcon(make_mask_swatch(picked, devicePixelRatioF()));
    });
}

measure_options::measure_options(options_tool_bar& opt_tool_bar, cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
    , opt_tool_bar_(opt_tool_bar)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

measure_options::~measure_options() = default;

void measure_options::setup_ui()
{
    setMovable(false);

    auto* width_label = new QLabel(tr("Line Width"), this);
    line_width_ = new QSpinBox(this);
    line_width_->setRange(1, 10);
    line_width_->setValue(core::config::global()->get<int>("measure.line_width"));

    auto* color_label = new QLabel(tr("Line Color"), this);
    const QColor color(QString::fromStdString(core::config::global()->get<std::string>("measure.line_color")));
    color_ = new QToolButton(this);
    color_->setIcon(make_mask_swatch(color.isValid() ? color : QColor(Qt::green), devicePixelRatioF()));
    color_->setToolTip(tr("Line color"));

    addWidget(width_label);
    addWidget(line_width_);
    addWidget(color_label);
    addWidget(color_);

    addSeparator();

    addAction(opt_tool_bar_.apply_action());
    addAction(opt_tool_bar_.cancel_action());
}

void measure_options::setup_subscriptions() { }

void measure_options::setup_connections()
{
    auto reg = core::config::global();

    connect(line_width_, &QSpinBox::valueChanged, this, [this, reg](int value) {
        bus_.post<core::event::measure_line_width_changed>(cbuspp::value<int>(value)).sync();
        [[maybe_unused]] auto res = reg->set<int>("measure.line_width", value);
    });

    connect(color_, &QToolButton::clicked, this, [this, reg] {
        const QColor initial(QString::fromStdString(reg->get<std::string>("measure.line_color")));
        const QColor picked = QColorDialog::getColor(initial, this, tr("Measure Line Color"));
        if (!picked.isValid())
            return;
        bus_.post<core::event::measure_line_color_changed>(cbuspp::value<QColor>(picked)).sync();
        [[maybe_unused]] auto res = reg->set<std::string>("measure.line_color", picked.name().toStdString());
        color_->setIcon(make_mask_swatch(picked, devicePixelRatioF()));
    });
}

} // namespace usip::ui
