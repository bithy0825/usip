#include "tiff_loader.hpp"

#include <cstring>
#include <fstream>

#include <hwy/highway.h>
#include <tiffio.h>

#include "logger.hpp" // common::log_warn(跳页告警)

namespace usip::io {
namespace {

    namespace hn = hwy::HWY_NAMESPACE;

    // ─── 文件头魔数:II*\0 / MM*\0 = classic,II+\0 / MM+\0(43)= BigTIFF ───────────

    struct file_magic {
        bool is_tiff = false;
        bool big_tiff = false;
    };

    [[nodiscard]] auto read_magic(const std::filesystem::path& path) -> file_magic
    {
        std::ifstream ifs(path, std::ios::binary);
        unsigned char m[4] = { };
        if (!ifs.read(reinterpret_cast<char*>(m), 4))
            return { };
        const bool order_ok = (m[0] == 'I' && m[1] == 'I') || (m[0] == 'M' && m[1] == 'M');
        if (!order_ok)
            return { };
        if (m[2] == 42 && m[3] == 0)
            return { .is_tiff = true, .big_tiff = false };
        if (m[2] == 43 && m[3] == 0)
            return { .is_tiff = true, .big_tiff = true };
        return { };
    }

    // ─── 标签 → 规范化枚举的映射 ─────────────────────────────────────────────────

    [[nodiscard]] auto map_sample_format(std::uint16_t sample_fmt, std::uint16_t bits)
        -> std::optional<common::sample_format>
    {
        using sf = common::sample_format;
        switch (sample_fmt) {
        case SAMPLEFORMAT_UINT:
            switch (bits) {
            case 8:
                return sf::uint8;
            case 16:
                return sf::uint16;
            case 32:
                return sf::uint32;
            case 64:
                return sf::uint64;
            }
            break;
        case SAMPLEFORMAT_INT:
            switch (bits) {
            case 8:
                return sf::int8;
            case 16:
                return sf::int16;
            case 32:
                return sf::int32;
            case 64:
                return sf::int64;
            }
            break;
        case SAMPLEFORMAT_IEEEFP:
            switch (bits) {
            case 32:
                return sf::float32;
            case 64:
                return sf::float64;
            }
            break;
        }
        return std::nullopt; // complex / void / 12bit 等:不支持
    }

