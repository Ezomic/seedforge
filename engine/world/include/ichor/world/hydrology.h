#pragma once

#include "ichor/world/heightfield.h"

#include <cstdint>
#include <vector>

namespace ichor {

struct HydrologyParams {
    // A sample is a river when this many samples (itself included) drain through it.
    std::uint32_t riverThreshold = 2000;
    // A depression is a lake where the filled surface is this much above the ground, in metres.
    float lakeDepth = 0.05f;
};

// Where the water goes (doc 08, phase 3). Every sample of the heightfield has one place it
// drains to, found by priority-flood: the land is flooded in from its edges, lowest first, and
// each sample drains towards the neighbour that reached it. That makes the flow acyclic by
// construction, so every river ends at the edge of the map or in a lake the flood filled, and
// none can run uphill or stop in the middle of nowhere.
//
// The result depends only on the heights. Ties are broken by the order samples were reached.
class Hydrology {
public:
    static constexpr std::uint8_t kOutlet = 255; // drains off the map

    std::uint32_t side = 0; // samples per side

    // The ground with every depression filled to the level it would spill over.
    std::vector<float> filled;
    // Which of the eight neighbours a sample drains to, or kOutlet at the map's edge.
    std::vector<std::uint8_t> direction;
    // How many samples drain through this one, itself included.
    std::vector<std::uint32_t> accumulation;

    bool empty() const { return filled.empty(); }
    std::size_t index(std::uint32_t sx, std::uint32_t sz) const { return static_cast<std::size_t>(sz) * side + sx; }

    // The sample a sample drains to, or the sample itself at an outlet.
    std::size_t downstream(std::size_t from) const;
    bool isOutlet(std::size_t sample) const { return direction[sample] == kOutlet; }
};

// Needs a heightfield with at least a 3 by 3 grid of samples; anything smaller comes back empty.
Hydrology computeHydrology(const Heightfield& heightfield);

// A sample is a river when enough drains through it, and a lake sample when the ground there is
// below the filled surface by at least lakeDepth.
bool isRiver(const Hydrology& hydrology, std::size_t sample, const HydrologyParams& params);
bool isLake(const Hydrology& hydrology, const Heightfield& heightfield, std::uint32_t sx, std::uint32_t sz, const HydrologyParams& params);

}
