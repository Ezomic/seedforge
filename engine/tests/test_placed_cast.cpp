#include "ichor/world/forged_heightfield.h"
#include "ichor/world/placed_cast.h"
#include "ichor/world/placed_pois.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>

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
    PlacedPois pois;
    PlacedCast cast;
};

Fixture build(std::uint64_t seed)
{
    worldforge::ForgeConfig config;
    config.dungeonArchetypes = {"crypt", "cave", "mine"};
    config.npcArchetypes = {"smith", "priest", "merchant", "ferryman", "healer", "guard"};
    worldforge::ForgedWorld world = worldforge::forgeWorld(seed, kGrid, config);
    ForgedHeightfield adapted = heightfieldFromForgedWorld(world, worldDesc(seed));
    REQUIRE(adapted.ok());
    PlacedPois pois = placeForgedPois(world, adapted.field);
    PlacedCast cast = placeForgedCast(world, pois.set, adapted.field);
    return {std::move(world), std::move(adapted.field), std::move(pois), std::move(cast)};
}

const Fixture& fixture()
{
    static const Fixture f = [] {
        for (std::uint64_t seed = 1; seed < 40; ++seed) {
            Fixture candidate = build(seed);
            if (candidate.pois.ok() && candidate.cast.ok() && candidate.world.manifest() && candidate.world.manifest()->cast.size() >= 12) return candidate;
        }
        FAIL("no seed under 40 forged a world with settlements and a cast");
        return build(1);
    }();
    return f;
}

const worldforge::ContentManifest& manifest() { return *fixture().world.manifest(); }

}

TEST_CASE("every cast member becomes one spawn with a valid home", "[bake][cast]")
{
    const Fixture& f = fixture();
    REQUIRE(f.cast.set.all().size() == manifest().cast.size());
    std::set<std::string> seen;
    for (const NpcSpawn& spawn : f.cast.set.all()) {
        REQUIRE(seen.insert(spawn.npcId).second);
        const auto member = std::find_if(manifest().cast.begin(), manifest().cast.end(), [&](const worldforge::CastMember& c) { return c.npcId == spawn.npcId; });
        REQUIRE(member != manifest().cast.end());
        REQUIRE(spawn.archetype == member->archetype);
        REQUIRE(spawn.homePoiId == member->homePoiId);
        const PlacedPoi* home = f.pois.set.find(spawn.homePoiId);
        REQUIRE(home != nullptr);
        REQUIRE(home->kind == PlacedPoiKind::Settlement);
    }
    REQUIRE(std::is_sorted(f.cast.set.all().begin(), f.cast.set.all().end(), [](const NpcSpawn& a, const NpcSpawn& b) { return a.npcId < b.npcId; }));
}

TEST_CASE("the same manifest gives the same cast, whatever the cast order", "[bake][cast]")
{
    const Fixture& f = fixture();
    const PlacedCast again = placeForgedCast(f.world, f.pois.set, f.field);
    REQUIRE(again.ok());
    worldforge::ContentManifest shuffled = manifest();
    std::reverse(shuffled.cast.begin(), shuffled.cast.end());
    const PlacedCast reversed = placeCast(shuffled, f.pois.set, f.field);
    REQUIRE(reversed.ok());
    REQUIRE(again.set.all().size() == f.cast.set.all().size());
    REQUIRE(reversed.set.all().size() == f.cast.set.all().size());
    for (std::size_t i = 0; i < f.cast.set.all().size(); ++i) {
        const NpcSpawn& a = f.cast.set.all()[i];
        for (const PlacedCast* other : {&again, &reversed}) {
            const NpcSpawn& b = other->set.all()[i];
            REQUIRE(a.npcId == b.npcId);
            REQUIRE(a.x == b.x);
            REQUIRE(a.z == b.z);
            REQUIRE(a.cell == b.cell);
            REQUIRE(canonicalForm(a.schedule) == canonicalForm(b.schedule));
            REQUIRE(canonicalForm(a.relationships) == canonicalForm(b.relationships));
        }
    }
}

TEST_CASE("a different world seed moves the cast", "[bake][cast]")
{
    const Fixture& f = fixture();
    worldforge::ContentManifest other = manifest();
    other.worldSeed ^= 0x5eed5eedull;
    const PlacedCast moved = placeCast(other, f.pois.set, f.field);
    REQUIRE(moved.ok());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < moved.set.all().size(); ++i) {
        if (moved.set.all()[i].x != f.cast.set.all()[i].x) ++differing;
    }
    REQUIRE(differing > moved.set.all().size() / 2);
}

TEST_CASE("spawns stand near their home, inside the world, on the terrain", "[bake][cast]")
{
    const Fixture& f = fixture();
    const double extent = f.field.desc().sizeMetres;
    const PlacedCastParams params;
    for (const NpcSpawn& spawn : f.cast.set.all()) {
        const PlacedPoi* home = f.pois.set.find(spawn.homePoiId);
        REQUIRE(std::abs(spawn.x - home->x) <= params.spawnRadiusM + 1e-9);
        REQUIRE(std::abs(spawn.z - home->z) <= params.spawnRadiusM + 1e-9);
        REQUIRE(spawn.x >= 0.0);
        REQUIRE(spawn.z >= 0.0);
        REQUIRE(spawn.x <= extent);
        REQUIRE(spawn.z <= extent);
        REQUIRE(spawn.groundY == f.field.heightAt(spawn.x, spawn.z));
    }
}

