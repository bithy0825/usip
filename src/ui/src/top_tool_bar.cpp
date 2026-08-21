#include "top_tool_bar.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QPixmap>
#include <QStyle>
#include <QWidget>

#include <magic_enum/magic_enum.hpp>

#include <cstring>

#include "colormap.hpp"
#include "config.hpp"
#include "event.hpp"
#include "logger.hpp"
#include "menu_bar.hpp"

namespace usip::ui {
namespace {

    // 横向色条图标:256 项 LUT 直铺一行后平滑缩放到目标尺寸;dpr 保证高分屏清晰
    [[nodiscard]] auto make_colormap_swatch(core::colormap_type type,
        const QSize& logical, qreal dpr) -> QIcon
    {
        const auto lut = core::make_color_lut(type, false); // 预览纯表,不烘焙 zero_is_black
        QImage bar { 256, 1, QImage::Format_RGBA8888 };
        std::memcpy(bar.scanLine(0), lut.entries.data(), 256 * sizeof(std::uint32_t));

        auto pm = QPixmap::fromImage(
            bar.scaled(logical * dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        return QIcon { pm };
    }

} // namespace

top_tool_bar::top_tool_bar(menu_bar& menu, cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol<top_tool_bar, QToolBar>(bus, parent)
    , menu_bar_(menu)
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

top_tool_bar::~top_tool_bar() = default;

void top_tool_bar::setup_ui()
{
    setMovable(false);

    // ── 文件操作(共享 menu_bar 的 action) ──────────────────────────────
    addAction(menu_bar_.open_action());
    addAction(menu_bar_.save_action());
    addSeparator();

    // ── 视图开关(共享 menu_bar 的 action;左→右:伪彩、0为黑、mask、ROI、标注)──
    addAction(menu_bar_.pseudocolor_action());
    addAction(menu_bar_.zero_is_black_action());
    addAction(menu_bar_.mask_action());
    addAction(menu_bar_.roi_action());
    addAction(menu_bar_.annotation_action());
    addSeparator();

    // ── 弹簧 ───────────────────────────────────────────────────────────
    auto* spring = new QWidget(this);
    spring->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(spring);

    // ── colormap 色条按钮:点击弹菜单 ────
    const auto dpr = devicePixelRatioF();
    const int icon_h = style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, this);
    const QSize icon_size { icon_h * 2, icon_h }; // 横向色条:宽 = 高 × 2

    colormap_button_ = new QToolButton(this);
    colormap_button_->setIconSize(icon_size);
    colormap_button_->setToolTip(tr("Pseudocolor colormap"));

    auto* menu = new QMenu(colormap_button_);
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);

    const auto current = core::colormap_from_string(
        core::config::global()->get<std::string>("pseudocolor.colormap"))
                             .value_or(core::colormap_type::jet);
    for (const auto type : magic_enum::enum_values<core::colormap_type>()) {
        const auto name = core::to_string(type); // 返回 string literal,静态生存期
        auto* action = menu->addAction(make_colormap_swatch(type, icon_size, dpr),
            QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));
        action->setData(QVariant::fromValue(type));
        action->setCheckable(true);
        group->addAction(action);
        if (type == current) {
            action->setChecked(true);
            colormap_button_->setIcon(action->icon());
        }
        connect(action, &QAction::triggered, this, [this, action, name] {
            colormap_button_->setIcon(action->icon());
            bus_.post<core::event::pseudocolor_colormap_changed>(cbuspp::value<core::colormap_type> { action->data().value<core::colormap_type>() }).sync();
            if (auto r = core::config::global()->set("pseudocolor.colormap", std::string { name });
                !r) {
                common::log_warn("set pseudocolor.colormap failed: {}", r.error());
                return;
            }
        });
    }
    connect(colormap_button_, &QToolButton::clicked, this, [this, menu] {
        menu->popup(colormap_button_->mapToGlobal(QPoint { 0, colormap_button_->height() }));
    });

    addWidget(colormap_button_);
    colormap_button_->setFixedWidth(colormap_button_->sizeHint().height() * 2);

    addSeparator();

    addAction(menu_bar_.about_action());
}

void top_tool_bar::setup_subscriptions() { }

void top_tool_bar::setup_connections() { }

} // namespace usip::ui
