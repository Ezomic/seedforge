#pragma once

#include "ichor/assets/bundle_types.h"

#include <array>
#include <cstring>
#include <cstdint>
#include <span>
#include <string_view>

namespace ichor {

// The regions a skinned body is split into, one glTF primitive each (`extras.region`). Equipment
// that brings its own limbs hides the regions it covers; the mask has one bit per entry here.
constexpr std::array<std::string_view, 9> kBodyRegions{"head", "torso", "upper_arms", "forearms", "hands", "pelvis", "thighs", "shins", "feet"};
constexpr std::uint32_t kAllBodyRegions = (1u << kBodyRegions.size()) - 1u;

inline std::string_view regionName(const Submesh& sub)
{
    return std::string_view(sub.region, strnlen(sub.region, kRegionNameSize));
}

// Zero for a submesh with no region or one that is not a body region, which equipment never hides.
inline std::uint32_t regionBit(std::string_view name)
{
    for (std::size_t i = 0; i < kBodyRegions.size(); ++i) {
        if (kBodyRegions[i] == name) return 1u << i;
    }
    return 0;
}

inline std::uint32_t regionMask(std::span<const std::string_view> names)
{
    std::uint32_t mask = 0;
    for (std::string_view name : names) {
        mask |= regionBit(name);
    }
    return mask;
}

inline bool submeshHidden(const Submesh& sub, std::uint32_t hiddenMask)
{
    return (regionBit(regionName(sub)) & hiddenMask) != 0;
}

}