TEST_CASE("a zero radius puts everyone on their settlement, a larger one spreads them", "[bake][cast]")
{
    const Fixture& f = fixture();
    PlacedCastParams none;
    none.spawnRadiusM = 0.0f;
    const PlacedCast stacked = placeCast(manifest(), f.pois.set, f.field, none);
    REQUIRE(stacked.ok());
    for (const NpcSpawn& spawn : stacked.set.all()) {
        const PlacedPoi* home = f.pois.set.find(spawn.homePoiId);
        REQUIRE(spawn.x == std::clamp(home->x, 0.0, static_cast<double>(f.field.desc().sizeMetres)));
    }
    std::set<double> distinct;
    for (const NpcSpawn& spawn : f.cast.set.all()) distinct.insert(spawn.x);
    REQUIRE(distinct.size() > f.cast.set.all().size() / 2);
}

TEST_CASE("each cell lists exactly the spawns standing in it, once", "[bake][cast]")
{
    const Fixture& f = fixture();
    const WorldDesc& desc = f.field.desc();
    std::map<std::string, int> counted;
    for (std::int32_t z = 0; z < static_cast<std::int32_t>(desc.cellsPerSide()); ++z) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(desc.cellsPerSide()); ++x) {
            for (const NpcSpawn& spawn : f.cast.set.inCell({x, z})) {
                ++counted[spawn.npcId];
                REQUIRE(spawn.cell == CellId{x, z});
                REQUIRE(spawn.x >= f.field.cellOriginX({x, z}));
                REQUIRE(spawn.x <= f.field.cellOriginX({x, z}) + desc.cellMetres);
            }
        }
    }
    for (const worldforge::CastMember& member : manifest().cast) REQUIRE(counted[member.npcId] == 1);
    REQUIRE(f.cast.set.inCell({-1, 0}).empty());
    REQUIRE(f.cast.set.inCell({1000, 1000}).empty());
}

TEST_CASE("every schedule is legal and answers at every hour", "[bake][cast][schedule]")
{
    const Fixture& f = fixture();
    std::set<std::string> forms;
    for (const NpcSpawn& spawn : f.cast.set.all()) {
        REQUIRE_FALSE(spawn.schedule.entries.empty());
        for (std::size_t i = 1; i < spawn.schedule.entries.size(); ++i) {
            REQUIRE(spawn.schedule.entries[i].startMinute > spawn.schedule.entries[i - 1].startMinute);
        }
        REQUIRE(spawn.schedule.entries.front().startMinute >= 0);
        REQUIRE(spawn.schedule.entries.back().startMinute < kMinutesPerDay);
        for (int minute = 0; minute < kMinutesPerDay; minute += 30) REQUIRE(stateAtMinute(spawn.schedule, minute).has_value());
        forms.insert(canonicalForm(spawn.schedule));
    }
    REQUIRE(forms.size() > 1);
}

TEST_CASE("an archetype schedule overrides the default templates", "[bake][cast][schedule]")
{
    const Fixture& f = fixture();
    PlacedCastParams params;
    params.archetypeSchedules.push_back({"smith", {"04:00 work", "20:00 sleep"}});
    const PlacedCast cast = placeCast(manifest(), f.pois.set, f.field, params);
    REQUIRE(cast.ok());
    std::size_t smiths = 0;
    for (const NpcSpawn& spawn : cast.set.all()) {
        if (spawn.archetype == "smith") {
            ++smiths;
            REQUIRE(spawn.schedule.entries.size() == 2);
            REQUIRE(spawn.schedule.entries[0].startMinute == 240);
        } else {
            REQUIRE(spawn.schedule.entries.size() > 2);
        }
    }
    REQUIRE(smiths > 0);
}

TEST_CASE("relationships follow the generator and each spawn carries its own edges", "[bake][cast][relationships]")
{
    const Fixture& f = fixture();
    std::vector<RelationshipNpc> people;
    for (const worldforge::CastMember& member : manifest().cast) people.push_back({member.npcId, NpcTier::Regular, member.homePoiId});
    const std::vector<RelationshipEdge> expected = generateRelationships(manifest().worldSeed, people);
    REQUIRE_FALSE(expected.empty());
    std::size_t carried = 0;
    for (const NpcSpawn& spawn : f.cast.set.all()) {
        REQUIRE(spawn.relationships.size() >= kMinRelationships);
        REQUIRE(spawn.relationships.size() <= kMaxRelationships);
        for (const RelationshipEdge& edge : spawn.relationships) {
            REQUIRE((edge.a == spawn.npcId || edge.b == spawn.npcId));
            REQUIRE(f.cast.set.find(edge.a == spawn.npcId ? edge.b : edge.a) != nullptr);
        }
        carried += spawn.relationships.size();
    }
    REQUIRE(carried == expected.size() * 2);
}

