// ==============================================================================
// canvas.cpp — 画布:事件订阅与状态管理;paintEvent 按模式组织层调用与几何;
// 滚轮锚点缩放 / 右键平移;对比页选取与校验
// ==============================================================================

#include "canvas.hpp"

#include <QInputDialog>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QSlider>
#include <QTransform>
#include <QWheelEvent>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "config.hpp"
#include "event.hpp"

namespace usip::ui {
namespace {

    // 构造时从 config 播种 options(部分键;未注册的保留结构默认)
    [[nodiscard]] auto snapshot_options() -> options
    {
        const auto* cfg = core::config::global();

        options opts;
        opts.pseudocolor_enabled = cfg->get<bool>("pseudocolor.enabled"); // 未注册 → false
        opts.pseudocolor_colormap = core::colormap_from_string(
            cfg->get<std::string>("pseudocolor.colormap"))
                                        .value_or(core::colormap_type::jet);
        opts.zero_is_black = cfg->get<bool>("pseudocolor.zero_is_black");
        opts.mask_color = QColor(QString::fromStdString(cfg->get<std::string>("mask.color")));
        opts.mask_opacity = cfg->get<double>("mask.opacity");
        opts.line_width = cfg->get<int>("measure.line_width");
        opts.line_color
            = QColor(QString::fromStdString(cfg->get<std::string>("measure.line_color")));
        return opts;
    }

    // 阈值分割:8 位显示域比较(u16 取 >> 8,与直方图 bin 同域);仅灰度页,
    // 彩色页返回空(保留旧 mask)。产物与原始页同尺寸同朝向,orient 由 L3 对齐
    [[nodiscard]] auto make_threshold_mask(const QImage& img, double floor, double ceil)
        -> QImage
    {
        if (img.isNull())
            return { };

        QImage out { img.size(), QImage::Format_Grayscale8 };
        switch (img.format()) {
        case QImage::Format_Grayscale8:
            for (int y = 0; y < img.height(); ++y) {
                const auto* src = img.constScanLine(y);
                auto* dst = out.scanLine(y);
                for (int x = 0; x < img.width(); ++x) {
                    const auto v = static_cast<double>(src[x]);
                    dst[x] = v >= floor && v <= ceil ? 255 : 0;
                }
            }
            break;
        case QImage::Format_Grayscale16:
            for (int y = 0; y < img.height(); ++y) {
                const auto* src
                    = reinterpret_cast<const std::uint16_t*>(img.constScanLine(y));
                auto* dst = out.scanLine(y);
                for (int x = 0; x < img.width(); ++x) {
                    const auto v = static_cast<double>(src[x] >> 8);
                    dst[x] = v >= floor && v <= ceil ? 255 : 0;
                }
            }
            break;
        default:
            return { };
        }
        return out;
    }

} // namespace

canvas::canvas(cbuspp::bus<common::executor>& bus, QWidget* parent)
    : ui_protocol(bus, parent)
    , options_(snapshot_options())
{
    setup_ui();
    setup_subscriptions();
    setup_connections();
}

canvas::~canvas() = default;

void canvas::setup_ui()
{
    setMinimumSize(200, 200); // 防止 dock 挤压成零尺寸
    setMouseTracking(false);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 100);
    slider_->setValue(50);
    slider_->hide(); // 仅 slider 模式可见
}

void canvas::setup_subscriptions()
{
    bus_.on<core::event::document_ready>().call(*this, &canvas::on_document_ready);
    bus_.on<core::event::document_switch>().call(*this, &canvas::on_document_switch);
    bus_.on<core::event::view_mode_changed>().call(*this, &canvas::on_view_mode_changed);
    bus_.on<core::event::compare_page_selected>()
        .call(*this, &canvas::on_compare_page_selected);
    bus_.on<core::event::pseudocolor_enabled_toggled>()
        .call(*this, &canvas::on_pseudocolor_enabled_toggled);
    bus_.on<core::event::pseudocolor_colormap_changed>()
        .call(*this, &canvas::on_pseudocolor_colormap_changed);
    bus_.on<core::event::pseudocolor_zero_is_black_toggled>()
        .call(*this, &canvas::on_pseudocolor_zero_is_black_toggled);
    bus_.on<core::event::mask_visible_toggled>().call(*this, &canvas::on_mask_visible_toggled);
    bus_.on<core::event::mask_color_changed>().call(*this, &canvas::on_mask_color_changed);
    bus_.on<core::event::mask_opacity_changed>().call(*this, &canvas::on_mask_opacity_changed);
    bus_.on<core::event::mask_floor_changed>().call(*this, &canvas::on_mask_floor_changed);
    bus_.on<core::event::mask_ceiling_changed>().call(*this, &canvas::on_mask_ceiling_changed);
    bus_.on<core::event::measure_line_width_changed>()
        .call(*this, &canvas::on_measure_line_width_changed);
    bus_.on<core::event::measure_line_color_changed>()
        .call(*this, &canvas::on_measure_line_color_changed);
}

