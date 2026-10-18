#include "ichor/world/forged_heightfield.h"
#include "ichor/world/placed_pois.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <limits>
#include <map>
#include <set>

using namespace ichor;

namespace {

constexpr int kGrid = 128;

WorldDesc worldDesc(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    return forgedWorldDesc(desc);
}

struct Fixture {
    worldforge::ForgedWorld world;
    Heightfield field;
    PlacedPois placed;
};

Fixture build(std::uint64_t seed)
{
    const WorldDesc desc = worldDesc(seed);
    worldforge::ForgeConfig config;
    config.dungeonArchetypes = {"crypt", "cave", "mine"};
    worldforge::ForgedWorld world = worldforge::forgeWorld(seed, kGrid, config);
    ForgedHeightfield adapted = heightfieldFromForgedWorld(world, desc);
    REQUIRE(adapted.ok());
    PlacedPois placed = placeForgedPois(world, adapted.field);
    return {std::move(world), std::move(adapted.field), std::move(placed)};
}

const Fixture& fixture()
{
    static const Fixture f = [] {
        for (std::uint64_t seed = 1; seed < 40; ++seed) {
            Fixture candidate = build(seed);
            if (candidate.placed.ok() && !candidate.placed.set.all().empty() && candidate.world.manifest() && !candidate.world.manifest()->dungeons.empty()) return candidate;
        }
        FAIL("no seed under 40 forged a world with points of interest and a dungeon");
        return build(1);
    }();
    return f;
}

}

TEST_CASE("every manifest id is placed exactly once", "[bake][pois]")
{
    const Fixture& f = fixture();
    REQUIRE(f.placed.ok());
    const auto& ids = f.world.manifest()->poiIds;
    REQUIRE_FALSE(ids.empty());
    REQUIRE(f.placed.set.all().size() == ids.size());
    for (const std::string& id : ids) {
        REQUIRE(f.placed.set.find(id) != nullptr);
    }
    std::set<std::string> seen;
    for (const PlacedPoi& poi : f.placed.set.all()) {
        REQUIRE(seen.insert(poi.id).second);
    }
}

TEST_CASE("a cell holds exactly the manifest points of interest inside it, once each", "[bake][pois]")
{
    const Fixture& f = fixture();
    const WorldDesc& desc = f.field.desc();
    std::map<std::string, int> counted;
    for (std::int32_t z = 0; z < static_cast<std::int32_t>(desc.cellsPerSide()); ++z) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(desc.cellsPerSide()); ++x) {
            for (const PlacedPoi& poi : f.placed.set.inCell({x, z})) {
                ++counted[poi.id];
                REQUIRE(poi.cell == CellId{x, z});
                REQUIRE(poi.x >= f.field.cellOriginX({x, z}));
                REQUIRE(poi.x <= f.field.cellOriginX({x, z}) + desc.cellMetres);
                REQUIRE(poi.z >= f.field.cellOriginZ({x, z}));
                REQUIRE(poi.z <= f.field.cellOriginZ({x, z}) + desc.cellMetres);
            }
        }
    }
    for (const std::string& id : f.world.manifest()->poiIds) {
        REQUIRE(counted[id] == 1);
    }
    REQUIRE(f.placed.set.inCell({-1, 0}).empty());
    REQUIRE(f.placed.set.inCell({1000, 1000}).empty());
}

TEST_CASE("positions follow the forged grid and stand on the terrain", "[bake][pois]")
{
    const Fixture& f = fixture();
    const auto& map = *f.world.pois();
    for (std::size_t i = 0; i < map.pois.size(); ++i) {
        char id[16];
        std::snprintf(id, sizeof id, "poi_%04zu", i);
        const PlacedPoi* poi = f.placed.set.find(id);
        REQUIRE(poi != nullptr);
        REQUIRE(poi->x == Catch::Approx(map.pois[i].x * 2048.0 / (kGrid - 1)));
        REQUIRE(poi->z == Catch::Approx(map.pois[i].y * 2048.0 / (kGrid - 1)));
        REQUIRE(poi->groundY == f.field.heightAt(poi->x, poi->z));
        REQUIRE(poi->silhouetteM > 0.0f);
    }
}