    // photometric + 通道数 → 像素类别(容错误差:RGB 带第 4 通道而无 ExtraSamples
    // 声明 → rgba,真样本里的常见非标准写法)
    [[nodiscard]] auto map_pixel_class(std::uint16_t photometric, std::uint16_t spp,
        bool& zero_is_white) -> std::optional<common::pixel_class>
    {
        using pc = common::pixel_class;
        zero_is_white = false;
        switch (photometric) {
        case PHOTOMETRIC_MINISWHITE:
            zero_is_white = true;
            [[fallthrough]];
        case PHOTOMETRIC_MINISBLACK:
            if (spp == 1)
                return pc::gray;
            if (spp == 2)
                return pc::gray_alpha;
            break;
        case PHOTOMETRIC_RGB:
            if (spp == 3)
                return pc::rgb;
            if (spp == 4)
                return pc::rgba;
            break;
        case PHOTOMETRIC_PALETTE:
            if (spp == 1)
                return pc::palette;
            break;
        case PHOTOMETRIC_SEPARATED:
            if (spp == 4)
                return pc::cmyk;
            break;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto map_orientation(std::uint16_t o) -> common::orientation
    {
        using ot = common::orientation;
        switch (o) {
        case ORIENTATION_TOPLEFT:
            return ot::top_left;
        case ORIENTATION_TOPRIGHT:
            return ot::top_right;
        case ORIENTATION_BOTRIGHT:
            return ot::bottom_right;
        case ORIENTATION_BOTLEFT:
            return ot::bottom_left;
        case ORIENTATION_LEFTTOP:
            return ot::left_top;
        case ORIENTATION_RIGHTTOP:
            return ot::right_top;
        case ORIENTATION_RIGHTBOT:
            return ot::right_bottom;
        case ORIENTATION_LEFTBOT:
            return ot::left_bottom;
        }
        return ot::top_left;
    }

    // 分辨率归一化为 dpi;tag 缺失 → nullopt;RESOLUTIONUNIT 无单位/厘米都折算
    [[nodiscard]] auto map_dpi(TIFF* tif, std::uint32_t tag, std::uint16_t resunit)
        -> std::optional<float>
    {
        float v = 0;
        if (!TIFFGetField(tif, tag, &v) || v <= 0)
            return std::nullopt;
        if (resunit == RESUNIT_CENTIMETER)
            v *= 2.54f;
        return v;
    }

    // 采样一页像素(匿名文件的特征采集):R==G==B 逐像素一致;4 通道页附加通道恒 255
    struct page_sample {
        bool sampled = false; // 成功解码至少一条 strip(失败不计数,不否决)
        bool identical = false; // R==G==B
        bool alpha_255 = false; // 第 4 通道恒 255(仅 4 通道页有意义)
    };

    [[nodiscard]] auto sample_rgb_page(TIFF* tif, const common::page_info& page) -> page_sample
    {
        if (page.klass != common::pixel_class::rgb && page.klass != common::pixel_class::rgba)
            return { };
        if (page.format != common::sample_format::uint8)
            return { }; // 目前只见过 8 位;其他位深保守判否

        const auto strip_size = TIFFStripSize(tif);
        if (strip_size <= 0)
            return { };
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(strip_size));
        if (TIFFReadEncodedStrip(tif, 0, strip.data(), strip_size) <= 0)
            return { };

        page_sample out { .sampled = true, .identical = true, .alpha_255 = true };

        const auto channels = page.channels;
        const auto* data = strip.data();
        const std::size_t pixel_count = static_cast<std::size_t>(strip_size) / channels;

        // SIMD 主循环:解交织后比较 R==G 且 R==B;4 通道页同时校验附加通道
        const hn::ScalableTag<std::uint8_t> d;
        const std::size_t n = hn::Lanes(d);
        const auto white = hn::Set(d, std::uint8_t { 255 });

        std::size_t i = 0;
        if (channels == 3) {
            for (; i + n <= pixel_count; i += n) {
                hn::Vec<decltype(d)> c0, c1, c2;
                hn::LoadInterleaved3(d, data + i * 3, c0, c1, c2);
                const auto diff = hn::Or(hn::Xor(c0, c1), hn::Xor(c0, c2));
                if (!hn::AllTrue(d, hn::Eq(diff, hn::Zero(d))))
                    out.identical = false;
            }
        } else {
            for (; i + n <= pixel_count; i += n) {
                hn::Vec<decltype(d)> c0, c1, c2, c3;
                hn::LoadInterleaved4(d, data + i * 4, c0, c1, c2, c3);
                const auto diff = hn::Or(hn::Xor(c0, c1), hn::Xor(c0, c2));
                if (!hn::AllTrue(d, hn::Eq(diff, hn::Zero(d))))
                    out.identical = false;
                if (!hn::AllTrue(d, hn::Eq(c3, white)))
                    out.alpha_255 = false;
            }
        }
        // 标量尾
        for (; i < pixel_count; ++i) {
            const auto* px = data + i * channels;
            if (px[0] != px[1] || px[0] != px[2])
                out.identical = false;
            if (channels == 4 && px[3] != 255)
                out.alpha_255 = false;
        }
        return out;
    }

} // namespace

// ─── tiff_reader ─────────────────────────────────────────────────────────────

// tif_ 是裸句柄:默认移动只复制指针,源对象析构会 TIFFClose 掉唯一句柄,
// 使目标悬垂(读取即崩溃)—— 必须手动转移并把源置空
tiff_reader::tiff_reader(tiff_reader&& other) noexcept
    : tif_ { other.tif_ }
    , info_ { std::move(other.info_) }
    , dir_indices_ { std::move(other.dir_indices_) }
{
    other.tif_ = nullptr;
}

tiff_reader::~tiff_reader()
{
    if (tif_)
        TIFFClose(static_cast<TIFF*>(tif_));
}

auto tiff_reader::open(const std::filesystem::path& path) -> result<tiff_reader>
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return common::fail(common::errc::not_found, "file not found: {}", path.string());