void canvas::setup_connections()
{
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        view_.split = value / 100.0; // 缝是视图态,只动 clip 不动内容
        update();
    });
}

// ─── 总线回调 ─────────────────────────────────────────────────────────────────

void canvas::on_document_ready(const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc)
        return;
    doc_ = doc;
    resolve_pages();
    // 页/文档变 → 全部层缓存重建(L4-L6 实现时同清)
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { };
    l3_img_ = { };
    view_dirty_ = true; // 新文档:延迟到尺寸有效时适配居中
    // 对比模式下新页无有效对比页 → 询问或回退 single(工具栏经订阅回同步)
    if (options_.mode != core::view_mode::single && compare_page_.expired()
        && !ensure_compare_page()) {
        options_.mode = core::view_mode::single;
        slider_->hide();
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { core::view_mode::single })
            .sync();
    }
    update();
}

void canvas::on_document_switch(
    const cbuspp::value<std::shared_ptr<core::document>>& value)
{
    const auto& doc = *value;
    if (!doc) // 空载荷(如重复打开的提醒)不切
        return;
    doc_ = doc;
    resolve_pages();
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { };
    l3_img_ = { };
    view_dirty_ = true;
    if (options_.mode != core::view_mode::single && compare_page_.expired()
        && !ensure_compare_page()) {
        options_.mode = core::view_mode::single;
        slider_->hide();
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { core::view_mode::single })
            .sync();
    }
    update();
}

void canvas::on_view_mode_changed(const cbuspp::value<core::view_mode>& value)
{
    const auto mode = *value;
    if (mode == options_.mode)
        return;

    // 进对比模式前:须存在已校验的对比页(未设弹对话框;取消/失败 → 保持 single);
    // 无文档时先收下选择,待 document_ready/switch 的补校验弹对话框(取消则回退 single)
    if (mode != core::view_mode::single && !page_.expired() && !ensure_compare_page()) {
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { core::view_mode::single })
            .sync();
        return;
    }

    options_.mode = mode;
    slider_->setVisible(mode == core::view_mode::slider);
    l2_img_ = { }; // 运算层内容随模式变
    view_dirty_ = true; // 视口定义变(整视口 ↔ 半区),重新适配
    update();
}

void canvas::on_compare_page_selected(const cbuspp::value<int>& value)
{
    const auto page = page_.lock();
    const auto doc = doc_.lock();
    if (!page || !doc)
        return;
    const auto idx = *value; // 0 起页序
    if (idx < 0 || std::cmp_greater_equal(idx, doc->info.pages.size()))
        return;

    page->compare_to = doc->info.pages[static_cast<std::size_t>(idx)].id; // 显式选择,写入页
    l1c_img_ = { };
    l2_img_ = { };
    // 已在对比模式:立刻校验新对比页,失败回退 single
    if (options_.mode != core::view_mode::single && !validate_compare()) {
        options_.mode = core::view_mode::single;
        slider_->hide();
        bus_.post<core::event::view_mode_changed>(
            cbuspp::value<core::view_mode> { core::view_mode::single })
            .sync();
    }
    update();
}

void canvas::on_pseudocolor_enabled_toggled(const cbuspp::value<bool>& value)
{
    options_.pseudocolor_enabled = *value;
    l1_img_ = { };
    l1c_img_ = { };
    update();
}

void canvas::on_pseudocolor_colormap_changed(const cbuspp::value<core::colormap_type>& value)
{
    options_.pseudocolor_colormap = *value;
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { }; // difference 的 diff LUT 由所选 colormap 派生
    update();
}

