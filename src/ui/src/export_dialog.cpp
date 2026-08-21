#include "export_dialog.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QRadioButton>
#include <QSvgGenerator>
#include <QVBoxLayout>

#include <tiffio.h>

#include <algorithm>
#include <format>
#include <ranges>
#include <string>

#include "colormap.hpp"
#include "config.hpp"
#include "event.hpp"
#include "utility.hpp"

namespace usip::ui {

export_dialog::export_dialog(cbuspp::bus<common::executor>& bus,
    std::shared_ptr<core::document> doc, QWidget* canvas, QWidget* parent)
    : QDialog(parent)
    , bus_ { bus }
    , doc_ { std::move(doc) }
    , canvas_ { canvas }
{
    setWindowTitle(tr("Export"));
    setMinimumWidth(420);
    setModal(true);
    setup_ui();
}

export_dialog::~export_dialog() = default;

void export_dialog::setup_ui()
{
    auto* root = new QVBoxLayout(this);

    // ── 内容 ────────────────────────────────────────────────────────────
    auto* content_box = new QGroupBox(tr("Content"), this);
    auto* content_layout = new QVBoxLayout(content_box);
    content_group_ = new QButtonGroup(this);
    screenshot_radio_ = new QRadioButton(tr("Canvas Screenshot (Current View)"), content_box);
    image_data_radio_ = new QRadioButton(tr("Image Data (Pseudocolor)"), content_box);
    content_group_->addButton(screenshot_radio_, static_cast<int>(content_type::canvas_screenshot));
    content_group_->addButton(image_data_radio_, static_cast<int>(content_type::image_data_pseudocolor));
    image_data_radio_->setChecked(true);
    content_layout->addWidget(screenshot_radio_);
    content_layout->addWidget(image_data_radio_);
    root->addWidget(content_box);

    // ── 页域 ────────────────────────────────────────────────────────────
    auto* scope_box = new QGroupBox(tr("Pages"), this);
    auto* scope_layout = new QVBoxLayout(scope_box);
    scope_group_ = new QButtonGroup(this);
    current_page_radio_ = new QRadioButton(tr("Current Page"), scope_box);
    all_pages_radio_ = new QRadioButton(tr("All Pages"), scope_box);
    scope_group_->addButton(current_page_radio_, static_cast<int>(page_scope::current_page));
    scope_group_->addButton(all_pages_radio_, static_cast<int>(page_scope::all_pages));
    current_page_radio_->setChecked(true);
    scope_layout->addWidget(current_page_radio_);
    scope_layout->addWidget(all_pages_radio_);
    root->addWidget(scope_box);

    // ── 格式 ────────────────────────────────────────────────────────────
    auto* format_box = new QGroupBox(tr("Format"), this);
    auto* format_layout = new QHBoxLayout(format_box);
    format_combo_ = new QComboBox(format_box);
    format_combo_->addItem(QStringLiteral("TIFF"), static_cast<int>(export_format::tiff));
    format_combo_->addItem(QStringLiteral("PNG"), static_cast<int>(export_format::png));
    format_combo_->addItem(QStringLiteral("BMP"), static_cast<int>(export_format::bmp));
    format_combo_->addItem(QStringLiteral("SVG"), static_cast<int>(export_format::svg));
    format_combo_->addItem(QStringLiteral("PDF"), static_cast<int>(export_format::pdf));
    format_layout->addWidget(format_combo_, 1);
    root->addWidget(format_box);

    // ── 按钮 ────────────────────────────────────────────────────────────
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* export_button = new QPushButton(tr("Export"), this);
    export_button->setDefault(true);
    auto* cancel_button = new QPushButton(tr("Cancel"), this);
    buttons->addWidget(export_button);
    buttons->addWidget(cancel_button);
    root->addLayout(buttons);

    connect(export_button, &QPushButton::clicked, this, &export_dialog::do_export);
    connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);
    // 截图只取当前视图:页域恒为当前页
    connect(content_group_, &QButtonGroup::idClicked, this, [this](int id) {
        const bool screenshot = id == static_cast<int>(content_type::canvas_screenshot);
        all_pages_radio_->setEnabled(!screenshot);
        if (screenshot)
            current_page_radio_->setChecked(true);
    });
}

