#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace cuuidpp {

struct alignas(8) uuid {
    enum class parse_error : uint8_t {
        invalid_length, // 长度不是 36
        invalid_hyphen, // 连字符位置错误
        invalid_hex_digit // 非法 hex 字符
    };

    using value_type = uint8_t;

    std::array<uint8_t, 16> bytes { };

    // ─── 容器式访问 ──────────────────────────────────────────────────────

    [[nodiscard]] constexpr auto operator[](size_t i) noexcept -> uint8_t&
    {
        return bytes[i];
    }
    [[nodiscard]] constexpr auto operator[](size_t i) const noexcept -> const uint8_t&
    {
        return bytes[i];
    }

    [[nodiscard]] explicit constexpr operator std::span<const uint8_t, 16>() const noexcept
    {
        return std::span<const uint8_t, 16>(bytes.data(), 16);
    }
    [[nodiscard]] explicit constexpr operator std::span<uint8_t, 16>() noexcept
    {
        return std::span<uint8_t, 16>(bytes.data(), 16);
    }

    [[nodiscard]] constexpr auto begin() noexcept { return bytes.begin(); }
    [[nodiscard]] constexpr auto end() noexcept { return bytes.end(); }
    [[nodiscard]] constexpr auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return bytes.end(); }

    [[nodiscard]] constexpr auto data() noexcept -> uint8_t* { return bytes.data(); }
    [[nodiscard]] constexpr auto data() const noexcept -> const uint8_t*
    {
        return bytes.data();
    }
    [[nodiscard]] constexpr auto size() const noexcept -> size_t { return 16; }

    [[nodiscard]] constexpr auto operator==(const uuid&) const noexcept -> bool = default;
    [[nodiscard]] constexpr auto operator<=>(const uuid&) const noexcept
        -> std::strong_ordering = default;

    // ─── RFC 9562 特殊值(§5.9 nil / §5.10 max) ───────────────────────────

    [[nodiscard]] static constexpr auto nil() noexcept -> uuid { return { }; }

    [[nodiscard]] static constexpr auto max() noexcept -> uuid
    {
        uuid id;
        id.bytes.fill(0xFF);
        return id;
    }

    [[nodiscard]] constexpr auto is_nil() const noexcept -> bool { return *this == nil(); }

    // ─── 字段解读(RFC 9562 §4.1 / §4.2) ─────────────────────────────────

    [[nodiscard]] constexpr auto version() const noexcept -> uint8_t
    {
        return bytes[6] >> 4;
    }

    // 0 = NCS, 1 = RFC 9562, 2 = Microsoft, 3 = future
    [[nodiscard]] constexpr auto variant() const noexcept -> uint8_t
    {
        const uint8_t v = bytes[8];
        if ((v & 0x80) == 0x00)
            return 0;
        if ((v & 0xC0) == 0x80)
            return 1;
        if ((v & 0xE0) == 0xC0)
            return 2;
        return 3;
    }

    // v7 内嵌的 48 位 Unix 毫秒时间戳;非 v7 返回 nullopt
    [[nodiscard]] constexpr auto timestamp_ms() const noexcept -> std::optional<uint64_t>
    {
        if (version() != 7)
            return std::nullopt;

        uint64_t ts = 0;
        for (int i = 0; i < 6; ++i)
            ts = (ts << 8) | bytes[i];
        return ts;
    }

    // ─── 文本形式:8-4-4-4-12 小写 hex ───────────────────────────────────

    // 向 out 写入 36 个字符(无终止符),返回末尾后一位。
    // to_string / to_array / std::formatter 共用的零开销内核:查表直写,
    // 不经 snprintf,也不做 16 次 format_to。
    constexpr auto write_chars(char* out) const noexcept -> char*
    {
        constexpr char hex[] = "0123456789abcdef";
        int o = 0;
        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                out[o++] = '-';
            out[o++] = hex[bytes[i] >> 4];
            out[o++] = hex[bytes[i] & 0x0F];
        }
        return out + o;
    }

    [[nodiscard]] constexpr auto to_array() const noexcept -> std::array<char, 36>
    {
        std::array<char, 36> buf { };
        write_chars(buf.data());
        return buf;
    }

    [[nodiscard]] auto to_string() const -> std::string
    {
        std::string s(36, '\0');
        write_chars(s.data());
        return s;
    }

    // 严格解析 8-4-4-4-12;输入大小写不敏感(RFC 9562 §4),失败给出具体原因
    [[nodiscard]] static constexpr auto from_string(std::string_view sv) noexcept
        -> std::expected<uuid, parse_error>
    {
        if (sv.size() != 36)
            return std::unexpected(parse_error::invalid_length);

        const auto nibble = [](char c) noexcept -> uint8_t {
            if (c >= '0' && c <= '9')
                return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F')
                return static_cast<uint8_t>(c - 'A' + 10);
            return uint8_t { 0xFF };
        };

        uuid id;
        int nib = 0;
        for (size_t i = 0; i < 36; ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (sv[i] != '-')
                    return std::unexpected(parse_error::invalid_hyphen);
                continue;
            }
            const uint8_t n = nibble(sv[i]);
            if (n == 0xFF)
                return std::unexpected(parse_error::invalid_hex_digit);

            id.bytes[nib / 2] = static_cast<uint8_t>((id.bytes[nib / 2] << 4) | n);
            ++nib;
        }
        return id;
    }
};