void canvas::on_pseudocolor_zero_is_black_toggled(const cbuspp::value<bool>& value)
{
    options_.zero_is_black = *value;
    l1_img_ = { };
    l1c_img_ = { };
    l2_img_ = { };
    update();
}

void canvas::on_mask_visible_toggled(const cbuspp::value<bool>& value)
{
    options_.mask_enabled = *value; // 可见性不动 L3 缓存(重开时原图直接用)
    update();
}

void canvas::on_mask_color_changed(const cbuspp::value<QColor>& value)
{
    options_.mask_color = *value;
    l3_img_ = { };
    update();
}

void canvas::on_mask_opacity_changed(const cbuspp::value<double>& value)
{
    options_.mask_opacity = *value;
    l3_img_ = { };
    update();
}

void canvas::on_mask_floor_changed(const cbuspp::value<double>& value)
{
    const auto page = page_.lock();
    if (!page)
        return;
    if (!page->mask) // 惰性:未初始化的页此刻补建
        page->mask.emplace();
    page->mask->range.first = *value;
    rethreshold_mask();
}

void canvas::on_mask_ceiling_changed(const cbuspp::value<double>& value)
{
    const auto page = page_.lock();
    if (!page)
        return;
    if (!page->mask)
        page->mask.emplace();
    page->mask->range.second = *value;
    rethreshold_mask();
}

void canvas::on_measure_line_width_changed(const cbuspp::value<int>& value)
{
    options_.line_width = *value; // L5 未实现:仅暂存
}

void canvas::on_measure_line_color_changed(const cbuspp::value<QColor>& value)
{
    options_.line_color = *value;
}

// ─── 页解析与校验 ─────────────────────────────────────────────────────────────

void canvas::resolve_pages()
{
    page_.reset();
    compare_page_.reset();
    const auto doc = doc_.lock();
    if (!doc)
        return;
    if (const auto it = doc->pages.find(doc->active_page); it != doc->pages.end()) {
        page_ = it->second;
        if (const auto& ct = it->second->compare_to) { // 顺带解析已设置的对比页
            if (const auto cit = doc->pages.find(*ct); cit != doc->pages.end())
                compare_page_ = cit->second;
        }
    }
}

auto canvas::ensure_compare_page() -> bool
{
    const auto page = page_.lock();
    const auto doc = doc_.lock();
    if (!page || !doc)
        return false;

    if (!page->compare_to) { // 未设对比页 → 对话框选页(确认才写入)
        const auto last = static_cast<int>(doc->info.pages.size()) - 1;
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Select Compare Page"),
            tr("Page (0-%1):").arg(last), 0, 0, std::max(0, last), 1, &ok);
        if (!ok)
            return false;
        page->compare_to = doc->info.pages[static_cast<std::size_t>(n)].id;
    }
    return validate_compare();
}

auto canvas::validate_compare() -> bool
{
    compare_page_.reset();

    const auto page = page_.lock();
    const auto doc = doc_.lock();
    if (!page || !doc || !page->compare_to)
        return false;

    const auto post_err = [this](common::error& err) {
        bus_.post<core::event::error_occurred>(cbuspp::value<common::error&> { err }).sync();
    };

    const auto it = doc->pages.find(*page->compare_to);
    if (it == doc->pages.end()) {
        auto err = common::error::make(common::errc::not_found,
            "compare page not found in document");
        post_err(err);
        return false;
    }
    if (it->second->image.size() != page->image.size()) { // 主副必须同尺寸
        auto err = common::error::make(common::errc::validation_failed,
            "compare page size mismatch: {}x{} vs {}x{}", page->image.width(),
            page->image.height(), it->second->image.width(), it->second->image.height());
        post_err(err);
        return false;
    }
    compare_page_ = it->second;
    return true;
}

void canvas::rethreshold_mask()
{
    const auto page = page_.lock();
    if (!page || !page->mask)
        return;
    if (QImage img = make_threshold_mask(page->image, page->mask->range.first,
            page->mask->range.second);
        !img.isNull()) {
        page->mask->image = std::move(img);
        l3_img_ = { }; // L3 内容变 → 清缓存重建
    }
    update();
}

// ─── 视图约束(旧版同款)────────────────────────────────────────────────────────

auto canvas::display_size() -> QSize
{
    const auto page = page_.lock();
    return page ? oriented_size(*page) : QSize { };
}