    const auto magic = read_magic(path);
    if (!magic.is_tiff)
        return common::fail(common::errc::parse, "not a TIFF file: {}", path.string());

#ifdef _WIN32
    TIFF* tif = TIFFOpenW(path.c_str(), "r");
#else
    TIFF* tif = TIFFOpen(path.string().c_str(), "r");
#endif
    if (!tif)
        return common::fail(common::errc::parse, "cannot open TIFF: {}", path.string());

    auto reader = tiff_reader { };
    reader.tif_ = tif;
    reader.info_.path = path;
    reader.info_.big_tiff = magic.big_tiff;

    // 身份 tag:首页目录的标准 ASCII 标识(仅 Make 参与设备判定;
    // 其余是导出软件元数据,收作属性面板展示)
    {
        auto& identity = reader.info_.identity;
        char* s = nullptr;
        if (TIFFGetField(tif, TIFFTAG_MAKE, &s) && s)
            identity.make = s;
        s = nullptr;
        if (TIFFGetField(tif, TIFFTAG_MODEL, &s) && s)
            identity.model = s;
        s = nullptr;
        if (TIFFGetField(tif, TIFFTAG_SOFTWARE, &s) && s)
            identity.software = s;
        s = nullptr;
        if (TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &s) && s)
            identity.description = s;
    }

    // 第一轮判定:仅凭 Make(facts 为空)—— 命中即定案,跳过全部像素采样。
    // 带 Make 但未命中的文件为终判 unknown,不再走特征(防误报)。
    common::detect_device(reader.info_.identity);
    const bool need_facts = reader.info_.identity.device.empty()
        && reader.info_.identity.anonymous();

    // 特征采样状态(仅匿名文件使用)
    constexpr std::uint32_t rgb_sample_pages_max = 3; // 多页抽查,单页一致不足为凭
    std::uint32_t rgb_pages_sampled = 0;
    std::uint32_t rgba_pages_sampled = 0;
    bool rgb_all_identical = true;
    bool rgba_all_alpha_255 = true;

