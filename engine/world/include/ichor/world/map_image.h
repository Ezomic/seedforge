#pragma once

#include "ichor/world/heightfield.h"

#include <cstdint>
#include <vector>

namespace ichor {

// A small picture of the whole world, north up (world -Z is the top row), opaque RGBA.
struct MapImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
    bool empty() const { return width == 0 || height == 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4; }
};

constexpr std::uint32_t kMapSide = 128;

// The terrain shaded by height and slope, with water below the world's water level in blue. The
// picture is as wide as the heightfield has samples, down to at most maxSide. The same world gives
// the same bytes. An empty or inconsistent heightfield gives an empty image.
MapImage renderMapImage(const Heightfield& heightfield, std::uint32_t maxSide = kMapSide);

// Where a world position falls on the map, as fractions of its width and height from the top
// left. A position outside the world sits on the nearest edge, and one that is not a number in the middle.
struct MapPoint {
    float u = 0.5f;
    float v = 0.5f;
    bool operator==(const MapPoint&) const = default;
};
MapPoint mapPosition(const WorldDesc& desc, double x, double z);

}
