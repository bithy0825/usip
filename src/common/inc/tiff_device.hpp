#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace usip::common {

struct tiff_info;

// ─── 特征位:匿名文件的像素指纹(io 层打开时采集)──────────────────────────
// 新增特征 = 加一位常量 + io 层一处 set(),其余零改动

struct device_probe_facts {
    using mask = std::uint32_t;

    static constexpr mask format_uint8 = 1u << 0; // 全部页面为 uint8
    static constexpr mask palette_midpoint_zero = 1u << 1; // pva 特征:colormap 红通道中点为 0
    static constexpr mask rgb_channels_identical = 1u << 2; // casic 特征:RGB 三通道逐像素一致
    static constexpr mask alpha_all_255 = 1u << 3; // casic 特征:第 4 通道恒 255(冗余 alpha)

    mask bits = 0;

    constexpr void set(mask m) noexcept { bits |= m; }
    [[nodiscard]] constexpr auto has(mask m) const noexcept -> bool { return (bits & m) != 0; }
    [[nodiscard]] constexpr auto has_all(mask m) const noexcept -> bool { return (bits & m) == m; }
};

// ─── 身份:文件侧的全部识别线索 + 判定结论 ────────────────────────────────
// io 打开时填线索、detect_device 写结论;日志与文档属性面板直接展示

struct tiff_identity {
    // 文件自报的身份 tag(271 Make / 272 Model / 305 Software / 270 ImageDescription)
    // 只有 make 参与判定;其余是导出软件写的元数据,仅作展示
    std::string make;
    std::string model;
    std::string software;
    std::string description;

    // 隐式线索:像素特征位(仅匿名文件采集;make 已定案时保持空,省采样)
    device_probe_facts facts;

    // 判定结论:命中设备类的 make(空 = unknown);string_view 指向类静态存储,安全
    std::string_view device { };

    enum class source : std::uint8_t {
        none, // 未识别
        tags, // Make tag 精确命中
        signature, // 无 Make 文件,像素特征命中
    };
    source detected_by { source::none };

    // 无 Make 身份(software/description 是导出软件写的,证明不了设备,
    // 不参与判定 —— 只有 make 缺失才算匿名)
    [[nodiscard]] auto anonymous() const noexcept -> bool
    {
        return make.empty();
    }
};

// ─── 文本比对辅助(实现见 tiff_device.cpp)─────────────────────────────────

// ASCII 大小写折叠的子串包含(空 needle 恒 false)
[[nodiscard]] auto contains_ci(std::string_view haystack, std::string_view needle) -> bool;

// ─── 像素规则内核(实现见 tiff_device.cpp;软应用:条件不满足 → 静默跳过)────

// 只保留指定通道(冗余编码收窄);应用后各页 channels=1、klass → gray
auto keep_channel(std::vector<std::byte>& pixels, tiff_info& doc, std::uint16_t channel) -> void;
// pva:调色板 ±128 偏移解码;应用后 klass → gray
auto palette_signed_decode(std::vector<std::byte>& pixels, tiff_info& doc) -> void;

// ─── CRTP 基类:统一判定逻辑 ────────────────────────────────────────────────
// 派生类契约(均为 static 成员,派生类按名遮蔽覆盖):
//   * make        —— 设备唯一身份:厂家+型号(必填)。双重身份:匹配 needle
//                   (文件 Make 含此子串即命中)与结论键(identity.device 指向它)
//   * facts       —— 特征兜底(无 Make 文件须全部命中;须逐位声明适用前提)
//   * reject      —— 特征位,命中任一即排除(缺省 0)
//   * apply_rules —— 命中后的像素规则(缺省无操作)

template <typename Derived>
class tiff_device_base {
public:
    // ── 缺省声明(派生类未覆盖时经名字查找落到这里)──────────────────────
    static constexpr device_probe_facts facts { };
    static constexpr device_probe_facts::mask reject { 0 };

    static void apply_rules(std::vector<std::byte>&, tiff_info&) { }

    // 判定:文件身份 vs 本设备的 make(语义见上方契约注释)
    [[nodiscard]] static auto match(const tiff_identity& id) -> bool
    {
        if (!id.make.empty())
            // 文件自带 Make:只认 make 命中;未命中即终判不匹配
            // (带身份的第三方文件即使像素相似也不认 —— 防误报核心)
            return contains_ci(id.make, Derived::make);

        // 文件无 Make:特征兜底(须全部命中 require,任一命中 reject 即排除)
        return id.facts.has_all(Derived::facts.bits) && !id.facts.has(Derived::reject);
    }
};

// ─── 设备类:一个厂家+型号一个类 ────────────────────────────────────────────

// 德国 PVA(Winsam 扫描声镜):palette ±128 偏移编码
// Make 自报 "PVA Tepla Analytical Systems"(文件不带型号 → 键只含厂家)
class pva final : public tiff_device_base<pva> {
public:
    static constexpr std::string_view make { "PVA" };

    // 无 Make 兜底:palette 编码 + uint8(不含 rgb_ident —— 真样例为 palette
    // 单通道,永远没有 RGB 页,要求 rgb_ident 必然漏判)
    static constexpr device_probe_facts facts {
        .bits = device_probe_facts::palette_midpoint_zero | device_probe_facts::format_uint8,
    };

    static void apply_rules(std::vector<std::byte>& pixels, tiff_info& doc)
    {
        keep_channel(pixels, doc, 0); // 若有冗余通道则收窄(palette 单通道时静默跳过)
        palette_signed_decode(pixels, doc);
    }
};

// 航天测控 AMC99026:RGBA 冗余编码(R=G=B、alpha 恒 255)
// 实测样例零身份 tag → make 是合成键,实际命中走特征兜底
class casic_amc99026 final : public tiff_device_base<casic_amc99026> {
public:
    static constexpr std::string_view make { "casic_amc99026" };

    static constexpr device_probe_facts facts {
        .bits = device_probe_facts::rgb_channels_identical
            | device_probe_facts::alpha_all_255
            | device_probe_facts::format_uint8,
    };

    static void apply_rules(std::vector<std::byte>& pixels, tiff_info& doc)
    {
        keep_channel(pixels, doc, 0); // R=G=B 冗余,只留 R
    }
};

// ─── 注册表:编译期设备列表(依次调用 match,直到命中)───────────────────────
// 新设备 = 新类 + 在此加一项;pva 在前(Make 判定,零像素采样成本)

using tiff_devices = std::tuple<pva, casic_amc99026>;

// 检测:遍历 tiff_devices,结论写入 identity(device = make + 判定来源)
auto detect_device(tiff_identity& identity) -> void;

// 规则应用:按 identity 结论找到设备类,应用其规则(load_tiff 读完全部页后调用)
auto apply_device_rules(std::vector<std::byte>& pixels, tiff_info& doc) -> void;

} // namespace usip::common