    std::uint16_t dir_index = 0;
    do {
        common::page_info page;

        std::uint32_t w = 0, h = 0;
        if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w)
            || !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h) || w == 0 || h == 0) {
            common::log_warn("tiff: page {} has no valid dimensions, skipped", dir_index);
            ++dir_index;
            continue;
        }

        std::uint16_t spp = 1, bps = 8, sample_fmt = SAMPLEFORMAT_UINT;
        std::uint16_t photometric = PHOTOMETRIC_MINISBLACK;
        std::uint16_t planar = PLANARCONFIG_CONTIG;
        std::uint16_t orient = ORIENTATION_TOPLEFT;
        std::uint16_t resunit = RESUNIT_NONE;
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
        TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sample_fmt);
        TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
        TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &photometric);
        TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orient);
        TIFFGetFieldDefaulted(tif, TIFFTAG_RESOLUTIONUNIT, &resunit);

        // 能力检查:不支持的形态静默跳页(多厂家容错),不否决整个文件
        const auto format = map_sample_format(sample_fmt, bps);
        if (!format) {
            common::log_warn("tiff: page {} unsupported sample format (fmt={}, bits={}), skipped",
                dir_index, sample_fmt, bps);
            ++dir_index;
            continue;
        }
        if (planar != PLANARCONFIG_CONTIG) {
            common::log_warn("tiff: page {} is planar-separate, skipped", dir_index);
            ++dir_index;
            continue;
        }
        if (TIFFIsTiled(tif)) {
            common::log_warn("tiff: page {} is tiled (not supported yet), skipped", dir_index);
            ++dir_index;
            continue;
        }

        bool zero_is_white = false;
        const auto klass = map_pixel_class(photometric, spp, zero_is_white);
        if (!klass) {
            common::log_warn("tiff: page {} unsupported photometric/spp ({}/{}), skipped",
                dir_index, photometric, spp);
            ++dir_index;
            continue;
        }

        page.width = w;
        page.height = h;
        page.channels = spp;
        page.format = *format;
        page.klass = *klass;
        page.zero_is_white = zero_is_white;
        page.orient = map_orientation(orient);
        page.dpi_x = map_dpi(tif, TIFFTAG_XRESOLUTION, resunit);
        page.dpi_y = map_dpi(tif, TIFFTAG_YRESOLUTION, resunit);

        // pva 特征:调色板红通道中点为 0(仅匿名文件采集;读取 colormap,零像素解码)
        if (need_facts && *klass == common::pixel_class::palette
            && !reader.info_.identity.facts.has(
                common::device_probe_facts::palette_midpoint_zero)) {
            std::uint16_t *red = nullptr, *green = nullptr, *blue = nullptr;
            if (TIFFGetField(tif, TIFFTAG_COLORMAP, &red, &green, &blue)
                && red && (1u << bps) > 128 && red[128] == 0)
                reader.info_.identity.facts.set(
                    common::device_probe_facts::palette_midpoint_zero);
        }

        // casic 特征:抽查前 N 个 rgb/rgba 页 —— R==G==B 逐像素一致;
        // rgba 页附加通道恒 255(采样须在目录指针位于该页时进行,故在循环内)
        if (need_facts && rgb_pages_sampled < rgb_sample_pages_max
            && (*klass == common::pixel_class::rgb || *klass == common::pixel_class::rgba)) {
            const auto s = sample_rgb_page(tif, page);
            if (s.sampled) {
                rgb_all_identical = rgb_all_identical && s.identical;
                if (*klass == common::pixel_class::rgba) {
                    rgba_all_alpha_255 = rgba_all_alpha_255 && s.alpha_255;
                    ++rgba_pages_sampled;
                }
                ++rgb_pages_sampled;
            }
        }

        // SubIFD 存在性(策略过滤用)
        std::uint16_t n_sub = 0;
        std::uint64_t* sub_offsets = nullptr;
        if (TIFFGetField(tif, TIFFTAG_SUBIFD, &n_sub, &sub_offsets) && n_sub > 0)
            reader.info_.has_sub_ifds = true;

        reader.dir_indices_.push_back(dir_index);
        reader.info_.pages.push_back(page);
        ++dir_index;
    } while (TIFFReadDirectory(tif));

    if (reader.info_.pages.empty())
        return common::fail(common::errc::unsupported,
            "no decodable pages in TIFF: {}", path.string());

    // 第二轮判定:匿名文件,以聚合特征定案(抽样页全过才置位)
    if (need_facts) {
        auto& facts = reader.info_.identity.facts;

        if (rgb_pages_sampled > 0 && rgb_all_identical)
            facts.set(common::device_probe_facts::rgb_channels_identical);
        if (rgba_pages_sampled > 0 && rgba_all_alpha_255)
            facts.set(common::device_probe_facts::alpha_all_255);

        {
            bool all_u8 = true;
            for (const auto& p : reader.info_.pages)
                if (p.format != common::sample_format::uint8) {
                    all_u8 = false;
                    break;
                }
            if (all_u8)
                facts.set(common::device_probe_facts::format_uint8);
        }

        common::detect_device(reader.info_.identity);
    }

    // 判定日志(设备键 + 判定来源,误判可追溯)
    {
        const auto& id = reader.info_.identity;
        const auto* by = id.detected_by == common::tiff_identity::source::tags   ? "tags"
            : id.detected_by == common::tiff_identity::source::signature         ? "signature"
                                                                                  : "none";
        common::log_info("tiff device: {} (by {})",
            id.device.empty() ? std::string_view { "unknown" } : id.device, by);
    }
    return reader;
}

