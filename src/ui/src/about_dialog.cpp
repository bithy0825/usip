#include "about_dialog.hpp"
#include "icon_registry.hpp"

#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSysInfo>
#include <QVBoxLayout>

#include <usip/version.hpp>

namespace usip::ui {
namespace {

    // 编译器身份(构建期宏 → 可读串)
    [[nodiscard]] auto compiler_info() -> QString
    {
#if defined(_MSC_VER)
        QString vs;
        if constexpr (_MSC_VER >= 1950)
            vs = QStringLiteral("Visual Studio 2026");
        else if constexpr (_MSC_VER >= 1940)
            vs = QStringLiteral("Visual Studio 2022");
        else if constexpr (_MSC_VER >= 1930)
            vs = QStringLiteral("Visual Studio 2022");
        else if constexpr (_MSC_VER >= 1920)
            vs = QStringLiteral("Visual Studio 2019");
        else
            vs = QStringLiteral("Visual Studio");
        return QStringLiteral("MSVC %1.%2 (%3)")
            .arg(_MSC_VER / 100)
            .arg(_MSC_VER % 100)
            .arg(vs);
#elif defined(__clang__)
        return QStringLiteral("Clang %1.%2.%3")
            .arg(__clang_major__)
            .arg(__clang_minor__)
            .arg(__clang_patchlevel__);
#elif defined(__GNUC__)
        return QStringLiteral("GCC %1.%2.%3")
            .arg(__GNUC__)
            .arg(__GNUC_MINOR__)
            .arg(__GNUC_PATCHLEVEL__);
#else
        return QStringLiteral("Unknown");
#endif
    }

    // 详情栅格的一行(键灰阶小字,值可选中复制)
    void add_detail(QGridLayout* grid, int row, const QString& key, const QString& value)
    {
        auto* k = new QLabel(key);
        auto* v = new QLabel(value);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(k, row, 0, Qt::AlignTop);
        grid->addWidget(v, row, 1, Qt::AlignTop);
    }

} // namespace

about_dialog::about_dialog(QWidget* parent)
    : QDialog(parent)
{
    setup_ui();
}

about_dialog::~about_dialog() = default;

void about_dialog::setup_ui()
{
    setWindowTitle(tr("About USIP"));
    setModal(true);
    setFixedSize(460, 380);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(12);

    // ── 头部:应用图标 + 名称/标语/版本 ─────────────────────────────────
    auto* header = new QHBoxLayout;
    header->setSpacing(16);

    auto* icon_label = new QLabel(this);
    const auto app_icon = icon_registry::instance().icon("app");
    if (app_icon)
        icon_label->setPixmap(app_icon->pixmap(64, 64));
    header->addWidget(icon_label, 0, Qt::AlignTop);

    auto* identity = new QVBoxLayout;
    identity->setSpacing(2);
    auto* title = new QLabel(tr("<span style='font-size:20pt; font-weight:600;'>USIP</span>"), this);
    auto* tagline = new QLabel(tr("Ultrasound Image Processor"), this);
    auto* version = new QLabel(tr("Version %1 (%2)")
                                   .arg(QLatin1String(kVersion), QLatin1String(kGitHash)),
        this);
    QPalette dim = tagline->palette();
    dim.setColor(QPalette::WindowText, dim.color(QPalette::PlaceholderText));
    tagline->setPalette(dim);
    version->setPalette(dim);
    identity->addWidget(title);
    identity->addWidget(tagline);
    identity->addWidget(version);
    identity->addStretch();
    header->addLayout(identity, 1);
    root->addLayout(header);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    root->addWidget(line);

    // ── 简介 ────────────────────────────────────────────────────────────
    auto* description = new QLabel(
        tr("A desktop application for multi-page ultrasound TIFF inspection and analysis: "
           "view comparison, threshold segmentation, ROI statistics and measurement "
           "annotation."),
        this);
    description->setWordWrap(true);
    root->addWidget(description);

    // ── 运行环境(值可复制)──────────────────────────────────────────────
    auto* grid = new QGridLayout;
    grid->setColumnMinimumWidth(0, 88);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    add_detail(grid, 0, tr("Qt"), QStringLiteral("%1 (runtime %2)").arg(QT_VERSION_STR, qVersion()));
    add_detail(grid, 1, tr("Compiler"), compiler_info());
    add_detail(grid, 2, tr("System"), QSysInfo::prettyProductName());
    add_detail(grid, 3, tr("Architecture"), QSysInfo::currentCpuArchitecture());
    root->addLayout(grid);

    root->addStretch();

    // ── 版权与按钮 ──────────────────────────────────────────────────────
    auto* copyright = new QLabel(tr("Copyright © 2024-2026 USIP. All rights reserved."), this);
    copyright->setPalette(dim);
    root->addWidget(copyright);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

}
