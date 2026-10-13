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
// The result depends only on the heights. Ties are broken by the order samples were reached, so
// across a flat or a lake the water drains in a wave out from the spill point: deterministic,
// not steepest descent, and a diagonal neighbour is chosen on height alone, not slope per metre.
// Accumulation counts samples, not area.
//
// The working set is about 16 bytes per sample plus the frontier: roughly 1 GB at the largest
// world WorldDesc allows (8193 samples a side), a few tens of MB for the 4 km world.
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
    // downstream and isOutlet need a computed result and a sample inside it.
    std::size_t downstream(std::size_t from) const;
    bool isOutlet(std::size_t sample) const { return direction[sample] == kOutlet; }
};

struct CarveParams {
    float baseDepth = 0.6f;         // metres cut where a river just begins
    float depthPerDoubling = 0.35f; // metres deeper each time the catchment doubles
    float maxDepth = 4.0f;          // a river is never cut deeper than this
};

// Needs a heightfield with at least a 3 by 3 grid of samples; anything smaller comes back empty.
Hydrology computeHydrology(const Heightfield& heightfield);

// A sample is a river when enough drains through it, and a lake sample when the ground there is
// below the filled surface by at least lakeDepth.
bool isRiver(const Hydrology& hydrology, std::size_t sample, const HydrologyParams& params);
bool isLake(const Hydrology& hydrology, const Heightfield& heightfield, std::uint32_t sx, std::uint32_t sz, const HydrologyParams& params);

// The heightfield with a channel cut along every river, deeper the more drains through it, so
// the water has somewhere to lie. Lakes are spared, with their shores and spill points (any
// river sample touching a lake) and the samples at the map's edge, so the lakes keep their
// depth and the edge stays level. Depth never decreases downstream, so a channel still runs
// downhill. A river threshold of zero, a bad height range or a hydrology of another size
// returns the land as it was.
Heightfield carveRivers(const Heightfield& heightfield, const Hydrology& hydrology, const HydrologyParams& rivers, const CarveParams& carve);

}