auto tiff_reader::read_page(std::uint32_t index, std::span<std::byte> dst) const -> result<>
{
    if (index >= info_.pages.size())
        return common::fail(common::errc::out_of_range,
            "page index {} out of {} pages", index, info_.pages.size());

    const auto& page = info_.pages[index];
    if (dst.size() < page.byte_size())
        return common::fail(common::errc::invalid_argument,
            "dst too small: {} < {} bytes", dst.size(), page.byte_size());

    auto* tif = static_cast<TIFF*>(tif_);
    if (!TIFFSetDirectory(tif, dir_indices_[index]))
        return common::fail(common::errc::io, "TIFFSetDirectory failed for page {}", index);

    // 条带读取(平铺页已在打开时跳过)
    const auto strip_size = TIFFStripSize(tif);
    const auto n_strips = TIFFNumberOfStrips(tif);
    if (strip_size <= 0 || n_strips == 0)
        return common::fail(common::errc::data_loss, "page {} has no strips", index);

    auto* out = reinterpret_cast<std::uint8_t*>(dst.data());
    for (std::uint32_t s = 0; s < n_strips; ++s) {
        const auto n = TIFFReadEncodedStrip(tif, s,
            out + static_cast<std::size_t>(s) * strip_size, strip_size);
        if (n < 0)
            return common::fail(common::errc::io, "read strip {} of page {} failed", s, index);
    }
    return { };
}

// ─── 策略过滤(硬错误)────────────────────────────────────────────────────────

auto check_policy(const common::tiff_info& info, const tiff_policy& policy) -> result<>
{
    if (info.big_tiff && policy.disable_big_tiff)
        return common::fail(common::errc::unsupported,
            "BigTIFF rejected by load policy");
    if (info.has_sub_ifds && policy.disable_sub_ifds)
        return common::fail(common::errc::unsupported,
            "SubIFDs rejected by load policy");

    for (std::size_t i = 0; i < info.pages.size(); ++i) {
        const auto& p = info.pages[i];
        if (p.width > policy.max_dimension || p.height > policy.max_dimension)
            return common::fail(common::errc::out_of_range,
                "page {} dimensions {}x{} exceed policy limit {}", i, p.width, p.height,
                policy.max_dimension);
        if (static_cast<std::uint64_t>(p.width) * p.height > policy.max_pixels_per_page)
            return common::fail(common::errc::out_of_range,
                "page {} pixel count exceeds policy limit {}", i,
                policy.max_pixels_per_page);
    }
    return { };
}

// ─── 组合操作:open → 策略 → 读全部页 → 按识别设备应用规则 ─────────────────────

auto load_tiff(const std::filesystem::path& path, const tiff_policy& policy)
    -> result<common::loaded_tiff>
{
    auto reader = tiff_reader::open(path);
    if (!reader)
        return std::unexpected(std::move(reader).error());

    if (auto bad = check_policy(reader->info(), policy); !bad)
        return std::unexpected(std::move(bad).error());

    common::loaded_tiff out { .info = reader->info() }; // 拷贝构造(tiff_info 的 const id 仍可拷贝构造)

    std::size_t total = 0;
    for (const auto& p : out.info.pages)
        total += p.byte_size();
    out.pixels.resize(total);

    std::size_t offset = 0;
    for (std::uint32_t i = 0; i < out.info.pages.size(); ++i) {
        const auto size = out.info.pages[i].byte_size();
        if (auto r = reader->read_page(i,
                std::span { out.pixels.data() + offset, size });
            !r)
            return std::unexpected(std::move(r).error());
        offset += size;
    }

    // 设备规则:收窄冗余通道 / 解码偏移编码(软应用;应用后尺寸可能收缩)
    common::apply_device_rules(out.pixels, out.info);
    return out;
}

} // namespace usip::io
