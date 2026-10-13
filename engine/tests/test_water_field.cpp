#include "ichor/world/forge.h"
#include "ichor/world/streamer.h"
#include "ichor/world/water_field.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

// A square heightfield n samples to a side, one metre apart, one cell, heights in tenths of a metre.
Heightfield gridOf(std::uint32_t n, const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& tenths)
{
    WorldDesc desc;
    desc.sizeMetres = n - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = n - 1;
    desc.heightRange = 6553.5f;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            samples[static_cast<std::size_t>(z) * n + x] = tenths(x, z);
        }
    }
    return Heightfield(desc, std::move(samples));
}

// A plateau with a basin of the given floor and a notch the water leaves by.
Heightfield basinOf(std::uint16_t floor)
{
    return gridOf(17, [floor](std::uint32_t x, std::uint32_t z) {
        if (x == 16 && z == 8) return std::uint16_t{5000};
        if (x >= 5 && x <= 11 && z >= 5 && z <= 11) return floor;
        return std::uint16_t{10000};
    });
}

std::shared_ptr<const Hydrology> hydrologyOf(const Heightfield& field)
{
    return std::make_shared<const Hydrology>(computeHydrology(field));
}

}

TEST_CASE("a lake is as deep as its surface is over the ground, and dry land has no water")
{
    const Heightfield field = basinOf(9950); // 5 m below the rim
    HydrologyParams rivers;
    rivers.riverThreshold = 100000;
    const WaterField water(field, hydrologyOf(field), rivers);

    const WaterField::Sample middle = water.at(8.0, 8.0);
    REQUIRE(middle.wet());
    REQUIRE(middle.depth == Approx(5.0f).margin(0.05f));
    // The surface is the lake's level, the same over the whole lake.
    REQUIRE(middle.surface == Approx(field.heightAtSample(2, 2)).margin(0.001f));
    REQUIRE(water.at(6.0, 10.0).surface == Approx(middle.surface).margin(0.001f));

    const WaterField::Sample plateau = water.at(2.0, 2.0);
    REQUIRE_FALSE(plateau.wet());
    REQUIRE(plateau.depth == 0.0f);
    REQUIRE(plateau.surface == Approx(field.heightAt(2.0, 2.0)));
}

TEST_CASE("the shore is a slope: depth blends between the samples around a point")
{
    const Heightfield field = basinOf(9950);
    HydrologyParams rivers;
    rivers.riverThreshold = 100000;
    const WaterField water(field, hydrologyOf(field), rivers);
    // Sample x = 4 is dry and x = 5 holds 5 m of water, so halfway between there is half as much.
    REQUIRE(water.at(4.0, 8.0).depth == 0.0f);
    REQUIRE(water.at(4.5, 8.0).depth == Approx(2.5f).margin(0.05f));
    REQUIRE(water.at(5.0, 8.0).depth == Approx(5.0f).margin(0.05f));
    // And it only ever rises towards the middle.
    float last = -1.0f;
    for (double x = 4.0; x <= 6.0; x += 0.25) {
        const float depth = water.at(x, 8.0).depth;
        REQUIRE(depth >= last);
        last = depth;
    }
}

