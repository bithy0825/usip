#include "document_service.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include <hwy/highway.h>

#include "event.hpp"
#include "logger.hpp"
#include "tiff.hpp"
#include "tiff_loader.hpp"

namespace usip::service {

namespace {

    namespace hn = hwy::HWY_NAMESPACE; // 静态分派(/arch:AVX2 基线),用法同 common/tiff.cpp

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

    // ─── 直方图统计(仅本文件使用:加载时全部页即算,index_dock 与显示共用)───
    // 值域(min/max)走 SIMD 归约;bin 装箱依赖离散写,Highway 无 scatter,
    // 逐 lane 部分表收益不抵开销,故装箱保持标量。u16 的 bin = 值 >> 8,
    // 值域仍为原始 16 位域(供显示管线 16→8 归一化)。

    void hist_row_u8(const std::uint8_t* row, std::size_t width,
        std::uint64_t* bins, std::uint8_t& lo, std::uint8_t& hi)
    {
        const hn::ScalableTag<std::uint8_t> d;
        const auto n = hn::Lanes(d);
        auto vmin = hn::Set(d, std::uint8_t { 255 });
        auto vmax = hn::Set(d, std::uint8_t { 0 });

        std::size_t x = 0;
        for (; x + n <= width; x += n) {
            const auto v = hn::LoadU(d, row + x);
            vmin = hn::Min(vmin, v);
            vmax = hn::Max(vmax, v);
        }
        if (x > 0) {
            lo = std::min(lo, hn::ReduceMin(d, vmin));
            hi = std::max(hi, hn::ReduceMax(d, vmax));
        }
        for (; x < width; ++x) {
            lo = std::min(lo, row[x]);
            hi = std::max(hi, row[x]);
        }
        for (x = 0; x < width; ++x)
            ++bins[row[x]];
    }

    // 交织多通道(3=RGB / 4=RGBA):LoadInterleaved 解交织后逐通道归约
    template <int C>
    void hist_row_u8_nch(const std::uint8_t* row, std::size_t width,
        std::uint64_t* bins, std::uint8_t* lo, std::uint8_t* hi)
    {
        const hn::ScalableTag<std::uint8_t> d;
        const auto n = hn::Lanes(d);

        hn::Vec<decltype(d)> vmin[C], vmax[C];
        for (int c = 0; c < C; ++c) {
            vmin[c] = hn::Set(d, std::uint8_t { 255 });
            vmax[c] = hn::Set(d, std::uint8_t { 0 });
        }

        std::size_t x = 0;
        for (; x + n <= width; x += n) {
            if constexpr (C == 3) {
                hn::Vec<decltype(d)> v0, v1, v2;
                hn::LoadInterleaved3(d, row + x * 3, v0, v1, v2);
                vmin[0] = hn::Min(vmin[0], v0);
                vmax[0] = hn::Max(vmax[0], v0);
                vmin[1] = hn::Min(vmin[1], v1);
                vmax[1] = hn::Max(vmax[1], v1);
                vmin[2] = hn::Min(vmin[2], v2);
                vmax[2] = hn::Max(vmax[2], v2);
            } else {
                hn::Vec<decltype(d)> v0, v1, v2, v3;
                hn::LoadInterleaved4(d, row + x * 4, v0, v1, v2, v3);
                vmin[0] = hn::Min(vmin[0], v0);
                vmax[0] = hn::Max(vmax[0], v0);
                vmin[1] = hn::Min(vmin[1], v1);
                vmax[1] = hn::Max(vmax[1], v1);
                vmin[2] = hn::Min(vmin[2], v2);
                vmax[2] = hn::Max(vmax[2], v2);
                vmin[3] = hn::Min(vmin[3], v3);
                vmax[3] = hn::Max(vmax[3], v3);
            }
        }
        for (int c = 0; c < C; ++c) {
            if (x > 0) {
                lo[c] = std::min(lo[c], hn::ReduceMin(d, vmin[c]));
                hi[c] = std::max(hi[c], hn::ReduceMax(d, vmax[c]));
            }
            for (std::size_t p = x; p < width; ++p) {
                lo[c] = std::min(lo[c], row[p * C + c]);
                hi[c] = std::max(hi[c], row[p * C + c]);
            }
        }
        for (std::size_t p = 0; p < width; ++p)
            for (int c = 0; c < C; ++c)
                ++bins[static_cast<std::size_t>(c) * common::histogram::bin_count
                    + row[p * C + c]];
    }

    void hist_row_u16(const std::uint16_t* row, std::size_t width,
        std::uint64_t* bins, std::uint16_t& lo, std::uint16_t& hi)
    {
        const hn::ScalableTag<std::uint16_t> d;
        const auto n = hn::Lanes(d);
        auto vmin = hn::Set(d, std::uint16_t { 0xFFFF });
        auto vmax = hn::Set(d, std::uint16_t { 0 });

        std::size_t x = 0;
        for (; x + n <= width; x += n) {
            const auto v = hn::LoadU(d, row + x);
            vmin = hn::Min(vmin, v);
            vmax = hn::Max(vmax, v);
        }
        if (x > 0) {
            lo = std::min(lo, hn::ReduceMin(d, vmin));
            hi = std::max(hi, hn::ReduceMax(d, vmax));
        }
        for (; x < width; ++x) {
            lo = std::min(lo, row[x]);
            hi = std::max(hi, row[x]);
        }
        for (x = 0; x < width; ++x)
            ++bins[row[x] >> 8];
    }