auto canvas::half_width() const -> double
{
    if (options_.mode == core::view_mode::split)
        return width() / 2.0;
    return static_cast<double>(width()); // slider 为整视口(缝只切显示侧)
}

auto canvas::zoom_anchor(const QPointF& cursor) const -> QPointF
{
    if (options_.mode != core::view_mode::split)
        return cursor; // 单视口(含 slider):光标即锚点(旧版同款)

    // split 两半共享 offset,单光标无法对称锚定两半 → 各半绕自身中点缩放
    const double seam = seam_x();
    const auto h = static_cast<double>(height());
    return cursor.x() < seam ? QPointF { seam / 2.0, h / 2.0 } // 左半中点
                             : QPointF { seam + (width() - seam) / 2.0, h / 2.0 }; // 右半中点
}

int canvas::seam_x() const
{
    if (options_.mode == core::view_mode::slider)
        return static_cast<int>(width() * view_.split);
    return width() / 2;
}

void canvas::clamp_offset()
{
    const QSize img = display_size();
    if (img.isEmpty())
        return;
    const auto hw = half_width();
    const auto h = static_cast<double>(height());
    const double sw = img.width() * view_.zoom;
    const double sh = img.height() * view_.zoom;

    // 横轴:以 S 所在半区为视口,小于居中、大于则边缘不离开;纵轴恒全高(不变)
    view_.offset.setX(sw <= hw ? (hw - sw) / 2.0 : std::clamp(view_.offset.x(), hw - sw, 0.0));
    view_.offset.setY(sh <= h ? (h - sh) / 2.0 : std::clamp(view_.offset.y(), h - sh, 0.0));
}

void canvas::fit_view()
{
    const QSize img = display_size();
    if (img.isEmpty()) {
        view_ = { };
        return;
    }
    view_.zoom = std::clamp(
        std::min(half_width() / img.width(), static_cast<double>(height()) / img.height()),
        0.1, 10.0);
    view_.offset = { 0.0, 0.0 };
    clamp_offset(); // 居中(适配缩放下即旧版 resetView)
}

// 旧版 QtRenderer::zoom 同款:ratio 锚点数学,缩放生效才 clamp_offset
void canvas::zoom_at(const QPointF& anchor, double delta)
{
    if (display_size().isEmpty())
        return;

    const QPointF local = zoom_anchor(anchor);
    const double old_zoom = view_.zoom;
    const double factor = delta > 0 ? 1.1 : 1.0 / 1.1;
    view_.zoom = std::clamp(view_.zoom * factor, 0.1, 10.0);

    if (view_.zoom != old_zoom) {
        const double ratio = view_.zoom / old_zoom;
        view_.offset = local - (local - view_.offset) * ratio;
        clamp_offset();
    }
}

// ─── 绘制与交互 ───────────────────────────────────────────────────────────────