TEST_CASE("a river has the depth the meshes give it, and the water field and the mesh agree on where water is")
{
    WorldDesc desc;
    desc.seed = 11;
    desc.sizeMetres = 1024;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    const Heightfield field = forgeHeightfield(desc);
    const auto hydrology = hydrologyOf(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 200;
    rivers.maxLakeDepth = kPlayableLakeDepth;
    WaterMeshParams mesh;
    mesh.riverSurface = 0.4f;
    const WaterField water(field, hydrology, rivers, mesh);

    std::size_t rivery = 0;
    std::size_t lakey = 0;
    for (std::uint32_t z = 1; z < 128; ++z) {
        for (std::uint32_t x = 1; x < 128; ++x) {
            const std::size_t i = hydrology->index(x, z);
            const WaterField::Sample sample = water.at(x * 8.0, z * 8.0);
            const bool lake = isLake(*hydrology, field, x, z, rivers);
            const bool river = !lake && hydrology->lake[i] == 0 && isRiver(*hydrology, i, rivers);
            REQUIRE(sample.wet() == (lake || river));
            if (river) {
                REQUIRE(sample.depth == Approx(0.4f).margin(0.001f));
                ++rivery;
            }
            if (lake) {
                REQUIRE(sample.depth >= rivers.lakeDepth);
                ++lakey;
            }
        }
    }
    REQUIRE(rivery > 0);
    (void)lakey;
}

TEST_CASE("outside the map, with no hydrology, or with a hydrology of another world, there is no water")
{
    const Heightfield field = basinOf(9950);
    HydrologyParams rivers;
    rivers.riverThreshold = 100000;
    const WaterField water(field, hydrologyOf(field), rivers);
    REQUIRE_FALSE(water.at(-1.0, 8.0).wet());
    REQUIRE_FALSE(water.at(8.0, 17.0).wet());
    REQUIRE_FALSE(water.at(1000.0, 1000.0).wet());
    REQUIRE_FALSE(water.at(std::nan(""), 8.0).wet());

    const WaterField none(field, nullptr, rivers);
    REQUIRE_FALSE(none.at(8.0, 8.0).wet());
    REQUIRE(none.at(8.0, 8.0).surface == Approx(field.heightAt(8.0, 8.0)));

    const Heightfield other = gridOf(9, [](std::uint32_t, std::uint32_t) { return std::uint16_t{0}; });
    const WaterField mismatched(field, hydrologyOf(other), rivers);
    REQUIRE_FALSE(mismatched.at(8.0, 8.0).wet());
}

TEST_CASE("a basin deeper than the cap holds no water, here or in the meshes")
{
    const Heightfield field = basinOf(1000); // 90 m deep
    HydrologyParams rivers;
    rivers.riverThreshold = 3;
    rivers.maxLakeDepth = kPlayableLakeDepth;
    const WaterField water(field, hydrologyOf(field), rivers);
    for (double z = 5.0; z <= 11.0; z += 1.0) {
        for (double x = 5.0; x <= 11.0; x += 1.0) {
            REQUIRE_FALSE(water.at(x, z).wet());
        }
    }
}

TEST_CASE("a streamer given the world's hydrology uses it instead of working it out again")
{
    WorldDesc desc;
    desc.seed = 11;
    desc.sizeMetres = 1024;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    const Heightfield field = forgeHeightfield(desc);
    const auto hydrology = hydrologyOf(field);

    StreamerConfig config;
    config.synchronous = true;
    config.radiusCells = 1;
    config.requiredRadius = 1;
    config.activationsPerUpdate = 100;
    config.water = true;
    config.rivers.riverThreshold = 200;
    config.hydrology = hydrology;
    WorldStreamer streamer(field, config);
    std::size_t vertices = 0;
    streamer.onLoaded([&](const CellPayload& payload) { vertices += payload.water.vertices.size(); });
    for (int i = 0; i < 4; ++i) {
        streamer.update({512.0, 0.0, 512.0});
    }
    REQUIRE(vertices > 0);
    // A shared hydrology of another world would give no water: proof that the shared one is what was read.
    const Heightfield other = gridOf(9, [](std::uint32_t, std::uint32_t) { return std::uint16_t{0}; });
    StreamerConfig wrong = config;
    wrong.hydrology = hydrologyOf(other);
    WorldStreamer wrongStreamer(field, wrong);
    std::size_t none = 0;
    wrongStreamer.onLoaded([&](const CellPayload& payload) { none += payload.water.vertices.size(); });
    for (int i = 0; i < 4; ++i) {
        wrongStreamer.update({512.0, 0.0, 512.0});
    }
    REQUIRE(none == 0);
}
