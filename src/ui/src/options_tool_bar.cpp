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
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <ranges>

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
    view_none_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    view_none_->setStatusTip(tr("Single view (Ctrl+1)"));
    addAction(view_none_);

    view_split_ = view_group_->addAction(reg.icon("split").value_or(QIcon { }), tr("&Split"));
    view_split_->setCheckable(true);
    view_split_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    view_split_->setStatusTip(tr("Split view: subject left, compare right (Ctrl+2)"));
    addAction(view_split_);

    view_slider_ = view_group_->addAction(reg.icon("slider").value_or(QIcon { }), tr("Sli&der"));
    view_slider_->setCheckable(true);
    view_slider_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    view_slider_->setStatusTip(tr("Slider view: wipe between subject and compare (Ctrl+3)"));
    addAction(view_slider_);

    view_highlight_ = view_group_->addAction(reg.icon("highlight").value_or(QIcon { }), tr("&Highlight"));
    view_highlight_->setCheckable(true);
    view_highlight_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_4));
    view_highlight_->setStatusTip(tr("Highlight differences (Ctrl+4)"));
    addAction(view_highlight_);

    view_difference_ = view_group_->addAction(reg.icon("difference").value_or(QIcon { }), tr("&Difference"));
    view_difference_->setCheckable(true);
    view_difference_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_5));
    view_difference_->setStatusTip(tr("Difference view with colormap (Ctrl+5)"));
    addAction(view_difference_);

    // 模式数据:action → view_mode(触发时随事件发出)
    view_none_->setData(static_cast<int>(core::view_mode::single));
    view_split_->setData(static_cast<int>(core::view_mode::split));
    view_slider_->setData(static_cast<int>(core::view_mode::slider));
    view_highlight_->setData(static_cast<int>(core::view_mode::highlight));
    view_difference_->setData(static_cast<int>(core::view_mode::difference));

    auto* page_label = new QLabel(tr("Compared Page"), this);
    addWidget(page_label);

    page_control_ = new QSpinBox(this);
    // page_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    page_control_->setMinimum(0);
    page_control_->setValue(0);
    page_control_->setEnabled(false); // single 无对比方,禁用;非 single 经模式事件解禁
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
    // 字母区 Return 与数字区 Enter 皆可(单一 shortcut 只认其一)
    apply_->setShortcuts({ QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter) });
    apply_->setStatusTip(tr("Apply (Enter)"));

    cancel_ = new QAction(reg.icon("cancel").value_or(QIcon { }), tr("&Cancel"), this);
    cancel_->setShortcut(QKeySequence(Qt::Key_Escape));
    cancel_->setStatusTip(tr("Cancel (Esc)"));

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
    bus_.on<core::event::document_ready>().call(this, &options_tool_bar::on_document_ready);
    bus_.on<core::event::document_switch>().call(this, &options_tool_bar::on_document_switch);
    bus_.on<core::event::document_closed>().call(this, &options_tool_bar::on_document_closed);
    bus_.on<core::event::view_mode_changed>().call(this, &options_tool_bar::on_view_mode_changed);
    bus_.on<core::event::compare_page_selected>()
        .call(this, &options_tool_bar::on_compare_page_selected);
    // 会话结束经 canvas 统一广播(不直接订阅 apply/canceled)
    bus_.on<core::event::tool_session_ended>().call(this, &options_tool_bar::on_tool_session_ended);
}

void options_tool_bar::setup_connections()
{
    connect(apply_, &QAction::triggered, this, [this] {
        bus_.post<core::event::tool_result_applied>().sync();
    });
    connect(cancel_, &QAction::triggered, this, [this] {
        bus_.post<core::event::tool_result_canceled>().sync();
    });

    // 视图模式组 → 切换请求(canvas 裁决后经 view_mode_changed 状态下发,
    // 取消/校验失败同渠道回退,勾选态只随状态事件)
    connect(view_group_, &QActionGroup::triggered, this, [this](QAction* action) {
        bus_.post<core::event::view_mode_change_requested>(
                cbuspp::value<core::view_mode> {
                    static_cast<core::view_mode>(action->data().toInt()) })
            .sync();
    });
    // 对比页选择:界面与页序同为 0 起
    connect(page_control_, &QSpinBox::valueChanged, this, [this](int value) {
        bus_.post<core::event::compare_page_selected>(cbuspp::value<int> { value }).sync();
    });
}

void options_tool_bar::on_rectangle_draw_requested()
{
    options_stack_->setCurrentWidget(draw_options_);
    page_control_->setEnabled(false); // 会话期禁改对比页(双写落盘的前提;同 measure)
}

