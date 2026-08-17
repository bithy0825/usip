#include "document_service.hpp"

#include "event.hpp"
#include "logger.hpp"
#include "tiff.hpp"
#include "tiff_loader.hpp"

namespace usip::service {

namespace {

    // pixel_class + sample_format → QImage 原生格式(零拷贝视图的前提)。
    // 其余组合(gray_alpha/palette/cmyk、32/64 位整型与浮点)无原生格式:
    // 归一化与通道换算属显示管线职责,此处判 unsupported
    [[nodiscard]] constexpr auto qimage_format(common::pixel_class klass,
        common::sample_format format) -> std::optional<QImage::Format>
    {
        using enum common::pixel_class;
        using enum common::sample_format;
        switch (klass) {
        case gray:
            if (format == uint8)
                return QImage::Format_Grayscale8;
            if (format == uint16)
                return QImage::Format_Grayscale16;
            break;
        case rgb:
            if (format == uint8)
                return QImage::Format_RGB888;
            break;
        case rgba:
            if (format == uint8)
                return QImage::Format_RGBA8888;
            break;
        case gray_alpha:
        case palette:
        case cmyk:
            break;
        }
        return std::nullopt;
    }

} // namespace

document_service::document_service(common::executor& executor,
    cbuspp::bus<common::executor>& bus)
    : bus_ { bus }
    , executor_ { executor }
{
    setup_subscriptions();
}

document_service::~document_service() = default;

void document_service::setup_subscriptions()
{
    bus_.on<core::event::file_selected>().call(*this, &document_service::on_file_selected);
}

void document_service::on_file_selected(const cbuspp::value<std::filesystem::path>& value)
{
    const auto& path = *value;

    // 同一文件不重复打开:比对已打开文档的 tiff_info 路径(词法比较,
    // 文件对话框恒返绝对路径);命中即警告并跳过本次加载
    if (std::ranges::any_of(docs_ | std::views::values,
            [&path](const core::document& doc) { return doc.info.path == path; })) {
        common::log_warn("document is already open: '{}'", path.string());
        bus_.post<core::event::document_switch>(cbuspp::value<std::shared_ptr<core::document>> { })
            .with_trace_id(core::event::trace_id::document_service)
            .sync();
        return;
    }

    const auto post_error = [this](common::error& err) {
        bus_.post<core::event::error_occurred>(cbuspp::value<common::error&> { err })
            .with_trace_id(core::event::trace_id::document_service)
            .sync();
    };

    auto loaded = io::load_tiff(path);
    if (!loaded) {
        auto err = loaded.error();
        common::log_error("failed to load '{}': {}", path.string(), err);
        post_error(err);
        return;
    }

    // 像素缓冲共享持有:各页 QImage 经 cleanup 回调各持一份引用(零拷贝),
    // 全部页释放后缓冲统一回收
    auto pixels = std::make_shared<std::vector<std::byte>>(std::move(loaded->pixels));

    core::document doc;
    doc.info = std::move(loaded->info);

    // 页先全部创建在本地:任何一页失败,局部对象析构即回滚,已打开的文档不受影响
    std::vector<core::page> pages;
    pages.reserve(doc.info.pages.size());

    std::size_t offset = 0;
    for (const auto& [i, pinfo] : doc.info.pages | std::views::enumerate) {
        const auto format = qimage_format(pinfo.klass, pinfo.format);
        if (!format) {
            auto err = common::error::make(common::errc::unsupported,
                "page {} has no native QImage format (klass {}, format {})", i,
                std::to_underlying(pinfo.klass), std::to_underlying(pinfo.format));
            common::log_error("failed to wrap '{}': {}", path.string(), err);
            post_error(err);
            return;
        }

        const auto slice_end = offset + pinfo.byte_size();
        if (slice_end > pixels->size()) { // io 不变式:拼接缓冲恰好覆盖全部页
            auto err = common::error::make(common::errc::internal,
                "page {} pixel slice overflows buffer ({} > {})", i, slice_end,
                pixels->size());
            common::log_error("failed to wrap '{}': {}", path.string(), err);
            post_error(err);
            return;
        }

        auto& pg = pages.emplace_back();
        pg.info = pinfo;
        pg.doc_id = doc.info.id;
        pg.index = static_cast<std::uint32_t>(i);

        // 行紧密排列(PLANARCONFIG_CONTIG):stride = 宽 × 通道 × 样本字节
        const auto stride = static_cast<int>(pinfo.width * pinfo.channels
            * common::sample_format_size(pinfo.format));
        pg.image = QImage {
            reinterpret_cast<const uchar*>(pixels->data() + offset),
            static_cast<int>(pinfo.width), static_cast<int>(pinfo.height),
            stride, *format,
            [](void* p) { delete static_cast<std::shared_ptr<std::vector<std::byte>>*>(p); },
            new std::shared_ptr<std::vector<std::byte>> { pixels }
        };

        offset = slice_end;
    }

    // 全部页就绪:入册(多文档共存,只增不替);别名指向入册后的页实体(地址稳定)
    for (auto& pg : pages) {
        const auto page_id = pg.info.id;
        auto& slot = pages_.emplace(page_id, std::move(pg)).first->second;
        doc.pages.emplace(page_id, std::shared_ptr<core::page> { &slot, [](core::page*) { } });
    }

    if (!doc.info.pages.empty())
        doc.active_page = doc.info.pages.front().id; // 默认当前页 = index 0

    const auto doc_id = doc.info.id;
    auto& stored = docs_.emplace(doc_id, std::move(doc)).first->second;

    common::log_info("document ready: '{}' ({} pages)", path.string(),
        stored.info.pages.size());

    // 非拥有别名:实体由 docs_ 持有
    std::shared_ptr<core::document> doc_alias { &stored, [](core::document*) { } };
    bus_.post<core::event::document_ready>(
            cbuspp::value<std::shared_ptr<core::document>> { doc_alias })
        .with_trace_id(core::event::trace_id::document_service)
        .sync();
}

}
