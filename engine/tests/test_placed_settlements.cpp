#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_roads.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/placed_settlements.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>

using namespace ichor;

namespace {

constexpr std::uint8_t kHamlet = 0;
constexpr std::uint8_t kVillage = 1;
constexpr std::uint8_t kTown = 2;
constexpr std::uint8_t kCity = 3;

WorldDesc smallDesc()
{
    WorldDesc desc;
    desc.seed = 7;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    desc.heightRange = 400.0f;
    return desc;
}

// Ground at `base(x, z)` metres.
template <typename F>
Heightfield shaped(F base)
{
    const WorldDesc desc = smallDesc();
    const std::uint32_t n = desc.samplesPerSide();
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const double metres = base(static_cast<double>(x * desc.metresPerSample), static_cast<double>(z * desc.metresPerSample));
            samples[static_cast<std::size_t>(z) * n + x] = static_cast<std::uint16_t>(std::clamp(metres / desc.heightRange * 65535.0, 0.0, 65535.0));
        }
    }
    return Heightfield(desc, std::move(samples));
}

Heightfield flatField()
{
    return shaped([](double, double) { return 100.0; });
}

class StripRoad final : public RoadField {
public:
    StripRoad(double x0, double x1) : x0_(x0), x1_(x1) {}
    float coverageAt(double x, double) const override { return x >= x0_ && x <= x1_ ? 1.0f : 0.0f; }

private:
    double x0_;
    double x1_;
};

std::size_t countOf(const PlacedSettlements& placed, const std::string& id)
{
    return placed.set.ofSettlement(id).size();
}

}

TEST_CASE("a hamlet and a city get different building counts", "[bake][settlements]")
{
    const Heightfield field = flatField();
    const std::vector<SettlementSite> sites = {
        {"settlement_0000", kHamlet, 400.0, 400.0},
        {"settlement_0001", kVillage, 1000.0, 400.0},
        {"settlement_0002", kTown, 400.0, 1200.0},
        {"settlement_0003", kCity, 1200.0, 1200.0},
    };
    const PlacedSettlements placed = placeSettlementBuildings(sites, field, nullptr, nullptr);
    REQUIRE(placed.ok());
    const std::size_t hamlet = countOf(placed, "settlement_0000");
    const std::size_t village = countOf(placed, "settlement_0001");
    const std::size_t town = countOf(placed, "settlement_0002");
    const std::size_t city = countOf(placed, "settlement_0003");
    REQUIRE(hamlet >= 1);
    REQUIRE(hamlet < village);
    REQUIRE(village < town);
    REQUIRE(town < city);
    REQUIRE(city >= 4 * hamlet);

    float hamletSize = 0.0f;
    float citySize = 0.0f;
    for (const PlacedBuilding& b : placed.set.ofSettlement("settlement_0000")) hamletSize = std::max(hamletSize, b.halfWidthM * b.halfDepthM);
    for (const PlacedBuilding& b : placed.set.ofSettlement("settlement_0003")) citySize = std::max(citySize, b.halfWidthM * b.halfDepthM);
    REQUIRE(citySize > hamletSize);
}

TEST_CASE("buildings stay on the flat ground they were checked on", "[bake][settlements]")
{
    const Heightfield flat = flatField();
    const Heightfield ramp = shaped([](double x, double) { return 20.0 + std::clamp(x - 800.0, 0.0, 400.0) * 0.6; });
    const std::vector<SettlementSite> sites = {{"settlement_0000", kCity, 1000.0, 1000.0}};
    const std::size_t onFlat = placeSettlementBuildings(sites, flat, nullptr, nullptr).set.all().size();
    const PlacedSettlements onRamp = placeSettlementBuildings(sites, ramp, nullptr, nullptr);
    REQUIRE(onRamp.ok());
    REQUIRE(onFlat > 10);
    REQUIRE(onRamp.set.all().empty());

    // Flat terraces on a staircase: a building may stand on a tread, never across a step.
    const Heightfield steps = shaped([](double x, double) { return 20.0 + std::floor(x / 12.0) * 6.0; });
    const PlacedSettlements stepped = placeSettlementBuildings(sites, steps, nullptr, nullptr);
    REQUIRE(stepped.ok());
    REQUIRE_FALSE(stepped.set.all().empty());
    for (const PlacedBuilding& b : stepped.set.all()) {
        const float centre = steps.heightAt(b.x, b.z);
        for (const double fx : {-1.0, 0.0, 1.0}) {
            for (const double fz : {-1.0, 0.0, 1.0}) {
                REQUIRE(std::abs(steps.heightAt(b.x + fx * b.halfWidthM, b.z + fz * b.halfDepthM) - centre) <= 1.2f);
            }
        }
        REQUIRE(b.groundY <= centre);
    }
}