void options_tool_bar::on_ellipse_draw_requested()
{
    options_stack_->setCurrentWidget(draw_options_);
    page_control_->setEnabled(false); // 会话期禁改对比页(双写落盘的前提;同 measure)
}

void options_tool_bar::on_polygon_draw_requested()
{
    options_stack_->setCurrentWidget(draw_options_);
    page_control_->setEnabled(false); // 会话期禁改对比页(双写落盘的前提;同 measure)
}

void options_tool_bar::on_threshold_segment_requested()
{
    options_stack_->setCurrentWidget(mask_options_);
    // 会话期禁用:对比页输入 + 与 mask 不兼容的显示模式(single↔split 仍可切换)
    page_control_->setEnabled(false);
    for (auto* action : { view_slider_, view_highlight_, view_difference_ })
        action->setEnabled(false);
}

// 会话结束(canvas 广播,携带模式):选项页复位;对比页输入与显示模式按模式解禁
void options_tool_bar::on_tool_session_ended(const cbuspp::value<core::view_mode>& value)
{
    options_stack_->setCurrentWidget(empty_);
    page_control_->setEnabled(*value != core::view_mode::single);
    for (auto* action : { view_slider_, view_highlight_, view_difference_ })
        action->setEnabled(true);
}

void options_tool_bar::on_measure_requested()
{
    options_stack_->setCurrentWidget(measure_options_);
    page_control_->setEnabled(false); // 会话期禁用对比页输入(标注五模式合法,模式按钮不动)
}

void options_tool_bar::on_document_ready(
    const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    sync_page_control(*value);
}

void options_tool_bar::on_document_switch(
    const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    sync_page_control(*value); // 页切换也经 document_switch 广播,回显跟随激活页
}

void options_tool_bar::on_document_closed(const cbuspp::value<cuuidpp::uuid>&)
{
    // 对比页输入复位到无文档基态;有剩余文档则随紧随的 document_switch 重同步
    // (single 模式禁用经 canvas 广播的 view_mode_changed 落位)
    const QSignalBlocker blocker(page_control_);
    page_control_->setMaximum(0);
    page_control_->setValue(0);
    page_control_->setEnabled(false);
}

// Compared Page:范围 = 0..页数-1;回显激活页已设置的对比页(信号阻断,不回发事件)
void options_tool_bar::sync_page_control(const std::shared_ptr<core::document>& doc)
{
    if (!doc)
        return;

    const QSignalBlocker blocker(page_control_);
    page_control_->setMaximum(
        std::max(0, static_cast<int>(doc->info.pages.size()) - 1));

    int current = 0;
    if (const auto it = doc->pages.find(doc->active_page); it != doc->pages.end()) {
        if (const auto& ct = it->second->compare_to) {
            for (const auto& [i, pinfo] : doc->info.pages | std::views::enumerate) {
                if (pinfo.id == *ct) {
                    current = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    page_control_->setValue(current);
}

// 对比页外部变更(对话框路径经 canvas 广播):回显输入框(阻断,不回发)
void options_tool_bar::on_compare_page_selected(const cbuspp::value<int>& value)
{
    const QSignalBlocker blocker(page_control_);
    page_control_->setValue(*value);
}

// 模式事件回同步勾选态(画布拒绝模式切换时回发 single,此处落回 None)
void options_tool_bar::on_view_mode_changed(const cbuspp::value<core::view_mode>& value)
{
    // Compared Page 仅在有对比方时可用(single 禁用)
    page_control_->setEnabled(*value != core::view_mode::single);

    const auto target = static_cast<int>(*value);
    for (QAction* action : view_group_->actions()) {
        if (action->data().toInt() == target) {
            action->setChecked(true);
            return;
        }
    }
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

void mask_options::setup_subscriptions()
{
    // 会话建立:滑条/spinbox 阻断回显会话起始域(UI 不持有状态)
    bus_.on<core::event::mask_range_echo>().call(*this, &mask_options::on_mask_range_echo);
}

void mask_options::on_mask_range_echo(const cbuspp::value<core::event::mask_range>& value)
{
    sync_range(*value);
}

void mask_options::sync_range(const std::pair<double, double>& range)
{
    // 阻断:回设不得触发 valueChanged 回发 mask_floor/ceiling 事件
    const QSignalBlocker t(threshold_), f(floor_), c(ceil_);
    threshold_->setValues(static_cast<int>(range.first), static_cast<int>(range.second));
    floor_->setValue(static_cast<int>(range.first));
    ceil_->setValue(static_cast<int>(range.second));
}

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