void canvas::paintEvent(QPaintEvent* event)
{
    QPainter painter { this };
    painter.fillRect(event->rect(), palette().color(QPalette::Window)); // 无文档空白背景

    if (view_dirty_ && width() > 0 && height() > 0) { // 文档就绪后的首次适配
        fit_view();
        view_dirty_ = false;
    }

    const auto page = page_.lock();
    if (!page)
        return;

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // 图像→屏幕:p' = zoom·p + offset。注意 QTransform 乘法为行向量约定:
    // A*B = 先 A 后 B,故 scale 在左、translate 在右(写反会把 offset 也缩放);
    // origin_x = 半区原点(split/slider 右半为缝位置)
    const auto apply_view = [this, &painter](double origin_x = 0.0) {
        painter.setTransform(QTransform::fromScale(view_.zoom, view_.zoom)
            * QTransform::fromTranslate(origin_x + view_.offset.x(), view_.offset.y()));
    };

    // 中缝(屏幕坐标,cosmetic,不随缩放变宽)
    const auto draw_seam = [&painter](int seam, int height) {
        painter.save();
        painter.resetTransform();
        QPen pen { QColor { 128, 128, 128 } };
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawLine(seam, 0, seam, height);
        painter.restore();
    };

    switch (options_.mode) {
    case core::view_mode::single:
        apply_view();
        draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
        draw<layer::l3>(painter, *page, nullptr, options_, l3_img_);
        draw<layer::l4>(painter, *page, nullptr, options_, l4_img_);
        draw<layer::l5>(painter, *page, nullptr, options_, l5_img_);
        draw<layer::l6>(painter, *page, nullptr, options_, l6_img_);
        break;

    case core::view_mode::highlight:
    case core::view_mode::difference: {
        apply_view(); // 不画 L1;L2 灰底 + 差异着色
        const auto compare = compare_page_.lock();
        if (compare)
            draw<layer::l2>(painter, *page, compare.get(), options_, l2_img_);
        draw<layer::l4>(painter, *page, nullptr, options_, l4_img_);
        draw<layer::l5>(painter, *page, nullptr, options_, l5_img_);
        draw<layer::l6>(painter, *page, nullptr, options_, l6_img_);
        break;
    }

    case core::view_mode::split: {
        const auto compare = compare_page_.lock();
        if (!compare) { // 双保险(进模式时已校验):回落 single
            apply_view();
            draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
            break;
        }

        const int seam = seam_x();

        painter.save(); // 左半:S(各半自为独立视口,共享 zoom/offset)
        painter.setClipRect(QRect { 0, 0, seam, height() });
        apply_view();
        draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
        draw<layer::l4>(painter, *page, nullptr, options_, l4_img_);
        draw<layer::l5>(painter, *page, nullptr, options_, l5_img_);
        draw<layer::l6>(painter, *page, nullptr, options_, l6_img_);
        painter.restore();

        painter.save(); // 右半:C(origin = 缝)
        painter.setClipRect(QRect { seam, 0, width() - seam, height() });
        apply_view(static_cast<double>(seam));
        draw<layer::l1>(painter, *compare, nullptr, options_, l1c_img_);
        draw<layer::l4>(painter, *compare, nullptr, options_, l4_img_);
        draw<layer::l5>(painter, *compare, nullptr, options_, l5_img_);
        draw<layer::l6>(painter, *compare, nullptr, options_, l6_img_);
        painter.restore();

        draw_seam(seam, height());
        break;
    }

    case core::view_mode::slider: {
        const auto compare = compare_page_.lock();
        if (!compare) {
            apply_view();
            draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
            break;
        }

        // 单一坐标系(与 single 同布局):同一变换,缝左 clip 画 S、缝右画 C
        const int seam = seam_x();

        painter.save();
        painter.setClipRect(QRect { 0, 0, seam, height() });
        apply_view();
        draw<layer::l1>(painter, *page, nullptr, options_, l1_img_);
        draw<layer::l4>(painter, *page, nullptr, options_, l4_img_);
        draw<layer::l5>(painter, *page, nullptr, options_, l5_img_);
        draw<layer::l6>(painter, *page, nullptr, options_, l6_img_);
        painter.restore();

        painter.save();
        painter.setClipRect(QRect { seam, 0, width() - seam, height() });
        apply_view(); // 无 origin 偏移:与 S 完全同位
        draw<layer::l1>(painter, *compare, nullptr, options_, l1c_img_);
        draw<layer::l4>(painter, *compare, nullptr, options_, l4_img_);
        draw<layer::l5>(painter, *compare, nullptr, options_, l5_img_);
        draw<layer::l6>(painter, *compare, nullptr, options_, l6_img_);
        painter.restore();

        draw_seam(seam, height());
        break;
    }
    }
}

void canvas::wheelEvent(QWheelEvent* event)
{
    zoom_at(event->position(), event->angleDelta().y() > 0 ? 1.0 : -1.0);
    update();
}

void canvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) { // 右键拖动平移
        panning_ = true;
        pan_last_ = event->position();
        event->accept();
    }
}

void canvas::mouseMoveEvent(QMouseEvent* event)
{
    if (!panning_)
        return;
    view_.offset += event->position() - pan_last_;
    pan_last_ = event->position();
    clamp_offset();
    update();
    event->accept();
}

void canvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton && panning_) {
        panning_ = false;
        event->accept();
    }
}

void canvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!view_dirty_)
        clamp_offset(); // 维持"边缘不离开视口/居中"约束
    // slider 悬浮于画布底部居中(非布局子控件,手动定位)
    slider_->setGeometry(event->size().width() / 2 - 150, event->size().height() - 36, 300, 20);
}

}
