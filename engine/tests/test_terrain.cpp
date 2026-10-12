#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/sim/scene.h"
#include "ichor/world/forge.h"
#include "ichor/world/streamer.h"
#include "ichor/world/world_io.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <set>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;

WorldDesc smallWorld(std::uint64_t seed = 3)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 1024;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

WorldDesc bigWorld()
{
    // Doc 08's world: 4 km at 4 m per sample, 512 m cells, 8 by 8.
    WorldDesc desc;
    desc.seed = 7;
    desc.sizeMetres = 4096;
    desc.metresPerSample = 4;
    desc.cellMetres = 512;
    return desc;
}

// Terrain collision for a cell goes into the scene's physics world, as the app does it.
void attachCollision(Scene& scene, const Heightfield& heightfield, WorldStreamer& streamer)
{
    streamer.onLoaded([&](const CellPayload& payload) {
        scene.physics().addHeightField(StableId::of("cell", cellKey(payload.cell.id)), payload.cell.heights.data(), payload.cell.samplesPerSide,
            {heightfield.cellOriginX(payload.cell.id), 0.0, heightfield.cellOriginZ(payload.cell.id)}, static_cast<float>(heightfield.desc().metresPerSample));
    });
    streamer.onUnloaded([&](CellId id) {
        scene.physics().remove(StableId::of("cell", cellKey(id)));
    });
}

SpawnCommand walker(glm::dvec3 position)
{
    SpawnCommand spawn;
    spawn.id = StableId::of("walker");
    spawn.name.value = "walker";
    spawn.transform.position = position;
    spawn.player = Player{6.0f};
    spawn.character = CharacterBody{0.3f, 0.6f};
    return spawn;
}

}

TEST_CASE("the forge is deterministic and stays in range")
{
    const Heightfield a = forgeHeightfield(smallWorld(3));
    const Heightfield b = forgeHeightfield(smallWorld(3));
    const Heightfield c = forgeHeightfield(smallWorld(4));
    REQUIRE(a.digest() == b.digest());
    REQUIRE(a.digest() != c.digest());
    REQUIRE(a.samples().size() == 129u * 129u);
    float lowest = 1e9f;
    float highest = -1e9f;
    for (std::uint32_t z = 0; z < 129; ++z) {
        for (std::uint32_t x = 0; x < 129; ++x) {
            lowest = std::min(lowest, a.heightAtSample(x, z));
            highest = std::max(highest, a.heightAtSample(x, z));
        }
    }
    REQUIRE(lowest >= 0.0f);
    REQUIRE(highest <= 400.0f);
    REQUIRE(highest - lowest > 50.0f); // there is terrain, not a plane
}

TEST_CASE("sampling is bilinear and cells share their edge samples")
{
    const Heightfield field = forgeHeightfield(smallWorld());
    REQUIRE(field.heightAt(80.0, 160.0) == Catch::Approx(field.heightAtSample(10, 20)));
    const float mid = field.heightAt(84.0, 160.0);
    REQUIRE(mid == Catch::Approx(0.5f * (field.heightAtSample(10, 20) + field.heightAtSample(11, 20))));
    REQUIRE(glm::length(field.normalAt(300.0, 300.0)) == Catch::Approx(1.0f));

    Heightfield::Cell left;
    Heightfield::Cell right;
    REQUIRE(field.cell({0, 1}, left));
    REQUIRE(field.cell({1, 1}, right));
    REQUIRE(left.samplesPerSide == 33);
    for (std::uint32_t z = 0; z < 33; ++z) {
        REQUIRE(left.heights[z * 33 + 32] == right.heights[z * 33 + 0]);
    }
    REQUIRE_FALSE(field.cell({4, 0}, left));
    REQUIRE(field.cellAt(255.9, 256.1) == CellId{0, 1});
}

TEST_CASE("the world round-trips through its on-disk layout")
{
    const Heightfield field = forgeHeightfield(smallWorld(11));
    const auto dir = std::filesystem::temp_directory_path() / "ichor_world";
    REQUIRE(writeWorld(dir, field));
    Heightfield loaded;
    REQUIRE(readWorld(dir, loaded));
    REQUIRE(loaded.digest() == field.digest());
    REQUIRE(loaded.desc().cellMetres == 256);
    std::filesystem::remove_all(dir);
}

