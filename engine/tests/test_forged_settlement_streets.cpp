#include "forged_world_fixture.h"

#include "ichor/assets/bundle.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/forged_roads.h"
#include "ichor/world/placed_settlements.h"
#include "ichor/world/ribbon_height.h"
#include "ichor/world/road_surface.h"
#include "ichor/world/settlement_kit.h"
#include "ichor/world/settlement_streets.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

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

Heightfield shaped(const std::function<double(double, double)>& heightAt)
{
    const WorldDesc desc = smallDesc();
    const std::uint32_t n = desc.samplesPerSide();
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const double h = heightAt(x * static_cast<double>(desc.metresPerSample), z * static_cast<double>(desc.metresPerSample));
            samples[static_cast<std::size_t>(z) * n + x] = static_cast<std::uint16_t>(std::lround(h / desc.heightRange * 65535.0));
        }
    }
    return Heightfield(desc, std::move(samples));
}

Heightfield flatField() { return shaped([](double, double) { return 100.0; }); }

const std::vector<SettlementSite>& sites()
{
    static const std::vector<SettlementSite> s = {
        {"settlement_0000", kHamlet, 400.0, 400.0},
        {"settlement_0001", kVillage, 1000.0, 400.0},
        {"settlement_0002", kTown, 600.0, 1400.0},
        {"settlement_0003", kCity, 1500.0, 1500.0},
    };
    return s;
}

struct Laid {
    SettlementStreets streets;
    PlacedSettlements buildings;
    PlacedKit kit;
};

Laid lay(const Heightfield& field, const RoadField* roads, const WaterField* water, const std::vector<SettlementSite>& list = sites())
{
    Laid out;
    const PlacedSettlementParams tiers = forgedStreetTierParams();
    out.streets = layoutSettlementStreets(list, field, tiers);
    REQUIRE(out.streets.ok());
    out.buildings = placeSettlementBuildings(list, field, roads, water, tiers, &out.streets.set);
    REQUIRE(out.buildings.ok());
    out.kit = placeSettlementKit(list, out.buildings.set, field, roads, water);
    REQUIRE(out.kit.ok());
    return out;
}

class StripRoad final : public RoadField {
public:
    StripRoad(double x0, double x1) : x0_(x0), x1_(x1) {}
    float coverageAt(double x, double) const override { return x >= x0_ && x <= x1_ ? 1.0f : 0.0f; }

private:
    double x0_;
    double x1_;
};

// The lowest a strip's plane comes over the terrain mesh, over every triangle's whole area (not only at its vertices), as metres above the ground.
float lowestClearance(const Heightfield& field, const Mesh& mesh, double originX, double originZ, std::size_t& triangles)
{
    float lowest = 1e9f;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[t]];
        const Vertex& b = mesh.vertices[mesh.indices[t + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[t + 2]];
        ++triangles;
        for (int i = 0; i <= 5; ++i) {
            for (int j = 0; i + j <= 5; ++j) {
                const float w1 = static_cast<float>(i) / 5.0f;
                const float w2 = static_cast<float>(j) / 5.0f;
                const float w0 = 1.0f - w1 - w2;
                const glm::vec3 p = a.position * w0 + b.position * w1 + c.position * w2;
                lowest = std::min(lowest, p.y - terrainMeshHeightAt(field, originX + p.x, originZ + p.z));
            }
        }
    }
    return lowest;
}

bool insideBand(const Street& street, double x, double z, double extra)
{
    const double across = std::abs((street.alongX ? z : x) - street.fixed);
    const double along = street.alongX ? x : z;
    return across <= static_cast<double>(street.widthM) * 0.5 + extra && along >= street.from && along <= street.to;
}

}

