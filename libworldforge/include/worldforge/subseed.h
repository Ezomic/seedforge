#pragma once

#include <cstdint>
#include <string_view>

// Hash-derived subseeds (doc 08, Determinism). Every seed below is a pure function of its inputs,
// so no derivation depends on how many others ran or in what order. There is no RNG stream here
// to share, advance or desynchronise. Integer-only, so results are identical on every platform.
namespace worldforge {

namespace detail {

constexpr std::uint64_t kRegionDomain = 0x7265676e5f736565ull;
constexpr std::uint64_t kDungeonDomain = 0x756e675f73656564ull;
constexpr std::uint64_t kLootDomain = 0x6c6f6f745f736565ull;
constexpr std::uint64_t kRerollDomain = 0x7265726f6c6c5f73ull;

}

// Steele, Lea and Flood's SplitMix64: advance by the golden gamma, then finalize. Bijective, and
// splitmix64(0) is the first output of the reference generator seeded with 0.
constexpr std::uint64_t splitmix64(std::uint64_t x)
{
    std::uint64_t z = x + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// Order matters: (1, 2) and (2, 1) hash apart. Negative coordinates sign-extend, so they are distinct.
constexpr std::uint64_t hashCell(std::int32_t x, std::int32_t y)
{
    const auto ux = static_cast<std::uint64_t>(static_cast<std::int64_t>(x));
    const auto uy = static_cast<std::uint64_t>(static_cast<std::int64_t>(y));
    return splitmix64(splitmix64(ux) ^ uy);
}

// FNV-1a over the bytes, finalized with splitmix64 because FNV alone leaves its low bits weak.
constexpr std::uint64_t hashText(std::string_view text)
{
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return splitmix64(h);
}

constexpr std::uint64_t regionSeed(std::uint64_t worldSeed, std::int32_t rx, std::int32_t ry)
{
    return splitmix64(splitmix64(worldSeed ^ detail::kRegionDomain) ^ hashCell(rx, ry));
}

constexpr std::uint64_t dungeonSeed(std::uint64_t worldSeed, std::string_view poiId)
{
    return splitmix64(splitmix64(worldSeed ^ detail::kDungeonDomain) ^ hashText(poiId));
}

// `dungeon` is the dungeon's own seed, so a chest's loot follows its dungeon.
constexpr std::uint64_t lootSeed(std::uint64_t dungeon, std::string_view chestId)
{
    return splitmix64(splitmix64(dungeon ^ detail::kLootDomain) ^ hashText(chestId));
}

// Seed for retry `attempt` after a world fails its playability gates.
constexpr std::uint64_t rerollSeed(std::uint64_t worldSeed, std::uint32_t attempt)
{
    return splitmix64(splitmix64(worldSeed ^ detail::kRerollDomain) ^ splitmix64(attempt));
}

}