TEST_CASE("a cell builds three levels of detail with skirts and valid indices")
{
    const Heightfield field = forgeHeightfield(smallWorld());
    CellPayload payload;
    WorldStreamer::buildPayload(field, {1, 2}, 3, payload);
    REQUIRE(payload.lods.size() == 3);
    REQUIRE(payload.lods[0].vertices.size() == 33u * 33u + 33u * 4u);
    REQUIRE(payload.lods[1].vertices.size() == 9u * 9u + 9u * 4u);
    REQUIRE(payload.lods[2].vertices.size() == 3u * 3u + 3u * 4u);
    for (const Mesh& lod : payload.lods) {
        REQUIRE(lod.indices.size() % 3 == 0);
        for (std::uint32_t index : lod.indices) {
            REQUIRE(index < lod.vertices.size());
        }
    }
}

TEST_CASE("streaming keeps the ring resident across every cell boundary of the 4 km world and leaks nothing")
{
    const Heightfield field = forgeHeightfield(bigWorld());
    REQUIRE(field.desc().cellsPerSide() == 8);
    StreamerConfig config;
    config.synchronous = true;
    config.radiusCells = 1;
    config.hysteresisCells = 1;
    config.activationsPerUpdate = 64;
    WorldStreamer streamer(field, config);

    // A serpentine over every cell centre crosses every interior boundary in both axes.
    std::vector<glm::dvec3> path;
    for (int z = 0; z < 8; ++z) {
        for (int i = 0; i < 8; ++i) {
            const int x = z % 2 == 0 ? i : 7 - i;
            path.push_back({x * 512.0 + 256.0, 0.0, z * 512.0 + 256.0});
        }
    }
    std::size_t maxResident = 0;
    for (const glm::dvec3& anchor : path) {
        streamer.update(anchor);
        REQUIRE(streamer.ready(anchor));
        const CellId centre = field.cellAt(anchor.x, anchor.z);
        for (CellId id : streamer.resident()) {
            REQUIRE(std::max(std::abs(id.x - centre.x), std::abs(id.z - centre.z)) <= 2);
        }
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const CellId id{centre.x + dx, centre.z + dz};
                if (field.contains(id)) REQUIRE(streamer.resident().contains(id));
            }
        }
        maxResident = std::max(maxResident, streamer.resident().size());
    }
    REQUIRE(maxResident <= 25);
    REQUIRE(streamer.loads() >= 64);
    REQUIRE(streamer.loads() == streamer.unloads() + streamer.resident().size());
    streamer.clear();
    REQUIRE(streamer.resident().empty());
    REQUIRE(streamer.loads() == streamer.unloads());
}

TEST_CASE("a budget spreads activations over ticks and the gate waits for the required ring")
{
    const Heightfield field = forgeHeightfield(smallWorld());
    StreamerConfig config;
    config.synchronous = true;
    config.radiusCells = 1;
    config.requiredRadius = 1;
    config.activationsPerUpdate = 2;
    WorldStreamer streamer(field, config);
    const glm::dvec3 anchor(400.0, 0.0, 400.0);
    streamer.update(anchor);
    REQUIRE(streamer.resident().size() == 2);
    REQUIRE_FALSE(streamer.ready(anchor));
    for (int i = 0; i < 4; ++i) {
        streamer.update(anchor);
    }
    REQUIRE(streamer.resident().size() == 9);
    REQUIRE(streamer.ready(anchor));
}

TEST_CASE("a character stands on the forged terrain")
{
    const Heightfield field = forgeHeightfield(smallWorld());
    Scene scene(1);
    StreamerConfig config;
    config.synchronous = true;
    config.activationsPerUpdate = 64;
    WorldStreamer streamer(field, config);
    attachCollision(scene, field, streamer);

    const glm::dvec3 start(500.0, field.heightAt(500.0, 500.0) + 5.0, 500.0);
    streamer.update(start);
    scene.enqueue(walker(start));
    for (int i = 0; i < 240; ++i) {
        scene.tick(kDt);
    }
    const auto& t = scene.registry().get<Transform>(scene.find(StableId::of("walker")));
    REQUIRE(scene.registry().get<CharacterBody>(scene.find(StableId::of("walker"))).onGround);
    REQUIRE(std::abs(t.position.y - field.heightAt(t.position.x, t.position.z)) < 0.15);
}

