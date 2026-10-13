#include "ichor/world/forge.h"
#include "ichor/world/hydrology.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <numeric>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

// A lake sample, or one of its neighbours, as carving sees it.
bool nearLake(const Hydrology& h, const Heightfield& field, std::uint32_t sx, std::uint32_t sz, const HydrologyParams& params)
{
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto nx = static_cast<std::int64_t>(sx) + dx;
            const auto nz = static_cast<std::int64_t>(sz) + dz;
            if (nx >= 0 && nz >= 0 && nx < h.side && nz < h.side && isLake(h, field, static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(nz), params)) {
                return true;
            }
        }
    }
    return false;
}

// A square heightfield n samples to a side, one metre apart, with a height in tenths of a
// metre at each sample.
Heightfield gridOf(std::uint32_t n, const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& tenths)
{
    WorldDesc desc;
    desc.sizeMetres = n - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = n - 1;
    desc.heightRange = 6553.5f; // one sample step is a tenth of a metre
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            samples[static_cast<std::size_t>(z) * n + x] = tenths(x, z);
        }
    }
    return Heightfield(desc, std::move(samples));
}

std::uint64_t drainedOffTheMap(const Hydrology& h)
{
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < h.accumulation.size(); ++i) {
        if (h.isOutlet(i)) {
            total += h.accumulation[i];
        }
    }
    return total;
}

}

TEST_CASE("everything on a slope drains down it, and every sample is counted at an outlet")
{
    // Lower towards x = 0, with a little ripple so no two neighbours are level.
    const Heightfield field = gridOf(17, [](std::uint32_t x, std::uint32_t z) { return static_cast<std::uint16_t>(x * 100 + z % 3); });
    const Hydrology h = computeHydrology(field);
    REQUIRE_FALSE(h.empty());
    REQUIRE(h.side == 17);
    REQUIRE(drainedOffTheMap(h) == 17u * 17u);
    // No lakes on a slope: the filled surface is the ground.
    for (std::uint32_t z = 0; z < 17; ++z) {
        for (std::uint32_t x = 0; x < 17; ++x) {
            REQUIRE(h.filled[h.index(x, z)] == field.heightAtSample(x, z));
        }
    }
    // The water leaves by the low side, where the catchments are biggest.
    std::uint32_t biggest = 0;
    for (std::uint32_t z = 0; z < 17; ++z) {
        biggest = std::max(biggest, h.accumulation[h.index(0, z)]);
    }
    REQUIRE(biggest > 17);
    for (std::uint32_t z = 1; z < 16; ++z) {
        const std::size_t next = h.downstream(h.index(8, z));
        REQUIRE(next % 17 == 7); // one step further down the slope, whatever the row
    }
}

TEST_CASE("a basin fills to the level it spills over and drains out through its lowest rim")
{
    // A high plateau with a deep basin in the middle and a low notch in the edge.
    const Heightfield field = gridOf(17, [](std::uint32_t x, std::uint32_t z) {
        if (x == 16 && z == 8) return std::uint16_t{5000};            // the notch the water leaves by
        if (x >= 5 && x <= 11 && z >= 5 && z <= 11) return std::uint16_t{1000}; // the basin
        return std::uint16_t{10000};
    });
    const Hydrology h = computeHydrology(field);
    HydrologyParams params;
    REQUIRE(isLake(h, field, 8, 8, params));
    REQUIRE_FALSE(isLake(h, field, 2, 2, params)); // the plateau is dry
    // The basin is filled flat to the plateau, the level it spills over.
    REQUIRE(h.filled[h.index(8, 8)] == field.heightAtSample(2, 2));
    REQUIRE(h.filled[h.index(6, 6)] == h.filled[h.index(10, 10)]);
    // Water from the basin has a way out: it ends at an outlet.
    std::size_t at = h.index(8, 8);
    std::size_t steps = 0;
    while (!h.isOutlet(at) && steps++ < 17u * 17u) {
        at = h.downstream(at);
    }
    REQUIRE(h.isOutlet(at));
}