TEST_CASE("the street layout is the same for the same seed and sites, in any order", "[bake][settlements][streets]")
{
    const Heightfield field = flatField();
    const Laid a = lay(field, nullptr, nullptr);
    std::vector<SettlementSite> reversed(sites().rbegin(), sites().rend());
    const Laid b = lay(field, nullptr, nullptr, reversed);
    REQUIRE(a.buildings.set.all().size() == b.buildings.set.all().size());
    REQUIRE_FALSE(a.buildings.set.all().empty());
    for (std::size_t i = 0; i < a.buildings.set.all().size(); ++i) {
        const PlacedBuilding& x = a.buildings.set.all()[i];
        const PlacedBuilding& y = b.buildings.set.all()[i];
        CHECK(x.id == y.id);
        CHECK(x.x == y.x);
        CHECK(x.z == y.z);
        CHECK(x.facing == y.facing);
        CHECK(x.heightM == y.heightM);
    }
    REQUIRE(a.kit.set.all().size() == b.kit.set.all().size());

    WorldDesc other = smallDesc();
    other.seed = 8;
    const std::uint32_t n = other.samplesPerSide();
    const Heightfield otherField(other, std::vector<std::uint16_t>(static_cast<std::size_t>(n) * n, static_cast<std::uint16_t>(100.0 / other.heightRange * 65535.0)));
    const Laid c = lay(otherField, nullptr, nullptr);
    bool differs = c.buildings.set.all().size() != a.buildings.set.all().size();
    for (std::size_t i = 0; !differs && i < a.buildings.set.all().size(); ++i) {
        differs = a.buildings.set.all()[i].x != c.buildings.set.all()[i].x || a.buildings.set.all()[i].z != c.buildings.set.all()[i].z;
    }
    CHECK(differs);
}

TEST_CASE("a bigger settlement has more streets and more houses along them", "[bake][settlements][streets]")
{
    const Heightfield field = flatField();
    const Laid laid = lay(field, nullptr, nullptr);
    std::size_t previousStreets = 0;
    std::size_t previousHouses = 0;
    for (const SettlementSite& site : sites()) {
        const SettlementLayout* layout = laid.streets.set.of(site.id);
        REQUIRE(layout != nullptr);
        const std::size_t houses = laid.buildings.set.ofSettlement(site.id).size();
        INFO(site.id);
        CHECK(layout->streets.size() > previousStreets);
        CHECK(houses > previousHouses);
        previousStreets = layout->streets.size();
        previousHouses = houses;
    }
}

TEST_CASE("every house fronts a street and its door faces it", "[bake][settlements][streets]")
{
    const Heightfield field = flatField();
    const Laid laid = lay(field, nullptr, nullptr);
    std::size_t fronting = 0;
    for (const PlacedBuilding& b : laid.buildings.set.all()) {
        INFO(b.id);
        REQUIRE(b.facing >= 0);
        const SettlementLayout* layout = laid.streets.set.of(b.settlementId);
        REQUIRE(layout != nullptr);
        const bool facesX = (b.facing & 1) != 0;
        const double across = static_cast<double>(facesX ? b.halfWidthM : b.halfDepthM);
        const double stepX = b.facing == 1 ? 1.0 : b.facing == 3 ? -1.0 : 0.0;
        const double stepZ = b.facing == 0 ? 1.0 : b.facing == 2 ? -1.0 : 0.0;
        // A step to the front of the lot, a metre and a bit past the house's own front, stands on the street.
        const double reach = across + SettlementStreetParams{}.frontGapM + SettlementStreetParams{}.widthM * 0.5;
        const bool onStreet = std::any_of(layout->streets.begin(), layout->streets.end(), [&](const Street& s) { return insideBand(s, b.x + stepX * reach, b.z + stepZ * reach, 0.05); });
        CHECK(onStreet);
        // The back of the house is not on one.
        const bool backOnStreet = std::any_of(layout->streets.begin(), layout->streets.end(), [&](const Street& s) { return insideBand(s, b.x - stepX * across, b.z - stepZ * across, 0.0); });
        CHECK_FALSE(backOnStreet);
        fronting += onStreet ? 1 : 0;
    }
    CHECK(fronting == laid.buildings.set.all().size());

    // The kit's house turns to the same side as its lot.
    for (const KitInstance& piece : laid.kit.set.all()) {
        if (piece.piece != KitPiece::SmallHouse) continue;
        const SettlementLayout* layout = laid.streets.set.of(piece.settlementId);
        REQUIRE(layout != nullptr);
        const glm::vec3 front = kitBasis(piece.quarterTurns)[2];
        const double reach = static_cast<double>(kitPieceInfo(KitPiece::SmallHouse).halfDepthM) + SettlementStreetParams{}.frontGapM + SettlementStreetParams{}.widthM * 0.5;
        const bool onStreet = std::any_of(layout->streets.begin(), layout->streets.end(),
            [&](const Street& s) { return insideBand(s, piece.x + front.x * reach, piece.z + front.z * reach, 0.05); });
        INFO(piece.settlementId << " at " << piece.x << ", " << piece.z);
        CHECK(onStreet);
    }
}

