#pragma once

#include "ichor/core/reflect.h"

#include <cstdint>
#include <tuple>

namespace ichor {

// The shape of a forged world: a square heightmap sampled every
// `metresPerSample`, cut into square streaming cells. Doc 08's 4 km world is
// 4096 m at 4 m with 512 m cells; tests use smaller ones.
struct WorldDesc {
    std::uint64_t seed = 1;
    std::uint32_t sizeMetres = 4096;
    std::uint32_t metresPerSample = 4;
    std::uint32_t cellMetres = 512;
    float minHeight = 0.0f;   // metres, what a sample of 0 means
    float heightRange = 400.0f; // metres, what a sample of 65535 adds

    std::uint32_t samplesPerSide() const { return sizeMetres / metresPerSample + 1; }
    std::uint32_t cellsPerSide() const { return sizeMetres / cellMetres; }
    std::uint32_t samplesPerCellSide() const { return cellMetres / metresPerSample + 1; }
    bool valid() const
    {
        return sizeMetres > 0 && metresPerSample > 0 && cellMetres > 0 && sizeMetres % cellMetres == 0 && cellMetres % metresPerSample == 0
            && heightRange > 0.0f && cellsPerSide() <= 256 && samplesPerSide() <= 8193;
    }
};

template <>
struct Reflect<WorldDesc> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("seed", &WorldDesc::seed), field("sizeMetres", &WorldDesc::sizeMetres), field("metresPerSample", &WorldDesc::metresPerSample),
            field("cellMetres", &WorldDesc::cellMetres), field("minHeight", &WorldDesc::minHeight), field("heightRange", &WorldDesc::heightRange));
    }
};

struct CellId {
    std::int32_t x = 0;
    std::int32_t z = 0;
    bool operator==(const CellId&) const = default;
    auto operator<=>(const CellId&) const = default;
};

// A unique key per cell, for naming its collision body: x in the high half, z in the low.
inline std::uint64_t cellKey(CellId id)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(id.x)) << 32) | static_cast<std::uint32_t>(id.z);
}

}