TEST_CASE("no sample drains uphill and every drop of rain reaches the edge, on a forged world")
{
    WorldDesc desc;
    desc.seed = 11;
    desc.sizeMetres = 1024;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    const Heightfield field = forgeHeightfield(desc);
    const Hydrology h = computeHydrology(field);
    const std::size_t total = h.filled.size();
    REQUIRE(total == 129u * 129u);
    REQUIRE(drainedOffTheMap(h) == total);

    std::size_t rivers = 0;
    HydrologyParams params;
    params.riverThreshold = 200;
    for (std::size_t i = 0; i < total; ++i) {
        REQUIRE(h.filled[i] >= field.heightAtSample(static_cast<std::uint32_t>(i % 129), static_cast<std::uint32_t>(i / 129)));
        if (!h.isOutlet(i)) {
            REQUIRE(h.filled[h.downstream(i)] <= h.filled[i]);
        }
        rivers += isRiver(h, i, params) ? 1 : 0;
    }
    REQUIRE(rivers > 0);

    // A walk down from anywhere ends at an outlet within as many steps as there are samples.
    for (std::size_t start = 0; start < total; start += 997) {
        std::size_t at = start;
        std::size_t steps = 0;
        while (!h.isOutlet(at) && steps++ < total) {
            at = h.downstream(at);
        }
        REQUIRE(h.isOutlet(at));
    }
    // Water only gathers on its way down: below any sample there is one with a bigger catchment.
    for (std::size_t i = 0; i < total; ++i) {
        if (!h.isOutlet(i)) {
            REQUIRE(h.accumulation[h.downstream(i)] > h.accumulation[i]);
        }
    }
    // Only the edge of the map is an outlet.
    for (std::uint32_t z = 0; z < 129; ++z) {
        for (std::uint32_t x = 0; x < 129; ++x) {
            const bool edge = x == 0 || z == 0 || x == 128 || z == 128;
            REQUIRE(h.isOutlet(h.index(x, z)) == edge);
        }
    }
}

TEST_CASE("the same ground always drains the same way, and a map too small to flood is left alone")
{
    WorldDesc desc;
    desc.seed = 4;
    desc.sizeMetres = 512;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    const Heightfield field = forgeHeightfield(desc);
    const Hydrology a = computeHydrology(field);
    const Hydrology b = computeHydrology(field);
    REQUIRE(a.direction == b.direction);
    REQUIRE(a.accumulation == b.accumulation);
    REQUIRE(a.filled == b.filled);

    const Heightfield tiny = gridOf(2, [](std::uint32_t, std::uint32_t) { return std::uint16_t{0}; });
    REQUIRE(computeHydrology(tiny).empty());
    REQUIRE(computeHydrology(Heightfield{}).empty());
    REQUIRE_FALSE(isLake(Hydrology{}, field, 0, 0, HydrologyParams{}));
    REQUIRE_FALSE(isRiver(Hydrology{}, 0, HydrologyParams{}));
}

TEST_CASE("a flat plateau drains in a wave to the edge, and a pit takes the lower of its rim's heights")
{
    const Heightfield flat = gridOf(9, [](std::uint32_t, std::uint32_t) { return std::uint16_t{300}; });
    const Hydrology h = computeHydrology(flat);
    REQUIRE(drainedOffTheMap(h) == 81u);
    for (std::uint32_t z = 0; z < 9; ++z) {
        for (std::uint32_t x = 0; x < 9; ++x) {
            const std::size_t i = h.index(x, z);
            REQUIRE(h.filled[i] == flat.heightAtSample(x, z));
            REQUIRE(h.isOutlet(i) == (x == 0 || z == 0 || x == 8 || z == 8));
            if (!h.isOutlet(i)) {
                // Each step is to a neighbour, one sample away in each direction at most.
                const std::size_t d = h.downstream(i);
                REQUIRE(std::abs(static_cast<int>(d % 9) - static_cast<int>(x)) <= 1);
                REQUIRE(std::abs(static_cast<int>(d / 9) - static_cast<int>(z)) <= 1);
            }
        }
    }

    // A pit ringed by a low side and a high side fills to the low one: where the water spills over.
    const Heightfield pit = gridOf(9, [](std::uint32_t x, std::uint32_t z) {
        if (x == 4 && z == 4) return std::uint16_t{0};
        if (x >= 3 && x <= 5 && z >= 3 && z <= 5) return x == 5 ? std::uint16_t{400} : std::uint16_t{900}; // the rim; east side the low one
        if (z == 4 && x >= 6) return static_cast<std::uint16_t>(300 - (x - 6) * 100); // a channel from the low rim to the edge
        return std::uint16_t{2000};
    });
    const Hydrology p = computeHydrology(pit);
    REQUIRE(p.filled[p.index(4, 4)] == pit.heightAtSample(5, 4));
    REQUIRE(p.filled[p.index(3, 4)] == pit.heightAtSample(3, 4)); // the high rim stays as it is
    REQUIRE(isLake(p, pit, 4, 4, HydrologyParams{}));
}