TEST_CASE("the floating origin follows a walk of three kilometres")
{
    const Heightfield field = forgeHeightfield(bigWorld());
    Scene scene(2);
    StreamerConfig config;
    config.synchronous = true;
    config.activationsPerUpdate = 64;
    WorldStreamer streamer(field, config);
    attachCollision(scene, field, streamer);

    glm::dvec3 at(300.0, 0.0, 300.0);
    at.y = field.heightAt(at.x, at.z) + 2.0;
    streamer.update(at);
    scene.enqueue(walker(at));
    scene.tick(kDt);
    REQUIRE(scene.physics().origin() == glm::dvec3(0.0));

    InputFrame east;
    east.moveX = 1.0f;
    east.buttons = InputFrame::Sprint;
    std::uint64_t hashBeforeRebase = 0;
    bool rebased = false;
    for (int i = 0; i < 60 * 300; ++i) { // 300 s at 12 m/s is 3.6 km, the world is 4 km
        const auto& t = scene.registry().get<Transform>(scene.find(StableId::of("walker")));
        streamer.update(t.position);
        if (!streamer.ready(t.position)) continue;
        scene.tick(kDt, east);
        if (!rebased && scene.physics().origin() != glm::dvec3(0.0)) {
            rebased = true;
            hashBeforeRebase = scene.stateHash();
        }
        if (t.position.x > 3500.0) break;
    }
    const auto& t = scene.registry().get<Transform>(scene.find(StableId::of("walker")));
    REQUIRE(rebased);
    REQUIRE(scene.physics().origin().x >= 2048.0);
    REQUIRE(t.position.x > 3000.0);
    REQUIRE(scene.registry().get<CharacterBody>(scene.find(StableId::of("walker"))).onGround);
    REQUIRE(std::abs(t.position.y - field.heightAt(t.position.x, t.position.z)) < 0.3);
    REQUIRE(hashBeforeRebase != 0);
}

TEST_CASE("the tick gate holds the simulation and owes no time")
{
    NullPlatform platform;
    NullRenderer renderer;
    Engine engine(platform, renderer, {});
    bool open = false;
    engine.setTickGate([&](const Scene&) { return open; });
    for (int i = 0; i < 10; ++i) {
        REQUIRE(engine.frame(kDt));
    }
    REQUIRE(engine.gated());
    REQUIRE(engine.scene().tickCount() == 0);
    open = true;
    REQUIRE(engine.frame(kDt));
    REQUIRE_FALSE(engine.gated());
    REQUIRE(engine.scene().tickCount() == 1); // not ten in a burst
}

TEST_CASE("terrain draws are rebuilt every frame, never accumulated")
{
    NullPlatform platform;
    NullRenderer renderer;
    Engine engine(platform, renderer, {});
    engine.setViewBuilder([](const Scene&, double, RenderView& view) {
        for (std::uint32_t cell = 0; cell < 3; ++cell) {
            view.terrain.push_back({cell, glm::dvec3(0.0), 0});
        }
    });
    for (int i = 0; i < 5; ++i) {
        REQUIRE(engine.frame(kDt));
    }
    REQUIRE(renderer.terrainDrawsSubmitted() == 15);
}

TEST_CASE("the floating origin does not thrash on a boundary walk")
{
    Scene scene(3);
    scene.enqueue(walker({1000.0, 5.0, 100.0}));
    scene.tick(kDt);
    REQUIRE(scene.physics().origin() == glm::dvec3(0.0));
    // The rebase reads the transform the previous step wrote, so two ticks settle a move.
    const auto moveTo = [&](double x) {
        scene.physics().setCharacterPosition(StableId::of("walker"), {x, 5.0, 100.0});
        scene.tick(kDt);
        scene.tick(kDt);
    };
    for (double x : {1030.0, 1000.0, 1030.0, 1500.0}) {
        moveTo(x);
        REQUIRE(scene.physics().origin() == glm::dvec3(0.0));
    }
    moveTo(1600.0);
    REQUIRE(scene.physics().origin() == glm::dvec3(1024.0, 0.0, 0.0));
    moveTo(1000.0);
    REQUIRE(scene.physics().origin() == glm::dvec3(1024.0, 0.0, 0.0));
}
