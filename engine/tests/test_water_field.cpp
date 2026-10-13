#include "ichor/sim/scene.h"
#include "ichor/world/forge.h"
#include "ichor/world/streamer.h"
#include "ichor/world/water_field.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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
    // Sample x = 4 is dry and x = 5 holds 5 m of water. Halfway between, the surface is half way
    // between the bank's sunk level and the lake's, over ground that is half way down the bank.
    REQUIRE(water.at(4.0, 8.0).depth == 0.0f);
    REQUIRE(water.at(4.5, 8.0).depth == Approx(2.2f).margin(0.05f));
    REQUIRE(water.at(5.0, 8.0).depth == Approx(5.0f).margin(0.05f));
    // The surface never stands above the lake's own level, even where the bank is higher.
    for (double x = 4.0; x <= 6.0; x += 0.125) {
        const WaterField::Sample sample = water.at(x, 8.0);
        if (sample.wet()) {
            REQUIRE(sample.surface <= field.heightAtSample(2, 2) + 0.001f);
            REQUIRE(sample.surface > field.heightAt(x, 8.0));
        }
    }
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
            }
        }
    }
    REQUIRE(rivery > 0);
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
    REQUIRE_FALSE(water.at(8.0, std::nan("")).wet());
    REQUIRE(std::isfinite(water.at(std::nan(""), 8.0).surface));
    REQUIRE_FALSE(water.at(INFINITY, 8.0).wet());
    REQUIRE_FALSE(water.at(8.0, -INFINITY).wet());
    REQUIRE(std::isfinite(water.at(INFINITY, 8.0).surface));
    REQUIRE_FALSE(water.at(-0.0, 8.0).wet());
    REQUIRE_FALSE(water.at(16.0, 16.0).wet()); // the last sample, a dry corner
    REQUIRE(water.at(16.0, 16.0).surface == Approx(field.heightAtSample(16, 16)));

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
    // A shared hydrology of another size is ignored, and the streamer works its own out.
    const Heightfield other = gridOf(9, [](std::uint32_t, std::uint32_t) { return std::uint16_t{0}; });
    StreamerConfig wrongSize = config;
    wrongSize.hydrology = hydrologyOf(other);
    WorldStreamer ignoring(field, wrongSize);
    std::size_t ownWater = 0;
    ignoring.onLoaded([&](const CellPayload& payload) { ownWater += payload.water.vertices.size(); });
    for (int i = 0; i < 4; ++i) {
        ignoring.update({512.0, 0.0, 512.0});
    }
    REQUIRE(ownWater == vertices);

    // A shared hydrology of the right size is what is read: with its water taken out, there is none.
    Hydrology dried = *hydrology;
    std::fill(dried.accumulation.begin(), dried.accumulation.end(), 1u);
    std::fill(dried.lake.begin(), dried.lake.end(), 0u);
    StreamerConfig dry = config;
    dry.hydrology = std::make_shared<const Hydrology>(std::move(dried));
    WorldStreamer dryStreamer(field, dry);
    std::size_t none = 0;
    dryStreamer.onLoaded([&](const CellPayload& payload) { none += payload.water.vertices.size(); });
    for (int i = 0; i < 4; ++i) {
        dryStreamer.update({512.0, 0.0, 512.0});
    }
    REQUIRE(none == 0);
}

TEST_CASE("the water field and the water mesh give the same surface across a cell, shores included")
{
    const Heightfield field = basinOf(9950);
    HydrologyParams rivers;
    rivers.riverThreshold = 100000;
    const auto hydrology = hydrologyOf(field);
    const WaterField water(field, hydrology, rivers);
    const Mesh mesh = buildWaterMesh(field, *hydrology, rivers, {0, 0});
    REQUIRE_FALSE(mesh.vertices.empty());
    std::size_t checked = 0;
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const glm::vec3 a = mesh.vertices[mesh.indices[t]].position;
        const glm::vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
        const glm::vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
        const glm::vec3 centre = (a + b + c) / 3.0f;
        const WaterField::Sample sample = water.at(centre.x, centre.z);
        // Where the mesh stands over the ground the field is wet, at the mesh's height to within the
        // difference between a triangle's slope and a cell's blend; where it is sunk the field is dry.
        const float clearance = centre.y - field.heightAt(centre.x, centre.z);
        if (clearance > 0.25f) {
            REQUIRE(sample.wet());
            REQUIRE(sample.surface == Approx(centre.y).margin(0.25f));
            ++checked;
        } else if (clearance < -0.25f) {
            REQUIRE_FALSE(sample.wet());
        }
    }
    REQUIRE(checked > 20);
}

TEST_CASE("wading takes nothing over the ankles and falls smoothly to a third at chest height")
{
    REQUIRE(wadingSpeedFactor(0.0f) == 1.0f);
    REQUIRE(wadingSpeedFactor(0.1f) == 1.0f);
    REQUIRE(wadingSpeedFactor(-3.0f) == 1.0f);
    REQUIRE(wadingSpeedFactor(std::nanf("")) == 1.0f);
    REQUIRE(wadingSpeedFactor(1.5f) == Approx(0.35f).margin(1e-5f));
    REQUIRE(wadingSpeedFactor(40.0f) == Approx(0.35f).margin(1e-5f));
    float last = 1.0f;
    for (float depth = 0.0f; depth <= 2.0f; depth += 0.05f) {
        const float factor = wadingSpeedFactor(depth);
        REQUIRE(factor <= last); // never faster in deeper water
        REQUIRE(factor >= 0.35f - 1e-5f);
        last = factor;
    }
}

TEST_CASE("a player walking into a lake slows down as the water deepens and speeds up again on the way out")
{
    const Heightfield field = basinOf(9950); // a lake 5 m deep over x = 5..11
    HydrologyParams rivers;
    rivers.riverThreshold = 100000;
    const WaterField water(field, hydrologyOf(field), rivers);

    constexpr double kDt = 1.0 / 60.0;
    Scene scene(3);
    SpawnCommand spawn;
    spawn.id = StableId::of("player");
    spawn.name.value = "player";
    spawn.player = Player{4.0f};
    spawn.transform.position = {1.0, 0.0, 8.0};
    scene.enqueue(spawn);
    scene.setSpeedModifier([&](const glm::dvec3& p) { return static_cast<double>(wadingSpeedFactor(water.at(p.x, p.z).depth)); });
    InputFrame east;
    east.moveX = 1.0f;
    double dryStep = 0.0;
    double deepStep = 1e9;
    double lastX = 1.0;
    for (int i = 0; i < 1200 && lastX < 15.0; ++i) {
        scene.tick(kDt, east);
        const double x = scene.registry().get<Transform>(scene.find(StableId::of("player"))).position.x;
        const double step = x - lastX;
        lastX = x;
        if (x < 3.0) dryStep = std::max(dryStep, step);
        if (x > 6.5 && x < 10.0) deepStep = std::min(deepStep, step);
    }
    REQUIRE(dryStep == Approx(4.0 * kDt).margin(1e-9));
    REQUIRE(deepStep == Approx(4.0 * 0.35 * kDt).margin(1e-6)); // chest deep in the middle
    REQUIRE(lastX >= 15.0);                                      // and out the other side
}
