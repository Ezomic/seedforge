#pragma once

#include <cstdint>
#include <string_view>

namespace ichor {

// FNV-1a. Stable across platforms and builds, which is the whole point:
// doc 14 derives every entity ID from world structure through this.
constexpr std::uint64_t hash64(std::string_view text, std::uint64_t seed = 14695981039346656037ull)
{
    std::uint64_t h = seed;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

constexpr std::uint64_t hashCombine(std::uint64_t a, std::uint64_t b)
{
    return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
}

}