TEST_CASE("the smallest floodable map has one inner sample, and it drains to the edge")
{
    const Heightfield three = gridOf(3, [](std::uint32_t x, std::uint32_t z) { return x == 1 && z == 1 ? std::uint16_t{0} : std::uint16_t{1000}; });
    const Hydrology h = computeHydrology(three);
    REQUIRE_FALSE(h.empty());
    REQUIRE(drainedOffTheMap(h) == 9u);
    REQUIRE_FALSE(h.isOutlet(h.index(1, 1)));
    REQUIRE(h.filled[h.index(1, 1)] == three.heightAtSample(0, 0)); // filled to the rim
    REQUIRE(h.accumulation[h.downstream(h.index(1, 1))] >= 2);
}

TEST_CASE("carving cuts channels along the rivers, deeper downstream, and touches nothing else")
{
    WorldDesc desc;
    desc.seed = 11;
    desc.sizeMetres = 1024;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    const Heightfield field = forgeHeightfield(desc);
    const Hydrology h = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 200;
    const CarveParams carve;
    const Heightfield carved = carveRivers(field, h, rivers, carve);
    REQUIRE(carved.desc().sizeMetres == field.desc().sizeMetres);
    REQUIRE(carved.samples().size() == field.samples().size());
    REQUIRE(carved.digest() != field.digest());

    const float metresPerUnit = desc.heightRange / 65535.0f;
    std::size_t cut = 0;
    for (std::uint32_t z = 0; z < h.side; ++z) {
        for (std::uint32_t x = 0; x < h.side; ++x) {
            const std::size_t i = h.index(x, z);
            const float before = field.heightAtSample(x, z);
            const float after = carved.heightAtSample(x, z);
            const bool river = isRiver(h, i, rivers) && !h.isOutlet(i) && !nearLake(h, field, x, z, rivers);
            if (!river) {
                REQUIRE(after == before);
                continue;
            }
            ++cut;
            REQUIRE(after <= before);
            // Cut by at least the base depth (to a sample's quantum), unless the land already ran out of height.
            REQUIRE((before - after >= carve.baseDepth - 2.0f * metresPerUnit || after <= desc.minHeight + 2.0f * metresPerUnit));
            REQUIRE(before - after <= carve.maxDepth + 2.0f * metresPerUnit);
        }
    }
    REQUIRE(cut > 0);

    // The deepest cut is at the biggest river, the shallowest where one begins.
    const std::uint32_t biggestCatchment = *std::max_element(h.accumulation.begin(), h.accumulation.end());
    REQUIRE(biggestCatchment >= 4 * rivers.riverThreshold);
    float deepestSmall = 0.0f;
    float deepestBig = 0.0f;
    for (std::size_t i = 0; i < h.accumulation.size(); ++i) {
        const auto x = static_cast<std::uint32_t>(i % 129);
        const auto z = static_cast<std::uint32_t>(i / 129);
        if (!isRiver(h, i, rivers) || h.isOutlet(i) || nearLake(h, field, x, z, rivers)) continue;
        const float depth = field.heightAtSample(x, z) - carved.heightAtSample(x, z);
        if (h.accumulation[i] < 2 * rivers.riverThreshold) deepestSmall = std::max(deepestSmall, depth);
        if (h.accumulation[i] >= biggestCatchment / 2) deepestBig = std::max(deepestBig, depth);
    }
    REQUIRE(deepestBig > deepestSmall);

    // The same river, forged again, is cut the same way.
    REQUIRE(carveRivers(field, h, rivers, carve).digest() == carved.digest());
    // A channel still runs downhill: carved ground never rises from one sample to the next down a river.
    for (std::size_t i = 0; i < h.accumulation.size(); ++i) {
        if (h.isOutlet(i)) continue;
        const std::size_t d = h.downstream(i);
        if (isRiver(h, i, rivers) && isRiver(h, d, rivers)) {
            REQUIRE((carved.samples()[d] <= carved.samples()[i] || field.samples()[d] > field.samples()[i]));
        }
    }

    // Nothing is carved from an empty hydrology, one of another size, a zero threshold or a bad height range.
    REQUIRE(carveRivers(field, Hydrology{}, rivers, carve).digest() == field.digest());
    WorldDesc small = desc;
    small.sizeMetres = 512;
    const Heightfield smaller = forgeHeightfield(small);
    REQUIRE(carveRivers(field, computeHydrology(smaller), rivers, carve).digest() == field.digest());
    HydrologyParams nothing;
    nothing.riverThreshold = 0;
    REQUIRE(carveRivers(field, h, nothing, carve).digest() == field.digest());
    WorldDesc flatRange = desc;
    flatRange.heightRange = 0.0f;
    REQUIRE(carveRivers(Heightfield(flatRange, field.samples()), h, rivers, carve).digest() == Heightfield(flatRange, field.samples()).digest());

    // A cut deeper than the land is high stops at the floor.
    CarveParams abyss;
    abyss.baseDepth = 1e6f;
    abyss.maxDepth = 1e6f;
    const Heightfield bottomed = carveRivers(field, h, rivers, abyss);
    std::size_t atFloor = 0;
    for (std::size_t i = 0; i < h.accumulation.size(); ++i) {
        atFloor += bottomed.samples()[i] == 0 && field.samples()[i] != 0 ? 1 : 0;
    }
    std::size_t wouldReachFloor = 0;
    for (std::uint32_t z = 0; z < h.side; ++z) {
        for (std::uint32_t x = 0; x < h.side; ++x) {
            const std::size_t i = h.index(x, z);
            const bool river = isRiver(h, i, rivers) && !h.isOutlet(i) && !nearLake(h, field, x, z, rivers);
            wouldReachFloor += river && field.samples()[i] != 0 ? 1 : 0;
        }
    }
    REQUIRE(wouldReachFloor > 0);
    REQUIRE(atFloor == wouldReachFloor);
}