TEST_CASE("buildings keep clear of roads", "[bake][settlements]")
{
    const Heightfield field = flatField();
    const std::vector<SettlementSite> sites = {{"settlement_0000", kCity, 1000.0, 1000.0}};
    const StripRoad road(980.0, 1020.0);
    const PlacedSettlements free = placeSettlementBuildings(sites, field, nullptr, nullptr);
    const PlacedSettlements avoided = placeSettlementBuildings(sites, field, &road, nullptr);
    REQUIRE(avoided.ok());
    REQUIRE_FALSE(avoided.set.all().empty());
    bool freeCrossesRoad = false;
    for (const PlacedBuilding& b : free.set.all()) {
        freeCrossesRoad = freeCrossesRoad || (b.x + b.halfWidthM >= 980.0 && b.x - b.halfWidthM <= 1020.0);
    }
    REQUIRE(freeCrossesRoad);
    for (const PlacedBuilding& b : avoided.set.all()) {
        REQUIRE((b.x + b.halfWidthM < 980.0 || b.x - b.halfWidthM > 1020.0));
    }

    // A road 3 m wide would slip between sparse samples; no building may have it under its footprint.
    const StripRoad thin(1000.0, 1003.0);
    const PlacedSettlements thinAvoided = placeSettlementBuildings(sites, field, &thin, nullptr);
    REQUIRE_FALSE(thinAvoided.set.all().empty());
    for (const PlacedBuilding& b : thinAvoided.set.all()) {
        REQUIRE((b.x + b.halfWidthM < 1000.0 || b.x - b.halfWidthM > 1003.0));
    }

    const UniformRoadField everywhere(1.0f);
    REQUIRE(placeSettlementBuildings(sites, field, &everywhere, nullptr).set.all().empty());
}

TEST_CASE("buildings keep out of water", "[bake][settlements]")
{
    constexpr double kRim = 400.0;
    const Heightfield bowl = shaped([](double x, double z) {
        const double r = std::sqrt((x - 1000.0) * (x - 1000.0) + (z - 1000.0) * (z - 1000.0));
        return r < kRim ? 100.0 + 20.0 * (r / kRim) : 120.0;
    });
    const auto hydrology = std::make_shared<const Hydrology>(computeHydrology(bowl));
    HydrologyParams rivers;
    rivers.riverThreshold = 1u << 30;
    const WaterField water(bowl, hydrology, rivers);
    REQUIRE(water.at(1000.0, 1000.0).wet());

    const std::vector<SettlementSite> sites = {{"settlement_0000", kCity, 1000.0, 1000.0}, {"settlement_0001", kCity, 1700.0, 1700.0}};
    const PlacedSettlements dry = placeSettlementBuildings(sites, bowl, nullptr, &water);
    const PlacedSettlements wet = placeSettlementBuildings(sites, bowl, nullptr, nullptr);
    REQUIRE(dry.ok());
    REQUIRE(countOf(wet, "settlement_0000") > 0);
    REQUIRE(countOf(dry, "settlement_0000") < countOf(wet, "settlement_0000"));
    REQUIRE(countOf(dry, "settlement_0001") == countOf(wet, "settlement_0001"));
    for (const PlacedBuilding& b : dry.set.all()) {
        for (const double fx : {-1.0, 0.0, 1.0}) {
            for (const double fz : {-1.0, 0.0, 1.0}) {
                REQUIRE_FALSE(water.at(b.x + fx * b.halfWidthM, b.z + fz * b.halfDepthM).wet());
            }
        }
    }
}