// ─── 生成器(实现见 uuid.cpp)─────────────────────────────────────────────
//
// v7:时间有序 —— 48 位 Unix 毫秒 + 同毫秒递增计数器 + 62 位随机,
//     字典序即生成顺序(每线程严格单调),作为默认 id 策略。
// v4:纯随机 122 位 —— 不读时钟,速度最快,不含任何时间信息。
// 两者均基于 thread_local PRNG:零锁、零原子、零系统调用(仅每线程首次播种)。

[[nodiscard]] auto generate_uuid_v7() -> uuid;
[[nodiscard]] auto generate_uuid_v4() -> uuid;

// 默认生成策略:v7
[[nodiscard]] inline auto generate_uuid() -> uuid { return generate_uuid_v7(); }

} // namespace uuid

// ─── std::hash:murmur fmix64 双字混合 ────────────────────────────────────

template <>
struct std::hash<cuuidpp::uuid> {
    constexpr auto operator()(const cuuidpp::uuid& id) const noexcept -> size_t
    {
        const auto [low, high] = std::bit_cast<std::array<uint64_t, 2>>(id.bytes);

        constexpr auto fmix64 = [](uint64_t x) noexcept -> uint64_t {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdULL;
            x ^= x >> 33;
            x *= 0xc4ceb9fe1a85ec53ULL;
            x ^= x >> 33;
            return x;
        };

        const uint64_t lo = fmix64(low);
        const uint64_t hi = fmix64(high);
        return static_cast<size_t>(lo ^ (hi + 0x9e3779b97f4a7c15ULL + (lo << 6) + (lo >> 2)));
    }
};

// ─── std::formatter:std::format / std::print 直接可用 ───────────────────

template <>
struct std::formatter<cuuidpp::uuid> {
    constexpr auto parse(std::format_parse_context& ctx)
    {
        return ctx.begin();
    }

    auto format(const cuuidpp::uuid& id, std::format_context& ctx) const
    {
        return std::ranges::copy(id.to_array(), ctx.out()).out;
    }
};

namespace cuuidpp {

// ─── uuid_like:作为 uuid 必须满足的接口 ──────────────────────────────────
//
// 可平凡复制的 16 字节连续 uint8_t 存储 + 全序比较 + 可哈希 + 版本可读 + 可文本化。
// 任何满足该接口的类型(含第三方 uuid 库)都可与 usip 组件互换。

template <typename T>
concept uuid_like = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && std::default_initializable<T> && sizeof(T) == 16 && std::same_as<typename T::value_type, uint8_t> && requires(T m, const T c) {
    { c.size() } noexcept -> std::same_as<size_t>;
    { m.data() } noexcept -> std::same_as<uint8_t*>;
    { c.data() } noexcept -> std::same_as<const uint8_t*>;
    { c.version() } noexcept -> std::convertible_to<uint8_t>;
    { c.to_string() } -> std::convertible_to<std::string>;
    requires T { }
    .size() == 16;
} && std::equality_comparable<T> && std::totally_ordered<T> && requires(const T c) {
    { std::hash<T> { }(c) } noexcept -> std::convertible_to<size_t>;
};

// ─── 编译期自测 ──────────────────────────────────────────────────────────

static_assert(uuid_like<uuid>);
static_assert(uuid::nil().is_nil());
static_assert(uuid::max().version() == 0xF && uuid::max().variant() == 3);
static_assert(uuid::from_string("00112233-4455-7677-8899-aabbccddeeff")
                  ->timestamp_ms()
    == 0x001122334455ULL);
static_assert(!uuid::from_string("00112233-4455-7677-8899-aabbccddeefg").has_value());
static_assert(!uuid::from_string("001122334455-7677-8899-aabbccddeeff").has_value());
static_assert(std::string_view(
                  uuid::from_string("00112233-4455-7677-8899-aabbccddeeff")
                      ->to_array()
                      .data(),
                  36)
    == "00112233-4455-7677-8899-aabbccddeeff");

} // namespace cuuidpp
