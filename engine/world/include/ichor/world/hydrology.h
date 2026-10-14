#pragma once

#include "ichor/world/heightfield.h"

#include <cstdint>
#include <vector>

namespace ichor {

// The deepest basin a world lets stand as a lake; the forge and the game both use it, so the rivers
// carved through deeper basins are the ones the game does not draw a lake over.
constexpr float kPlayableLakeDepth = 10.0f;

// No river is wider than this, whatever the parameters say, so that finding the water around a
// sample never means searching further than this.
constexpr float kRiverHalfWidthCeiling = 64.0f;

struct HydrologyParams {
    // A sample is a river when this many samples (itself included) drain through it.
    std::uint32_t riverThreshold = 2000;
    // A depression is a lake where the filled surface is this much above the ground, in metres.
    float lakeDepth = 0.05f;
    // A lake whose deepest point is deeper than this is not drawn as one. The flood fills a basin
    // to its rim however high that is, which on rough ground makes reservoirs hundreds of metres
    // deep; a real world would have breached the rim. The default is no limit.
    float maxLakeDepth = 1.0e9f;
    // Half the width of a river where it begins, in metres. It grows with the square root of the
    // catchment over riverThreshold, the way real channels do, and stops at maxHalfWidth. A river is
    // never narrower than one sample; a maxHalfWidth of zero keeps every river one sample wide.
    float springHalfWidth = 1.5f;
    float maxHalfWidth = 12.0f;
};

// How far from the middle of a river the water reaches, in metres, for a sample that this many
// samples drain through: zero for a sample that is not a river, springHalfWidth where a river
// begins, then wider with the catchment, never narrower downstream and never past maxHalfWidth.
// Not a number in the parameters counts as no widening. Plain float arithmetic and one correctly
// rounded square root, so it is the same on every machine.
float riverHalfWidth(std::uint32_t accumulation, const HydrologyParams& params);

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
// The working set is about 20 bytes per sample plus the frontier: roughly 1.2 GB at the largest
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
    // The lake a sample lies under, counting from one, or zero when it is dry: a run of samples
    // with the same filled level above the ground. lakeDeepest[id - 1] is the deepest point of lake id.
    // Basins that touch edge to edge at one level count as one lake.
    std::vector<std::uint32_t> lake;
    std::vector<float> lakeDeepest;

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