TEST_CASE("the world edge is kept clear", "[bake][settlements]")
{
    const Heightfield field = flatField();
    const std::vector<SettlementSite> sites = {{"settlement_0000", kCity, 0.0, 0.0}, {"settlement_0001", kCity, 2048.0, 2048.0}};
    const PlacedSettlements placed = placeSettlementBuildings(sites, field, nullptr, nullptr);
    REQUIRE(placed.ok());
    REQUIRE_FALSE(placed.set.all().empty());
    for (const PlacedBuilding& b : placed.set.all()) {
        REQUIRE(b.x - b.halfWidthM >= 0.0);
        REQUIRE(b.z - b.halfDepthM >= 0.0);
        REQUIRE(b.x + b.halfWidthM <= 2048.0);
        REQUIRE(b.z + b.halfDepthM <= 2048.0);
    }
}

TEST_CASE("buildings of a settlement never overlap", "[bake][settlements]")
{
    const Heightfield field = flatField();
    const PlacedSettlements placed = placeSettlementBuildings({{"settlement_0000", kCity, 1000.0, 1000.0}}, field, nullptr, nullptr);
    const auto& all = placed.set.all();
    REQUIRE(all.size() > 10);
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            const bool apart = std::abs(all[i].x - all[j].x) >= all[i].halfWidthM + all[j].halfWidthM || std::abs(all[i].z - all[j].z) >= all[i].halfDepthM + all[j].halfDepthM;
            REQUIRE(apart);
        }
    }
}

TEST_CASE("the placement does not depend on site order or the order cells are asked for", "[bake][settlements]")
{
    const Heightfield field = flatField();
    std::vector<SettlementSite> sites = {
        {"settlement_0000", kVillage, 300.0, 300.0},
        {"settlement_0001", kCity, 1000.0, 600.0},
        {"settlement_0002", kTown, 1600.0, 1500.0},
        {"settlement_0003", kHamlet, 500.0, 1700.0},
    };
    const PlacedSettlements forward = placeSettlementBuildings(sites, field, nullptr, nullptr);
    std::reverse(sites.begin(), sites.end());
    const PlacedSettlements backward = placeSettlementBuildings(sites, field, nullptr, nullptr);
    REQUIRE(forward.set.all().size() == backward.set.all().size());
    for (std::size_t i = 0; i < forward.set.all().size(); ++i) {
        REQUIRE(forward.set.all()[i].id == backward.set.all()[i].id);
        REQUIRE(forward.set.all()[i].x == backward.set.all()[i].x);
        REQUIRE(forward.set.all()[i].z == backward.set.all()[i].z);
        REQUIRE(forward.set.all()[i].heightM == backward.set.all()[i].heightM);
    }
    REQUIRE(std::is_sorted(forward.set.all().begin(), forward.set.all().end(), [](const PlacedBuilding& a, const PlacedBuilding& b) { return a.id < b.id; }));

    std::map<std::string, int> counted;
    const auto cells = static_cast<std::int32_t>(field.desc().cellsPerSide());
    for (std::int32_t i = cells * cells - 1; i >= 0; --i) {
        const CellId cell{i % cells, i / cells};
        for (const PlacedBuilding& b : forward.set.inCell(cell)) {
            ++counted[b.id];
            REQUIRE(b.cell == cell);
            REQUIRE(field.cellAt(b.x, b.z) == cell);
        }
    }
    REQUIRE(counted.size() == forward.set.all().size());
    for (const auto& [id, n] : counted) REQUIRE(n == 1);
    REQUIRE(forward.set.inCell({-1, 0}).empty());
    REQUIRE(forward.set.inCell({1000, 1000}).empty());

    sites.push_back({"settlement_0009", kCity, 1000.0, 1700.0});
    const PlacedSettlements more = placeSettlementBuildings(sites, field, nullptr, nullptr);
    REQUIRE(more.set.ofSettlement("settlement_0001").size() == forward.set.ofSettlement("settlement_0001").size());
}