TEST_CASE("the main streets lead through the wall's gates", "[bake][settlements][streets]")
{
    const Heightfield field = flatField();
    const Laid laid = lay(field, nullptr, nullptr);
    for (const SettlementSite& site : {sites()[2], sites()[3]}) {
        const SettlementLayout* layout = laid.streets.set.of(site.id);
        REQUIRE(layout != nullptr);
        std::size_t gates = 0;
        for (const KitInstance& piece : laid.kit.set.all()) {
            if (piece.settlementId != site.id || piece.piece != KitPiece::GateBayGrid) continue;
            ++gates;
            const bool through = std::any_of(layout->streets.begin(), layout->streets.end(), [&](const Street& s) { return s.main && insideBand(s, piece.x, piece.z, -0.01); });
            INFO(site.id << " gate at " << piece.x << ", " << piece.z);
            CHECK(through);
        }
        CHECK(gates == 4);
    }
}

TEST_CASE("houses and streets stay inside the settlement's footprint and keep off water and roads", "[bake][settlements][streets]")
{
    constexpr double kRim = 400.0;
    const Heightfield bowl = shaped([](double x, double z) {
        const double r = std::sqrt((x - 1500.0) * (x - 1500.0) + (z - 1500.0) * (z - 1500.0));
        return r < kRim ? 100.0 + 20.0 * (r / kRim) : 120.0;
    });
    const auto hydrology = std::make_shared<const Hydrology>(computeHydrology(bowl));
    HydrologyParams rivers;
    rivers.riverThreshold = 1u << 30;
    const WaterField water(bowl, hydrology, rivers);
    REQUIRE(water.at(1500.0, 1500.0).wet());
    const std::vector<SettlementSite> shoreSites = {
        {"settlement_0002", kTown, 600.0, 1400.0},
        {"settlement_0003", kCity, 1880.0, 1500.0},
    };
    const StripRoad road(596.0, 604.0);

    const Laid laid = lay(bowl, &road, &water, shoreSites);
    const Laid drySame = lay(bowl, &road, nullptr, shoreSites);
    REQUIRE(laid.buildings.set.ofSettlement("settlement_0003").size() > 0);
    REQUIRE(laid.buildings.set.ofSettlement("settlement_0002").size() > 0);
    CHECK(laid.buildings.set.ofSettlement("settlement_0003").size() < drySame.buildings.set.ofSettlement("settlement_0003").size());
    for (const PlacedBuilding& b : laid.buildings.set.all()) {
        INFO(b.id);
        const SettlementLayout* layout = laid.streets.set.of(b.settlementId);
        REQUIRE(layout != nullptr);
        CHECK(std::abs(b.x - layout->centreX) + b.halfWidthM <= layout->halfM);
        CHECK(std::abs(b.z - layout->centreZ) + b.halfDepthM <= layout->halfM);
        for (const double fx : {-1.0, 0.0, 1.0}) {
            for (const double fz : {-1.0, 0.0, 1.0}) {
                const double px = b.x + fx * b.halfWidthM;
                const double pz = b.z + fz * b.halfDepthM;
                CHECK_FALSE(water.at(px, pz).wet());
                CHECK(road.coverageAt(px, pz) == 0.0f);
            }
        }
    }

    const std::int32_t cells = static_cast<std::int32_t>(bowl.desc().cellsPerSide());
    std::size_t vertices = 0;
    for (std::int32_t cz = 0; cz < cells; ++cz) {
        for (std::int32_t cx = 0; cx < cells; ++cx) {
            const CellId id{cx, cz};
            const Mesh strip = laid.streets.set.buildMesh(bowl, id, &road, &water);
            const double ox = bowl.cellOriginX(id);
            const double oz = bowl.cellOriginZ(id);
            for (const Vertex& v : strip.vertices) {
                ++vertices;
                CHECK_FALSE(water.at(ox + v.position.x, oz + v.position.z).wet());
                CHECK(road.coverageAt(ox + v.position.x, oz + v.position.z) < 1.0f);
            }
        }
    }
    CHECK(vertices > 0);
}