TEST_CASE("a cast beyond the relationship cap still spawns, the overflow without edges", "[bake][cast][relationships]")
{
    const Fixture& f = fixture();
    worldforge::ContentManifest big = manifest();
    const worldforge::CastMember seedMember = big.cast.front();
    big.cast.clear();
    for (std::size_t i = 0; i < kMaxRelatedCast + 40; ++i) {
        worldforge::CastMember member = seedMember;
        member.npcId = "extra#" + std::to_string(100000 + i);
        big.cast.push_back(member);
    }
    const PlacedCast cast = placeCast(big, f.pois.set, f.field);
    REQUIRE(cast.ok());
    REQUIRE(cast.set.all().size() == big.cast.size());
    REQUIRE_FALSE(cast.set.all().front().relationships.empty());
    REQUIRE(cast.set.all().back().relationships.empty());
}

TEST_CASE("find and ofSettlement agree with the set", "[bake][cast]")
{
    const Fixture& f = fixture();
    const NpcSpawn& first = f.cast.set.all().front();
    REQUIRE(f.cast.set.find(first.npcId) != nullptr);
    REQUIRE(f.cast.set.find("no-such-npc") == nullptr);
    std::size_t total = 0;
    for (const PlacedPoi& poi : f.pois.set.all()) total += f.cast.set.ofSettlement(poi.id).size();
    REQUIRE(total == f.cast.set.all().size());
}

TEST_CASE("hostile manifests fail with a message and never half-place", "[bake][cast][hostile]")
{
    const Fixture& f = fixture();

    worldforge::ContentManifest unknownHome = manifest();
    unknownHome.cast.front().homePoiId = "settlement_9999";
    CHECK_FALSE(placeCast(unknownHome, f.pois.set, f.field).ok());

    worldforge::ContentManifest dungeonHome = manifest();
    REQUIRE_FALSE(dungeonHome.dungeons.empty());
    dungeonHome.cast.front().homePoiId = dungeonHome.dungeons.front().poiId;
    CHECK_FALSE(placeCast(dungeonHome, f.pois.set, f.field).ok());

    worldforge::ContentManifest duplicate = manifest();
    duplicate.cast.push_back(duplicate.cast.front());
    CHECK_FALSE(placeCast(duplicate, f.pois.set, f.field).ok());

    worldforge::ContentManifest blank = manifest();
    blank.cast.front().npcId.clear();
    CHECK_FALSE(placeCast(blank, f.pois.set, f.field).ok());

    worldforge::ContentManifest huge = manifest();
    huge.cast.resize(kMaxCastSpawns + 1, huge.cast.front());
    CHECK_FALSE(placeCast(huge, f.pois.set, f.field).ok());

    CHECK_FALSE(placeCast(manifest(), f.pois.set, Heightfield{}).ok());

    worldforge::ContentManifest empty = manifest();
    empty.cast.clear();
    const PlacedCast none = placeCast(empty, f.pois.set, f.field);
    CHECK(none.ok());
    CHECK(none.set.empty());
}

TEST_CASE("bad params and schedules are rejected", "[bake][cast][hostile]")
{
    const Fixture& f = fixture();
    for (const float radius : {-1.0f, kMaxSpawnRadiusM + 1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        PlacedCastParams params;
        params.spawnRadiusM = radius;
        CHECK_FALSE(placeCast(manifest(), f.pois.set, f.field, params).ok());
    }
    PlacedCastParams noTemplates;
    noTemplates.defaultSchedules.clear();
    CHECK_FALSE(placeCast(manifest(), f.pois.set, f.field, noTemplates).ok());

    PlacedCastParams badDefault;
    badDefault.defaultSchedules = {{"25:00 work"}};
    CHECK_FALSE(placeCast(manifest(), f.pois.set, f.field, badDefault).ok());

    PlacedCastParams unordered;
    unordered.archetypeSchedules.push_back({"smith", {"10:00 work", "09:00 home"}});
    CHECK_FALSE(placeCast(manifest(), f.pois.set, f.field, unordered).ok());

    PlacedCastParams twice;
    twice.archetypeSchedules.push_back({"smith", {"06:00 work"}});
    twice.archetypeSchedules.push_back({"smith", {"07:00 work"}});
    CHECK_FALSE(placeCast(manifest(), f.pois.set, f.field, twice).ok());
}

TEST_CASE("a world that stopped before the content phase has no cast", "[bake][cast]")
{
    const Fixture& f = fixture();
    const worldforge::ForgedWorld early = worldforge::forgeWorld(f.world.seed(), kGrid, worldforge::ForgeConfig{}, worldforge::ForgePhase::Hydrology);
    const PlacedCast cast = placeForgedCast(early, f.pois.set, f.field);
    CHECK_FALSE(cast.ok());
    CHECK(cast.set.empty());
}