TEST_CASE("a lake keeps its depth when its rivers are carved, and the map's edge is left level")
{
    // The basin from before: a plateau with a deep basin and a notch to the east.
    const Heightfield field = gridOf(17, [](std::uint32_t x, std::uint32_t z) {
        if (x == 16 && z == 8) return std::uint16_t{5000};
        if (x >= 5 && x <= 11 && z >= 5 && z <= 11) return std::uint16_t{1000};
        return std::uint16_t{10000};
    });
    const Hydrology h = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 3;
    const Heightfield carved = carveRivers(field, h, rivers, CarveParams{});
    REQUIRE(carved.digest() != field.digest()); // something was cut
    for (std::uint32_t z = 0; z < 17; ++z) {
        for (std::uint32_t x = 0; x < 17; ++x) {
            const bool edge = x == 0 || z == 0 || x == 16 || z == 16;
            if (edge || isLake(h, field, x, z, rivers)) {
                REQUIRE(carved.heightAtSample(x, z) == field.heightAtSample(x, z));
            }
        }
    }
    // The same lakes are there to be found after carving, as deep as they were.
    const Hydrology again = computeHydrology(carved);
    REQUIRE(isLake(again, carved, 8, 8, rivers));
    REQUIRE(again.filled[again.index(8, 8)] == h.filled[h.index(8, 8)]);
}
