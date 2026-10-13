#include "ichor/world/forge.h"
#include "ichor/world/hydrology.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <numeric>

using namespace ichor;

namespace {

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
        REQUIRE(h.downstream(h.index(8, z)) % 17 < 8); // one step further down the slope
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
    REQUIRE(rivers < total / 4);

    // A walk down from anywhere ends at an outlet within as many steps as there are samples.
    for (std::size_t start = 0; start < total; start += 997) {
        std::size_t at = start;
        std::size_t steps = 0;
        while (!h.isOutlet(at) && steps++ < total) {
            at = h.downstream(at);
        }
        REQUIRE(h.isOutlet(at));
    }
    // A river runs on down: below a river sample there is another with at least its catchment.
    for (std::size_t i = 0; i < total; ++i) {
        if (isRiver(h, i, params) && !h.isOutlet(i)) {
            REQUIRE(h.accumulation[h.downstream(i)] > h.accumulation[i]);
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