void export_dialog::do_export()
{
    const auto format = static_cast<export_format>(format_combo_->currentData().toInt());
    const auto scope = static_cast<page_scope>(scope_group_->checkedId());
    const auto content = static_cast<content_type>(content_group_->checkedId());
    const bool screenshot = content == content_type::canvas_screenshot;

    if (!screenshot && !doc_) { // 数据导出须先开文档
        QMessageBox::warning(this, tr("Export"), tr("No document is open."));
        return;
    }

    // 格式 → 后缀/过滤器
    const auto ext = [&format] {
        switch (format) {
        case export_format::tiff:
            return QStringLiteral(".tiff");
        case export_format::png:
            return QStringLiteral(".png");
        case export_format::bmp:
            return QStringLiteral(".bmp");
        case export_format::svg:
            return QStringLiteral(".svg");
        case export_format::pdf:
            return QStringLiteral(".pdf");
        }
        return QStringLiteral(".png");
    }();
    const auto filter = ext.isEmpty() ? QString() : tr("%1 Files (*%2)").arg(ext.mid(1).toUpper(), ext);

    const QString stem = doc_
        ? QString::fromStdString(common::path_to_utf8(doc_->info.path.stem()))
        : QStringLiteral("usip");
    const QString default_name = stem + (screenshot ? QStringLiteral("_screenshot") : QStringLiteral("_export")) + ext;

    QString path = QFileDialog::getSaveFileName(this, tr("Export"), default_name, filter);
    if (path.isEmpty())
        return;
    if (!path.endsWith(ext, Qt::CaseInsensitive))
        path += ext;

    // 图像集合:截图 = 当前视图抓取;数据 = 当前页或全部页伪彩
    std::vector<QImage> images;
    if (screenshot) {
        images.push_back(canvas_->grab().toImage());
    } else if (scope == page_scope::current_page) {
        if (const auto it = doc_->pages.find(doc_->active_page); it != doc_->pages.end())
            images.push_back(page_image(*it->second));
    } else {
        for (const auto& page : ordered_pages())
            images.push_back(page_image(*page));
    }
    if (images.empty() || std::ranges::all_of(images, &QImage::isNull)) {
        QMessageBox::warning(this, tr("Export"), tr("Nothing to export."));
        return;
    }

    // 落盘:TIFF/PDF 原生多页;其余格式多页拆分为 _1/_2… 单文件
    const bool multipage = format == export_format::tiff || format == export_format::pdf;
    if (multipage || images.size() == 1) {
        switch (format) {
        case export_format::tiff:
            save_tiff(path, images);
            break;
        case export_format::pdf:
            save_pdf(path, images);
            break;
        case export_format::png:
            save_raster(path, images.front(), "PNG");
            break;
        case export_format::bmp:
            save_raster(path, images.front(), "BMP");
            break;
        case export_format::svg:
            save_svg(path, images.front());
            break;
        }
    } else {
        const QString base = path.left(path.size() - ext.size());
        for (const auto& [i, image] : images | std::views::enumerate) {
            const QString each = QStringLiteral("%1_%2%3").arg(base).arg(i + 1).arg(ext);
            switch (format) {
            case export_format::png:
                save_raster(each, image, "PNG");
                break;
            case export_format::bmp:
                save_raster(each, image, "BMP");
                break;
            case export_format::svg:
                save_svg(each, image);
                break;
            default:
                break;
            }
        }
    }

    bus_.post<core::event::status_message>(
            cbuspp::value<std::string> {
                std::format("Exported: {}", path.toUtf8().toStdString()) })
        .sync();
    accept();
}

auto export_dialog::page_image(const core::page& page) -> QImage
{
    if (core::pseudocolorable(page.image.format())) {
        std::optional<std::pair<double, double>> range; // 16 位按 hist 量程(同 L1)
        if (const auto& hist = page.info.hist; hist && !hist->range_min.empty())
            range = std::pair { hist->range_min.front(), hist->range_max.front() };
        const auto* cfg = core::config::global();
        const auto lut = core::make_color_lut(
            core::colormap_from_string(cfg->get<std::string>("pseudocolor.colormap"))
                .value_or(core::colormap_type::jet),
            cfg->get<bool>("pseudocolor.zero_is_black"));
        if (QImage colored = core::colorize(page.image, lut, range); !colored.isNull())
            return colored;
    }
    return page.image.convertToFormat(QImage::Format_RGBA8888); // 彩色页:原样
}

auto export_dialog::ordered_pages() const -> std::vector<std::shared_ptr<core::page>>
{
    std::vector<std::shared_ptr<core::page>> pages;
    pages.reserve(doc_->pages.size());
    for (const auto& page : doc_->pages | std::views::values)
        pages.push_back(page);
    std::ranges::sort(pages, { }, &core::page::index);
    return pages;
}

void export_dialog::save_tiff(const QString& path, const std::vector<QImage>& images)
{
    TIFF* tif = TIFFOpenW(path.toStdWString().c_str(), "w");
    if (!tif) {
        QMessageBox::warning(this, tr("Export"), tr("Failed to create TIFF file."));
        return;
    }

    for (const auto& [i, image] : images | std::views::enumerate) {
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);

        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, rgba.width());
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH, rgba.height());
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, rgba.width() * 4));
        TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE); // 多页
        TIFFSetField(tif, TIFFTAG_PAGENUMBER, static_cast<std::uint16_t>(i),
            static_cast<std::uint16_t>(images.size()));

        for (int row = 0; row < rgba.height(); ++row)
            if (TIFFWriteScanline(tif, const_cast<uchar*>(rgba.constScanLine(row)), row, 0) < 0)
                break; // 单行失败:该页截断,继续后续页

        if (static_cast<std::size_t>(i) + 1 < images.size())
            TIFFWriteDirectory(tif);
    }
    TIFFClose(tif);
}

void export_dialog::save_raster(const QString& path, const QImage& image, const char* format)
{
    if (!image.save(path, format))
        QMessageBox::warning(nullptr, tr("Export"), tr("Failed to save: %1").arg(path));
}

void export_dialog::save_svg(const QString& path, const QImage& image)
{
    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(image.size());
    generator.setViewBox(QRect { 0, 0, image.width(), image.height() });
    generator.setTitle(tr("Exported Image"));
    generator.setDescription(tr("Exported from USIP"));

    QPainter painter { &generator };
    painter.drawImage(0, 0, image);
}

void export_dialog::save_pdf(const QString& path, const std::vector<QImage>& images)
{
    if (images.empty())
        return;

    QPdfWriter writer { path };
    writer.setPageSize(QPageSize { QPageSize::A4 });
    writer.setResolution(300);

    QPainter painter { &writer };
    for (const auto& [i, image] : images | std::views::enumerate) {
        if (i > 0)
            writer.newPage();
        const QRect page_rect = painter.viewport();
        QSize fitted = image.size();
        fitted.scale(page_rect.size(), Qt::KeepAspectRatio);
        painter.drawImage(QRect { (page_rect.width() - fitted.width()) / 2,
                              (page_rect.height() - fitted.height()) / 2, fitted.width(),
                              fitted.height() },
            image);
    }
}

}
