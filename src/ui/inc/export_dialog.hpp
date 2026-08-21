#pragma once

// ==============================================================================
// export_dialog.hpp — 高级导出(旧版 ExportDialog 的新架构移植)
//
// 内容:画布截图(当前视图)/ 图像数据(伪彩);页域:当前页 / 全部页(截图
// 恒当前页);格式:TIFF(多页)/ PNG / BMP / SVG / PDF(多页)。伪彩走
// core::colorize(配置当前 colormap 与 zero_is_black,16 位按 hist 量程归一
// 化,与 L1 渲染同口径);彩色页无伪彩语义,原样导出。
// ==============================================================================

#include <QDialog>

#include <cbuspp/cbuspp.hpp>

#include <memory>
#include <vector>

#include "document.hpp"
#include "executor.hpp"

class QButtonGroup;
class QComboBox;
class QRadioButton;

namespace usip::ui {

class export_dialog : public QDialog {
    Q_OBJECT

public:
    // doc 可空(无文档时仅截图可用;数据导出在导出时拦截提示)
    export_dialog(cbuspp::bus<common::executor>& bus, std::shared_ptr<core::document> doc,
        QWidget* canvas, QWidget* parent = nullptr);
    ~export_dialog() override;

    export_dialog(const export_dialog&) = delete;
    export_dialog& operator=(const export_dialog&) = delete;
    export_dialog(export_dialog&&) = delete;
    export_dialog& operator=(export_dialog&&) = delete;

private:
    enum class content_type : int { canvas_screenshot = 0, image_data_pseudocolor = 1 };
    enum class page_scope : int { current_page = 0, all_pages = 1 };
    enum class export_format : int { tiff = 0, png, bmp, svg, pdf };

private:
    void setup_ui();
    void do_export();

    // 页 → 导出图:灰度页按当前配置伪彩化;彩色页原样转 RGBA8888
    [[nodiscard]] static auto page_image(const core::page& page) -> QImage;
    // 文档全部页按页序排列
    [[nodiscard]] auto ordered_pages() const -> std::vector<std::shared_ptr<core::page>>;

    void save_tiff(const QString& path, const std::vector<QImage>& images);
    static void save_raster(const QString& path, const QImage& image, const char* format);
    static void save_svg(const QString& path, const QImage& image);
    static void save_pdf(const QString& path, const std::vector<QImage>& images);

private:
    cbuspp::bus<common::executor>& bus_;
    std::shared_ptr<core::document> doc_ { }; // 空 = 无文档
    QWidget* canvas_ { nullptr }; // 截图抓取对象

    QButtonGroup* content_group_ { nullptr };
    QRadioButton* screenshot_radio_ { nullptr };
    QRadioButton* image_data_radio_ { nullptr };
    QButtonGroup* scope_group_ { nullptr };
    QRadioButton* current_page_radio_ { nullptr };
    QRadioButton* all_pages_radio_ { nullptr };
    QComboBox* format_combo_ { nullptr };
};

}