TEST_CASE("a street strip lies above the ground it follows so it does not z-fight", "[bake][settlements][streets]")
{
    const Heightfield hills = shaped([](double x, double z) { return 100.0 + 3.0 * std::sin(x * 0.045) * std::cos(z * 0.06) + 0.8 * std::sin(x * 0.31 + z * 0.27); });
    const SettlementStreets streets = layoutSettlementStreets(sites(), hills, forgedStreetTierParams());
    REQUIRE(streets.ok());
    std::size_t checked = 0;
    const std::int32_t cells = static_cast<std::int32_t>(hills.desc().cellsPerSide());
    for (std::int32_t cz = 0; cz < cells; ++cz) {
        for (std::int32_t cx = 0; cx < cells; ++cx) {
            const CellId id{cx, cz};
            const Mesh strip = streets.set.buildMesh(hills, id, nullptr, nullptr);
            const double ox = hills.cellOriginX(id);
            const double oz = hills.cellOriginZ(id);
            REQUIRE(strip.indices.size() % 3 == 0);
            if (strip.vertices.empty()) continue;
            for (std::size_t t = 0; t + 2 < strip.indices.size(); t += 3) {
                const glm::vec3 a = strip.vertices[strip.indices[t]].position;
                const glm::vec3 b = strip.vertices[strip.indices[t + 1]].position;
                const glm::vec3 c = strip.vertices[strip.indices[t + 2]].position;
                CHECK(glm::cross(b - a, c - a).y > 0.0f);
            }
            CHECK(lowestClearance(hills, strip, ox, oz, checked) >= 0.0f);
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("the ground tint is full inside a settlement, fades past its wall and is zero away from it", "[bake][settlements][streets]")
{
    const Heightfield field = flatField();
    const SettlementStreets streets = layoutSettlementStreets(sites(), field, forgedStreetTierParams());
    REQUIRE(streets.ok());
    const SettlementLayout* town = streets.set.of("settlement_0002");
    REQUIRE(town != nullptr);
    CHECK(streets.set.groundAmountAt(town->centreX, town->centreZ) == 1.0f);
    CHECK(streets.set.groundAmountAt(town->centreX + town->halfM - 1.0, town->centreZ) == 1.0f);
    float previous = 1.0f;
    for (double out = 0.0; out <= SettlementStreetSet::kFadeM; out += 1.0) {
        const float amount = streets.set.groundAmountAt(town->centreX + town->halfM + out, town->centreZ);
        CHECK(amount <= previous);
        previous = amount;
    }
    CHECK(previous == 0.0f);
    CHECK(streets.set.groundAmountAt(100.0, 1900.0) == 0.0f);
}

TEST_CASE("the road dirt tile is deterministic, mipmapped and wears out at its edges", "[bake][settlements][streets]")
{
    const Texture a = dirtRoadTexture(64);
    const Texture b = dirtRoadTexture(64);
    CHECK(a.pixels == b.pixels);
    CHECK(a.mipCount == mipLevelCount(64, 64));
    std::size_t expected = 0;
    for (std::uint32_t size = 64; size >= 1; size /= 2) expected += static_cast<std::size_t>(size) * size * 4;
    CHECK(a.pixels.size() == expected);

    const auto columnMean = [&](std::uint32_t x, int channel) {
        double sum = 0.0;
        for (std::uint32_t y = 0; y < a.height; ++y) sum += a.pixels[(static_cast<std::size_t>(y) * a.width + x) * 4 + channel];
        return sum / a.height;
    };
    // Between the ruts the surface is lighter than in them, and at the edge it has gone green-brown like the ground.
    const double rut = columnMean(static_cast<std::uint32_t>(0.29 * 64), 0);
    const double between = columnMean(static_cast<std::uint32_t>(0.4 * 64), 0);
    CHECK(between > rut);
    const double edgeRatio = columnMean(0, 1) / columnMean(0, 0);
    const double centreRatio = columnMean(static_cast<std::uint32_t>(0.4 * 64), 1) / columnMean(static_cast<std::uint32_t>(0.4 * 64), 0);
    CHECK(edgeRatio > centreRatio + 0.1);
}

namespace {

constexpr int kGrid = 64;

std::unique_ptr<ForgedGame> forgedGame(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    const ForgeJobResult forged = test::forgedWorldFixture(desc, kGrid);
    auto game = buildForgedGame(forged, desc);
    REQUIRE(game->ok());
    return game;
}

}

TEST_CASE("a forged world's settlements have streets, houses along them and a worn ground", "[forged_game][streets]")
{
    const auto game = forgedGame(1);
    REQUIRE(game->streets);
    REQUIRE_FALSE(game->streets->empty());
    REQUIRE(game->buildings);
    std::size_t withHouses = 0;
    for (const SettlementSite& site : game->sites) {
        const SettlementLayout* layout = game->streets->of(site.id);
        if (layout == nullptr) continue;
        const std::vector<PlacedBuilding> houses = game->buildings->ofSettlement(site.id);
        withHouses += houses.empty() ? 0 : 1;
        for (const PlacedBuilding& b : houses) {
            INFO(b.id);
            CHECK(b.facing >= 0);
            CHECK(std::abs(b.x - layout->centreX) + b.halfWidthM <= layout->halfM);
            CHECK(std::abs(b.z - layout->centreZ) + b.halfDepthM <= layout->halfM);
            CHECK_FALSE(game->waterField->at(b.x, b.z).wet());
            CHECK(game->roads->coverageAt(b.x, b.z) == 0.0f);
        }
    }
    CHECK(withHouses > 0);

    const ForgedScenery scenery = buildForgedScenery(*game);
    CHECK_FALSE(scenery.streetMesh.empty());
    REQUIRE_FALSE(scenery.bundle.textures.empty());
    CHECK(scenery.bundle.materials[0].baseColorTexture == 0);

    // The streamed ground carries the tint inside a settlement and none elsewhere.
    StreamerConfig config;
    config.biomes = game->biomes;
    config.streets = game->streets;
    config.lodCount = 1;
    float deepest = -1.0f;
    float shallowest = -1.0f;
    for (const SettlementLayout& layout : game->streets->all()) {
        CellPayload payload;
        WorldStreamer::buildPayload(game->heightfield, game->heightfield.cellAt(layout.centreX, layout.centreZ), config, payload);
        for (const Vertex& v : payload.lods[0].vertices) deepest = std::min(deepest, v.tangent.w);
    }
    CHECK(deepest < -1.9f);
    StreamerConfig plain = config;
    plain.streets = nullptr;
    CellPayload untinted;
    const SettlementLayout& first = game->streets->all().front();
    WorldStreamer::buildPayload(game->heightfield, game->heightfield.cellAt(first.centreX, first.centreZ), plain, untinted);
    for (const Vertex& v : untinted.lods[0].vertices) shallowest = std::max(shallowest, v.tangent.w);
    CHECK(shallowest == -1.0f);
}

TEST_CASE("the forged road ribbon lies above the ground and never dips under it between its vertices", "[forged_game][streets]")
{
    const auto game = forgedGame(1);
    std::size_t triangles = 0;
    const std::int32_t cells = static_cast<std::int32_t>(game->desc.cellsPerSide());
    for (std::int32_t cz = 0; cz < cells; ++cz) {
        for (std::int32_t cx = 0; cx < cells; ++cx) {
            const CellId id{cx, cz};
            const Mesh ribbon = game->roads->buildMesh(game->heightfield, id);
            const double ox = game->heightfield.cellOriginX(id);
            const double oz = game->heightfield.cellOriginZ(id);
            if (ribbon.vertices.empty()) continue;
            CHECK(lowestClearance(game->heightfield, ribbon, ox, oz, triangles) >= 0.0f);
        }
    }
    CHECK(triangles > 0);
}
