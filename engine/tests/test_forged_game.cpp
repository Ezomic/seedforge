#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

using namespace ichor;

namespace {

constexpr int kGrid = 64;

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

std::unique_ptr<ForgedGame> gameFor(std::uint64_t seed)
{
    ForgeJob job(descFor(seed), kGrid);
    REQUIRE(job.start());
    for (int i = 0; i < 12000 && job.state() == ForgeJobState::Running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(job.error());
    REQUIRE(job.state() == ForgeJobState::Done);
    auto game = buildForgedGame(*job.result(), descFor(seed));
    REQUIRE(game->ok());
    return game;
}

Heightfield flatWorld(float heightMetres)
{
    WorldDesc desc;
    desc.sizeMetres = 1024;
    desc.cellMetres = 256;
    desc.minHeight = 0.0f;
    desc.heightRange = 400.0f;
    desc.waterHeight = 10.0f;
    const auto sample = static_cast<std::uint16_t>(std::lround(heightMetres / desc.heightRange * 65535.0f));
    return Heightfield(desc, std::vector<std::uint16_t>(static_cast<std::size_t>(desc.samplesPerSide()) * desc.samplesPerSide(), sample));
}

}

TEST_CASE("the same seed builds the same forged game and another seed a different one", "[forged_game]")
{
    const auto a = gameFor(1);
    const auto b = gameFor(1);
    const auto c = gameFor(2);
    CHECK(a->stateHash() == b->stateHash());
    CHECK(a->stateHash() != c->stateHash());
    CHECK(a->heightfield.digest() == b->heightfield.digest());
    CHECK(a->heightfield.digest() != c->heightfield.digest());
}

TEST_CASE("the spawn is on dry ground that is not steep", "[forged_game]")
{
    for (const std::uint64_t seed : {1u, 2u}) {
        const auto game = gameFor(seed);
        const ForgedSpawn& spawn = game->spawn;
        INFO("seed " << seed);
        REQUIRE(spawn.found);
        CHECK(game->heightfield.heightAt(spawn.x, spawn.z) > game->desc.waterLevel());
        CHECK(game->heightfield.normalAt(spawn.x, spawn.z).y >= ForgedSpawnParams{}.minUpY);
        CHECK(spawn.groundY == game->heightfield.heightAt(spawn.x, spawn.z));
    }
}

TEST_CASE("the spawn search starts at the settlement nearest the centre", "[forged_game]")
{
    const Heightfield flat = flatWorld(100.0f);
    const ForgedSpawn centre = findForgedSpawn(flat, nullptr, {});
    REQUIRE(centre.found);
    CHECK(centre.x == 512.0);
    CHECK(centre.z == 512.0);

    const ForgedSpawn near = findForgedSpawn(flat, nullptr, {{100.0, 100.0}, {600.0, 560.0}, {900.0, 900.0}});
    REQUIRE(near.found);
    CHECK(near.x == 600.0);
    CHECK(near.z == 560.0);
}

TEST_CASE("the spawn skips ground that is wet, steep or blocked", "[forged_game]")
{
    // A world that is all below the water level has no dry ground: the search ends at the centre, not found.
    const ForgedSpawn drowned = findForgedSpawn(flatWorld(5.0f), nullptr, {});
    CHECK_FALSE(drowned.found);
    CHECK(drowned.x == 512.0);

    const Heightfield flat = flatWorld(100.0f);
    const ForgedSpawn moved = findForgedSpawn(flat, nullptr, {}, [](double x, double z) { return std::abs(x - 512.0) < 20.0 && std::abs(z - 512.0) < 20.0; });
    REQUIRE(moved.found);
    CHECK(std::hypot(moved.x - 512.0, moved.z - 512.0) >= 20.0);
    CHECK(std::hypot(moved.x - 512.0, moved.z - 512.0) < 40.0);

    WorldDesc desc = flat.desc();
    std::vector<std::uint16_t> ramp(static_cast<std::size_t>(desc.samplesPerSide()) * desc.samplesPerSide());
    for (std::uint32_t z = 0; z < desc.samplesPerSide(); ++z) {
        for (std::uint32_t x = 0; x < desc.samplesPerSide(); ++x) {
            // 1:1 slope in the middle band, flat plateaus either side.
            const double ground = x < 100 ? 100.0 : x > 156 ? 100.0 + 56.0 * 4.0 : 100.0 + (x - 100) * 4.0;
            ramp[static_cast<std::size_t>(z) * desc.samplesPerSide() + x] = static_cast<std::uint16_t>(std::lround(ground / desc.heightRange * 65535.0));
        }
    }
    const Heightfield hill(desc, std::move(ramp));
    const ForgedSpawn onHill = findForgedSpawn(hill, nullptr, {{512.0, 512.0}});
    REQUIRE(onHill.found);
    CHECK(hill.normalAt(onHill.x, onHill.z).y >= ForgedSpawnParams{}.minUpY);
}

TEST_CASE("a spawn search over empty or bad input finds nothing and does not crash", "[forged_game]")
{
    CHECK_FALSE(findForgedSpawn(Heightfield{}, nullptr, {}).found);
    ForgedSpawnParams bad;
    bad.searchStepMetres = 0.0f;
    CHECK_FALSE(findForgedSpawn(flatWorld(100.0f), nullptr, {}, {}, bad).found);
    const double nan = std::nan("");
    CHECK(findForgedSpawn(flatWorld(100.0f), nullptr, {{nan, nan}}).found);
}

TEST_CASE("an empty forge result is refused with a message", "[forged_game]")
{
    const ForgeJobResult empty;
    const auto game = buildForgedGame(empty, descFor(1));
    CHECK_FALSE(game->ok());
}

TEST_CASE("the forged game carries every piece the app draws and walks", "[forged_game]")
{
    const auto game = gameFor(1);
    REQUIRE(game->biomes);
    REQUIRE_FALSE(game->biomes->empty());
    REQUIRE(game->water.hydrology);
    REQUIRE(game->waterField);
    REQUIRE(game->roads);
    REQUIRE(game->pois);
    REQUIRE(game->buildings);
    REQUIRE(game->navSurface);
    CHECK(game->tiers.ok());
    CHECK_FALSE(game->mapLayers.empty());
    CHECK_FALSE(game->pois->empty());
    CHECK_FALSE(game->roads->empty());
}

TEST_CASE("the spawn is dry, clear of buildings and next to the settlement nearest the centre", "[forged_game]")
{
    const auto game = gameFor(1);
    const ForgedSpawn& spawn = game->spawn;
    REQUIRE(spawn.found);
    CHECK_FALSE(game->waterField->at(spawn.x, spawn.z).wet());
    for (const PlacedBuilding& b : game->buildings->all()) {
        const bool inside = std::abs(spawn.x - b.x) < b.halfWidthM && std::abs(spawn.z - b.z) < b.halfDepthM;
        CHECK_FALSE(inside);
    }
    const double centre = game->desc.sizeMetres * 0.5;
    double nearest = 1e30;
    double townX = centre;
    double townZ = centre;
    for (const PlacedPoi& poi : game->pois->all()) {
        const double d = std::hypot(poi.x - centre, poi.z - centre);
        if (poi.kind == PlacedPoiKind::Settlement && d < nearest) {
            nearest = d;
            townX = poi.x;
            townZ = poi.z;
        }
    }
    CHECK(std::hypot(spawn.x - townX, spawn.z - townZ) < 400.0);
}

TEST_CASE("the scenery has a mesh for every placed point of interest, one for buildings and one per road cell", "[forged_game]")
{
    const auto game = gameFor(1);
    const ForgedScenery scenery = buildForgedScenery(*game);
    CHECK(scenery.poiMesh.size() == game->pois->all().size());
    for (const PlacedPoi& poi : game->pois->all()) {
        REQUIRE(scenery.poiMesh.contains(poi.id));
        CHECK(scenery.poiMesh.at(poi.id) < scenery.bundle.meshes.size());
    }
    CHECK(scenery.buildingMesh < scenery.bundle.meshes.size());
    CHECK_FALSE(scenery.roadMesh.empty());
    for (const Mesh& mesh : scenery.bundle.meshes) {
        REQUIRE_FALSE(mesh.vertices.empty());
        for (const Submesh& submesh : mesh.submeshes) {
            CHECK(submesh.material < scenery.bundle.materials.size());
        }
    }
}

TEST_CASE("a streamed cell of a forged world carries its biome splat, its placed objects and a forest of its biome", "[forged_game]")
{
    const auto game = gameFor(1);
    StreamerConfig forged;
    forged.biomes = game->biomes;
    forged.pois = game->pois;
    forged.buildings = game->buildings;
    forged.vegetation = true;
    forged.lodCount = 1;

    double grassLow = 1e9;
    double grassHigh = -1e9;
    std::size_t placed = 0;
    for (std::uint32_t z = 0; z < game->desc.cellsPerSide(); ++z) {
        for (std::uint32_t x = 0; x < game->desc.cellsPerSide(); ++x) {
            CellPayload payload;
            WorldStreamer::buildPayload(game->heightfield, {static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)}, forged, payload);
            placed += payload.pois.size();
            for (const Vertex& v : payload.lods[0].vertices) {
                REQUIRE(v.tangent.w == -1.0f);
                const double sum = v.tangent.x + v.tangent.y + v.tangent.z;
                REQUIRE(std::abs(sum - 1.0) < 1e-4);
                grassLow = std::min<double>(grassLow, v.tangent.x);
                grassHigh = std::max<double>(grassHigh, v.tangent.x);
            }
        }
    }
    CHECK(placed == game->pois->all().size());
    // The biomes differ across the world, so the grass weight is not one number everywhere.
    CHECK(grassHigh - grassLow > 0.2);

