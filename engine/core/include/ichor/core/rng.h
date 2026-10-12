#pragma once

#include "ichor/core/hash.h"

#include <cstdint>
#include <string_view>

namespace ichor {

// PCG32. One instance per system, passed explicitly; there is no global RNG
// anywhere in the engine (doc 17, architecture rule 4).
class Rng {
public:
    explicit Rng(std::uint64_t seed, std::uint64_t stream = 0)
        : seed_(seed)
    {
        inc_ = (stream << 1u) | 1u;
        state_ = 0;
        nextU32();
        state_ += seed;
        nextU32();
    }

    // Rebuilds a generator from a snapshot; see scene_io.
    static Rng fromState(std::uint64_t seed, std::uint64_t state, std::uint64_t increment)
    {
        Rng rng(seed);
        rng.state_ = state;
        rng.inc_ = increment;
        return rng;
    }

    // A child generator whose sequence depends on this seed and the name only,
    // so adding a system never reshuffles another system's draws.
    Rng derive(std::string_view name) const
    {
        return Rng(hashCombine(seed_, hash64(name)), hash64(name) >> 1);
    }

    std::uint32_t nextU32()
    {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    std::uint64_t nextU64()
    {
        // Two statements: operand order of | is unspecified and this must
        // draw identically on every compiler.
        const std::uint64_t hi = nextU32();
        const std::uint64_t lo = nextU32();
        return (hi << 32) | lo;
    }

    // [0, 1)
    float nextFloat()
    {
        return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f);
    }

    // [min, max], unbiased
    std::uint32_t range(std::uint32_t min, std::uint32_t max)
    {
        const std::uint32_t span = max - min + 1;
        if (span == 0) {
            return nextU32();
        }
        const std::uint32_t threshold = (0u - span) % span;
        for (;;) {
            const std::uint32_t r = nextU32();
            if (r >= threshold) {
                return min + r % span;
            }
        }
    }

    std::uint64_t state() const { return state_; }
    std::uint64_t increment() const { return inc_; }
    std::uint64_t seed() const { return seed_; }

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;
    std::uint64_t seed_ = 0;
};

}