TEST_CASE("a streamed cell carries its buildings, and none without a set", "[bake][settlements]")
{
    const Heightfield field = flatField();
    const PlacedSettlements placed = placeSettlementBuildings({{"settlement_0000", kCity, 1000.0, 1000.0}, {"settlement_0001", kTown, 300.0, 300.0}}, field, nullptr, nullptr);
    REQUIRE_FALSE(placed.set.empty());
    StreamerConfig config;
    config.synchronous = true;
    config.lodCount = 1;
    config.buildings = std::make_shared<const PlacedBuildingSet>(placed.set);

    std::size_t total = 0;
    const auto cells = static_cast<std::int32_t>(field.desc().cellsPerSide());
    for (std::int32_t z = 0; z < cells; ++z) {
        for (std::int32_t x = 0; x < cells; ++x) {
            CellPayload payload;
            WorldStreamer::buildPayload(field, {x, z}, config, payload);
            REQUIRE(payload.buildings.size() == placed.set.inCell({x, z}).size());
            total += payload.buildings.size();
        }
    }
    REQUIRE(total == placed.set.all().size());

    config.buildings.reset();
    CellPayload bare;
    WorldStreamer::buildPayload(field, placed.set.all().front().cell, config, bare);
    REQUIRE(bare.buildings.empty());
}

TEST_CASE("bad inputs are refused, not placed", "[bake][settlements]")
{
    const Heightfield field = flatField();
    const SettlementSite site{"settlement_0000", kCity, 1000.0, 1000.0};
    REQUIRE_FALSE(placeSettlementBuildings({site}, Heightfield{}, nullptr, nullptr).ok());
    SettlementSite nan = site;
    nan.x = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE(placeSettlementBuildings({nan}, field, nullptr, nullptr).ok());
    nan.x = std::numeric_limits<double>::infinity();
    REQUIRE_FALSE(placeSettlementBuildings({nan}, field, nullptr, nullptr).ok());

    PlacedSettlementParams bad;
    bad.maxHeightDeltaM = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(placeSettlementBuildings({site}, field, nullptr, nullptr, bad).ok());
    bad = {};
    bad.tiers[kCity].buildings = 1 << 30;
    REQUIRE_FALSE(placeSettlementBuildings({site}, field, nullptr, nullptr, bad).ok());
    bad = {};
    bad.attemptsPerBuilding = 0;
    REQUIRE_FALSE(placeSettlementBuildings({site}, field, nullptr, nullptr, bad).ok());
    bad = {};
    bad.tiers[kHamlet].minHalfFootprintM = -1.0f;
    REQUIRE_FALSE(placeSettlementBuildings({site}, field, nullptr, nullptr, bad).ok());

    REQUIRE_FALSE(placeSettlementBuildings({site, site}, field, nullptr, nullptr).ok());
    bad = {};
    bad.tiers[kCity].radiusM = 1.0e30f;
    REQUIRE_FALSE(placeSettlementBuildings({site}, field, nullptr, nullptr, bad).ok());

    SettlementSite far = site;
    far.tier = 200;
    far.x = 1.0e12;
    REQUIRE(placeSettlementBuildings({far}, field, nullptr, nullptr).set.empty());
    far.x = 1000.0;
    REQUIRE(placeSettlementBuildings({far}, field, nullptr, nullptr).set.all().size() == placeSettlementBuildings({site}, field, nullptr, nullptr).set.all().size());
    REQUIRE(placeSettlementBuildings({}, field, nullptr, nullptr).set.empty());
}