TEST_CASE("kinds follow the manifest", "[bake][pois]")
{
    const Fixture& f = fixture();
    std::size_t dungeons = 0;
    for (const worldforge::DungeonSlot& slot : f.world.manifest()->dungeons) {
        const PlacedPoi* poi = f.placed.set.find(slot.poiId);
        REQUIRE(poi != nullptr);
        REQUIRE(poi->kind == PlacedPoiKind::Dungeon);
        REQUIRE(poi->archetype == slot.archetype);
        ++dungeons;
    }
    REQUIRE(dungeons > 0);
    for (const PlacedPoi& poi : f.placed.set.all()) {
        if (poi.id.starts_with("settlement_")) REQUIRE(poi.kind == PlacedPoiKind::Settlement);
        if (poi.kind != PlacedPoiKind::Dungeon) REQUIRE(poi.archetype.empty());
    }
}

TEST_CASE("the placement does not depend on the order cells are asked for", "[bake][pois]")
{
    const Fixture& f = fixture();
    const auto cells = static_cast<std::int32_t>(f.field.desc().cellsPerSide());
    std::vector<std::string> forward;
    std::vector<std::string> backward;
    for (std::int32_t i = 0; i < cells * cells; ++i) {
        for (const PlacedPoi& p : f.placed.set.inCell({i % cells, i / cells})) forward.push_back(p.id);
    }
    for (std::int32_t i = cells * cells - 1; i >= 0; --i) {
        for (const PlacedPoi& p : f.placed.set.inCell({i % cells, i / cells})) backward.push_back(p.id);
    }
    std::sort(forward.begin(), forward.end());
    std::sort(backward.begin(), backward.end());
    REQUIRE(forward == backward);
    const Fixture again = build(f.world.seed());
    REQUIRE(again.placed.set.all().size() == f.placed.set.all().size());
    for (std::size_t i = 0; i < again.placed.set.all().size(); ++i) {
        REQUIRE(again.placed.set.all()[i].id == f.placed.set.all()[i].id);
        REQUIRE(again.placed.set.all()[i].x == f.placed.set.all()[i].x);
        REQUIRE(again.placed.set.all()[i].z == f.placed.set.all()[i].z);
    }
}

TEST_CASE("a streamed cell carries its points of interest, and none without a set", "[bake][pois]")
{
    const Fixture& f = fixture();
    StreamerConfig config;
    config.synchronous = true;
    config.lodCount = 1;
    config.pois = std::make_shared<const PlacedPoiSet>(f.placed.set);

    std::size_t total = 0;
    const auto cells = static_cast<std::int32_t>(f.field.desc().cellsPerSide());
    for (std::int32_t z = 0; z < cells; ++z) {
        for (std::int32_t x = 0; x < cells; ++x) {
            CellPayload payload;
            WorldStreamer::buildPayload(f.field, {x, z}, config, payload);
            REQUIRE(payload.pois.size() == f.placed.set.inCell({x, z}).size());
            total += payload.pois.size();
        }
    }
    REQUIRE(total == f.world.manifest()->poiIds.size());

    config.pois.reset();
    CellPayload bare;
    WorldStreamer::buildPayload(f.field, f.placed.set.all().front().cell, config, bare);
    REQUIRE(bare.pois.empty());
}

TEST_CASE("a world that cannot be placed says why", "[bake][pois]")
{
    const Fixture& f = fixture();
    const worldforge::ForgedWorld early = worldforge::forgeWorld(f.world.seed(), kGrid, worldforge::ForgeConfig{}, worldforge::ForgePhase::Hydrology);
    REQUIRE_FALSE(placeForgedPois(early, f.field).ok());
    REQUIRE_FALSE(placeForgedPois(f.world, Heightfield{}).ok());
    PlacedPoiParams bad;
    bad.landmarkHeightM = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(placeForgedPois(f.world, f.field, bad).ok());
    bad.landmarkHeightM = -1.0f;
    REQUIRE_FALSE(placeForgedPois(f.world, f.field, bad).ok());
}

TEST_CASE("the placeholder marker is a closed pillar of the asked height", "[bake][pois]")
{
    for (const PlacedPoiKind kind : {PlacedPoiKind::Settlement, PlacedPoiKind::Dungeon, PlacedPoiKind::Landmark}) {
        const Mesh mesh = placedPoiMarkerMesh(kind, 20.0f);
        REQUIRE(mesh.vertices.size() == 16);
        REQUIRE(mesh.indices.size() == 24);
        float top = 0.0f;
        for (const Vertex& v : mesh.vertices) top = std::max(top, v.position.y);
        REQUIRE(top == 20.0f);
        for (const std::uint32_t i : mesh.indices) REQUIRE(i < mesh.vertices.size());
        const Mesh nan = placedPoiMarkerMesh(kind, std::numeric_limits<float>::quiet_NaN());
        for (const Vertex& v : nan.vertices) REQUIRE(std::isfinite(v.position.y));
    }
}