    // 由 QImage 统计直方图(逐行走 constScanLine,容忍行填充);
    // 非原生格式(loader 已过滤,双保险)→ channels == 0 的空 histogram
    [[nodiscard]] auto compute_histogram(const QImage& img) -> common::histogram
    {
        common::histogram hist;
        if (img.isNull()) [[unlikely]]
            return hist;

        const auto w = static_cast<std::size_t>(img.width());
        switch (img.format()) {
        case QImage::Format_Grayscale8: {
            hist.channels = 1;
            hist.bins.assign(common::histogram::bin_count, 0);
            std::uint8_t lo = 255, hi = 0;
            for (int y = 0; y < img.height(); ++y)
                hist_row_u8(img.constScanLine(y), w, hist.bins.data(), lo, hi);
            hist.range_min.push_back(lo);
            hist.range_max.push_back(hi);
            break;
        }
        case QImage::Format_Grayscale16: {
            hist.channels = 1;
            hist.bins.assign(common::histogram::bin_count, 0);
            std::uint16_t lo = 0xFFFF, hi = 0;
            for (int y = 0; y < img.height(); ++y)
                hist_row_u16(reinterpret_cast<const std::uint16_t*>(img.constScanLine(y)),
                    w, hist.bins.data(), lo, hi);
            hist.range_min.push_back(lo);
            hist.range_max.push_back(hi);
            break;
        }
        case QImage::Format_RGB888:
        case QImage::Format_RGBA8888: {
            const bool rgb = img.format() == QImage::Format_RGB888;
            hist.channels = rgb ? 3 : 4;
            hist.bins.assign(
                static_cast<std::size_t>(hist.channels) * common::histogram::bin_count, 0);
            std::array<std::uint8_t, 4> lo { 255, 255, 255, 255 };
            std::array<std::uint8_t, 4> hi { 0, 0, 0, 0 };
            for (int y = 0; y < img.height(); ++y) {
                const auto* row = img.constScanLine(y);
                if (rgb)
                    hist_row_u8_nch<3>(row, w, hist.bins.data(), lo.data(), hi.data());
                else
                    hist_row_u8_nch<4>(row, w, hist.bins.data(), lo.data(), hi.data());
            }
            for (std::uint16_t c = 0; c < hist.channels; ++c) {
                hist.range_min.push_back(lo[c]);
                hist.range_max.push_back(hi[c]);
            }
            break;
        }
        default:
            return { };
        }
        return hist;
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
    bus_.on<core::event::document_switch_requested>()
        .call(*this, &document_service::on_document_switch_requested);
    bus_.on<core::event::page_switch_requested>()
        .call(*this, &document_service::on_page_switch_requested);
}

void document_service::on_document_switch_requested(const cbuspp::value<cuuidpp::uuid>& value)
{
    const auto it = docs_.find(*value);
    if (it == docs_.end()) [[unlikely]] {
        common::log_warn("switch requested for unknown document: '{}'", value->to_string());
        return;
    }

    // 非拥有别名:实体由 docs_ 持有
    std::shared_ptr<core::document> doc_alias { &it->second, [](core::document*) { } };
    bus_.post<core::event::document_switch>(
            cbuspp::value<std::shared_ptr<core::document>> { doc_alias })
        .with_trace_id(core::event::trace_id::document_service)
        .sync();
}

void document_service::on_page_switch_requested(const cbuspp::value<cuuidpp::uuid>& value)
{
    const auto pg_it = pages_.find(*value);
    if (pg_it == pages_.end()) [[unlikely]] {
        common::log_warn("page switch requested for unknown page: '{}'", value->to_string());
        return;
    }

    const auto doc_it = docs_.find(pg_it->second.doc_id);
    if (doc_it == docs_.end()) [[unlikely]] {
        common::log_warn("page switch requested for orphan page: '{}'", value->to_string());
        return;
    }

    doc_it->second.active_page = *value;

    // 复用 document_switch:画布按新 active_page 重解析、清层缓存并重新适配;
    // index_dock 同步行选择(信号阻断,不回发请求)
    std::shared_ptr<core::document> doc_alias { &doc_it->second, [](core::document*) { } };
    bus_.post<core::event::document_switch>(
            cbuspp::value<std::shared_ptr<core::document>> { doc_alias })
        .with_trace_id(core::event::trace_id::document_service)
        .sync();
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
    if (!loaded) [[unlikely]] {
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
        if (!format) [[unlikely]] {
            auto err = common::error::make(common::errc::unsupported,
                "page {} has no native QImage format (klass {}, format {})", i,
                std::to_underlying(pinfo.klass), std::to_underlying(pinfo.format));
            common::log_error("failed to wrap '{}': {}", path.string(), err);
            post_error(err);
            return;
        }

        const auto slice_end = offset + pinfo.byte_size();
        if (slice_end > pixels->size()) [[unlikely]] { // io 不变式:拼接缓冲恰好覆盖全部页
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

        // 直方图:全部页加载时即算(index_dock 页统计与 16 位显示归一化共用)
        if (auto hist = compute_histogram(pg.image); hist.channels != 0) [[likely]]
            pg.info.hist = std::move(hist);

        auto& m = pg.mask.emplace();
        m.image = QImage { static_cast<int>(pinfo.width),
            static_cast<int>(pinfo.height), QImage::Format_Grayscale8 };
        m.image.fill(255);
        m.range = { 0.0, 255.0 };

        offset = slice_end;
    }

    // 全部页就绪:入册(多文档共存,只增不替);别名指向入册后的页实体(地址稳定)
    for (auto& pg : pages) {
        const auto page_id = pg.info.id;
        auto& slot = pages_.emplace(page_id, std::move(pg)).first->second;
        doc.pages.emplace(page_id, std::shared_ptr<core::page> { &slot, [](core::page*) { } });
    }

    if (!doc.info.pages.empty()) [[likely]]
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