TEST_CASE("the placeholder building is a closed-top box of the asked size", "[bake][settlements]")
{
    const Mesh mesh = placedBuildingMesh(3.0f, 4.0f, 6.0f);
    REQUIRE(mesh.vertices.size() == 20);
    REQUIRE(mesh.indices.size() == 30);
    float top = -1000.0f;
    float bottom = 1000.0f;
    for (const Vertex& v : mesh.vertices) {
        top = std::max(top, v.position.y);
        bottom = std::min(bottom, v.position.y);
        REQUIRE(std::abs(v.position.x) <= 3.0f);
        REQUIRE(std::abs(v.position.z) <= 4.0f);
    }
    REQUIRE(top == 6.0f);
    REQUIRE(bottom == -0.5f);
    for (const std::uint32_t i : mesh.indices) REQUIRE(i < mesh.vertices.size());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Mesh bad = placedBuildingMesh(nan, nan, nan, nan);
    for (const Vertex& v : bad.vertices) {
        REQUIRE(std::isfinite(v.position.x));
        REQUIRE(std::isfinite(v.position.y));
        REQUIRE(std::isfinite(v.position.z));
    }
}

namespace {

struct ForgedFixture {
    worldforge::ForgedWorld world;
    WorldDesc desc;
    Heightfield field;
    ForgedRoads roads;
    ForgedWater water;
    PlacedSettlements placed;
};

ForgedFixture buildForged(std::uint64_t seed)
{
    WorldDesc base;
    base.seed = seed;
    base.sizeMetres = 2048;
    base.metresPerSample = 8;
    base.cellMetres = 512;
    const WorldDesc desc = forgedWorldDesc(base);
    worldforge::ForgedWorld world = worldforge::forgeWorld(seed, 128);
    ForgedHeightfield adapted = heightfieldFromForgedRoads(world, desc);
    REQUIRE(adapted.ok());
    ForgedRoads roads(world, desc);
    ForgedWater water = waterFromForgedWorld(world, adapted.field);
    REQUIRE(water.ok());
    const WaterField waterField(adapted.field, water.hydrology, water.rivers);
    PlacedSettlements placed = placeForgedSettlements(world, adapted.field, &roads, &waterField);
    return {std::move(world), desc, std::move(adapted.field), std::move(roads), std::move(water), std::move(placed)};
}

const ForgedFixture& forged()
{
    static const ForgedFixture f = [] {
        for (std::uint64_t seed = 1; seed < 40; ++seed) {
            ForgedFixture candidate = buildForged(seed);
            if (candidate.placed.ok() && !candidate.placed.set.empty() && !candidate.roads.empty()) return candidate;
        }
        FAIL("no seed under 40 forged a world with settlements and roads");
        return buildForged(1);
    }();
    return f;
}

}

TEST_CASE("a forged world's settlements get buildings, clear of its roads and water", "[bake][settlements]")
{
    const ForgedFixture& f = forged();
    REQUIRE(f.placed.ok());
    const WaterField water(f.field, f.water.hydrology, f.water.rivers);
    std::set<std::string> known;
    for (std::size_t i = 0; i < f.world.settlements()->sites.size(); ++i) known.insert(worldforge::detail::forgeSettlementId(i));
    for (const PlacedBuilding& b : f.placed.set.all()) {
        REQUIRE(known.contains(b.settlementId));
        REQUIRE(b.tier == f.world.settlements()->sites[std::stoul(b.settlementId.substr(11))].tier);
        REQUIRE(f.roads.coverageAt(b.x, b.z) == 0.0f);
        REQUIRE_FALSE(water.at(b.x, b.z).wet());
        REQUIRE(b.groundY <= f.field.heightAt(b.x, b.z));
    }
}

TEST_CASE("a world that cannot be placed says why", "[bake][settlements]")
{
    const ForgedFixture& f = forged();
    const worldforge::ForgedWorld early = worldforge::forgeWorld(f.world.seed(), 128, worldforge::ForgeConfig{}, worldforge::ForgePhase::Hydrology);
    REQUIRE_FALSE(placeForgedSettlements(early, f.field, nullptr, nullptr).ok());
    REQUIRE_FALSE(placeForgedSettlements(f.world, Heightfield{}, nullptr, nullptr).ok());
}
