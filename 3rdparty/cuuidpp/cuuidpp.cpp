#include "cuuidpp.hpp"

#include <bit>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>

namespace cuuidpp {
namespace detail {

    // ─── xoshiro256**:速度/统计质量综合最优的非加密 PRNG ─────────────────────
    // 256 位状态,周期 2^256-1,通过 BigCrush;每次调用仅几条移位/异或指令。

    class xoshiro256ss {
    public:
        // splitmix64 把 64 位种子展开为 256 位状态(xoshiro 官方推荐播种法)
        explicit constexpr xoshiro256ss(uint64_t seed) noexcept
        {
            for (auto& word : state_) {
                seed += 0x9E3779B97F4A7C15ULL;
                uint64_t z = seed;
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                word = z ^ (z >> 31);
            }
        }

        constexpr auto operator()() noexcept -> uint64_t
        {
            const uint64_t result = std::rotl(state_[1] * 5, 7) * 9;
            const uint64_t t = state_[1] << 17;

            state_[2] ^= state_[0];
            state_[3] ^= state_[1];
            state_[1] ^= state_[2];
            state_[0] ^= state_[3];

            state_[2] ^= t;
            state_[3] = std::rotl(state_[3], 45);

            return result;
        }

    private:
        std::array<uint64_t, 4> state_ { };
    };

    // ─── 一次性播种:四路独立熵源混合(每线程仅首次生成时执行)─────────────────

    [[nodiscard]] auto gather_entropy() -> uint64_t
    {
        auto device = std::random_device { };
        const uint64_t from_device = (uint64_t { device() } << 32) | device();
        const uint64_t from_clock = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const uint64_t from_thread = std::hash<std::thread::id> { }(std::this_thread::get_id());
        const uint64_t from_aslr = std::bit_cast<uint64_t>(&from_device); // 栈地址即 ASLR 熵

        uint64_t z = from_device ^ std::rotl(from_clock, 21)
            ^ std::rotl(from_thread, 42) ^ from_aslr;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // 大端写入 64 位:C++23 std::byteswap,零成本
    inline auto store_be64(uint8_t* out, uint64_t v) noexcept -> void
    {
        if constexpr (std::endian::native == std::endian::little)
            v = std::byteswap(v);

        std::memcpy(out, &v, sizeof(v));
    }

    [[nodiscard]] inline auto unix_ms_now() noexcept -> uint64_t
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

} // namespace detail

// ─── v7(RFC 9562 §5.7,单调性取 §6.2 固定长度计数器法)─────────────────────
//
// 布局:[48 位 unix_ts_ms][4 位 ver][12 位 rand_a][2 位 var][62 位 rand_b]
// rand_a 兼作同毫秒单调计数器:新毫秒随机起步,同毫秒递增;
// 时钟回拨时沿用 last_ts 不倒退;计数器溢出则虚拟推进时间戳
// (单调性优先于时间精确性,这是 RFC 9562 明确允许的做法)。
// 唯一性由每个 id 的 74 位随机(12 位计数器随机起点 + 62 位 rand_b)保证。

auto generate_uuid_v7() -> uuid
{
    struct generator_state {
        detail::xoshiro256ss rng;
        uint64_t last_ts = 0;
        uint16_t counter = 0; // 仅用低 12 位
    };
    thread_local generator_state state { detail::xoshiro256ss { detail::gather_entropy() } };

    uint64_t ts = detail::unix_ms_now();
    if (ts > state.last_ts) [[likely]] {
        state.last_ts = ts;
        state.counter = static_cast<uint16_t>(state.rng() & 0x0FFF);
    } else if (++state.counter > 0x0FFF) {
        state.counter = 0;
        ++state.last_ts;
    }
    ts = state.last_ts;

    const uint64_t hi = (ts << 16) | (uint64_t { 7 } << 12) | state.counter;
    const uint64_t lo = (uint64_t { 0b10 } << 62) | (state.rng() & 0x3FFF'FFFF'FFFF'FFFFULL);

    uuid id;
    detail::store_be64(id.bytes.data(), hi);
    detail::store_be64(id.bytes.data() + 8, lo);
    return id;
}

// ─── v4(RFC 9562 §5.4):128 位纯随机,覆写版本/变体位 ─────────────────────

auto generate_uuid_v4() -> uuid
{
    thread_local detail::xoshiro256ss rng { detail::gather_entropy() };

    uuid id;
    detail::store_be64(id.bytes.data(), rng());
    detail::store_be64(id.bytes.data() + 8, rng());
    id.bytes[6] = static_cast<uint8_t>((id.bytes[6] & 0x0F) | 0x40); // version = 4
    id.bytes[8] = static_cast<uint8_t>((id.bytes[8] & 0x3F) | 0x80); // variant = 10b
    return id;
}

} // namespace cuuidpp