    CellPayload standIn;
    WorldStreamer::buildPayload(game->heightfield, {2, 2}, StreamerConfig{.lodCount = 1}, standIn);
    for (const Vertex& v : standIn.lods[0].vertices) {
        REQUIRE(v.tangent.w == 1.0f);
    }
    CHECK(standIn.pois.empty());
}

TEST_CASE("every level of detail of a forged cell keeps its splat weights", "[forged_game]")
{
    const auto game = gameFor(1);
    StreamerConfig config;
    config.biomes = game->biomes;
    config.navmesh = true;
    config.navSurface = game->navSurface;
    CellPayload payload;
    WorldStreamer::buildPayload(game->heightfield, {3, 3}, config, payload);
    REQUIRE(payload.lods.size() == 3);
    for (const Mesh& lod : payload.lods) {
        REQUIRE_FALSE(lod.vertices.empty());
        for (const Vertex& v : lod.vertices) {
            REQUIRE(v.tangent.w == -1.0f);
            REQUIRE(std::abs(v.tangent.x + v.tangent.y + v.tangent.z - 1.0f) < 1e-4f);
        }
    }
}

TEST_CASE("a navmesh built over a forged cell leaves the water out", "[forged_game]")
{
    const auto game = gameFor(1);
    StreamerConfig walled;
    walled.navmesh = true;
    walled.navSurface = game->navSurface;
    StreamerConfig open;
    open.navmesh = true;
    std::size_t walledBytes = 0;
    std::size_t openBytes = 0;
    bool wetCell = false;
    for (std::uint32_t z = 0; z < game->desc.cellsPerSide(); ++z) {
        for (std::uint32_t x = 0; x < game->desc.cellsPerSide(); ++x) {
            const CellId id{static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)};
            const double cx = (x + 0.5) * game->desc.cellMetres;
            const double cz = (z + 0.5) * game->desc.cellMetres;
            if (!game->waterField->at(cx, cz).wet()) {
                continue;
            }
            wetCell = true;
            CellPayload a;
            CellPayload b;
            WorldStreamer::buildPayload(game->heightfield, id, walled, a);
            WorldStreamer::buildPayload(game->heightfield, id, open, b);
            walledBytes += a.nav.data.size();
            openBytes += b.nav.data.size();
        }
    }
    if (wetCell) {
        CHECK(walledBytes < openBytes);
    }
}
