#include "ichor/assets/cooker.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/placed_settlements.h"
#include "ichor/world/settlement_kit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>

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

Heightfield flatField()
{
    const WorldDesc desc = smallDesc();
    const std::uint32_t n = desc.samplesPerSide();
    return Heightfield(desc, std::vector<std::uint16_t>(static_cast<std::size_t>(n) * n, static_cast<std::uint16_t>(100.0 / desc.heightRange * 65535.0)));
}

class StripRoad final : public RoadField {
public:
    StripRoad(double x0, double x1) : x0_(x0), x1_(x1) {}
    float coverageAt(double x, double) const override { return x >= x0_ && x <= x1_ ? 1.0f : 0.0f; }

private:
    double x0_;
    double x1_;
};

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

struct Built {
    PlacedSettlements buildings;
    PlacedKit kit;
};

Built build(const Heightfield& field, const RoadField* roads, std::vector<SettlementSite> list = sites())
{
    Built out;
    out.buildings = placeSettlementBuildings(list, field, roads, nullptr);
    REQUIRE(out.buildings.ok());
    out.kit = placeSettlementKit(list, out.buildings.set, field, roads, nullptr);
    REQUIRE(out.kit.ok());
    return out;
}

std::size_t count(const SettlementKitSet& set, const std::string& settlement, KitPiece piece)
{
    return static_cast<std::size_t>(std::count_if(set.all().begin(), set.all().end(), [&](const KitInstance& i) { return i.settlementId == settlement && i.piece == piece; }));
}

bool isWall(KitPiece piece)
{
    return piece == KitPiece::WallSegmentGrid || piece == KitPiece::PalisadeRunGrid || piece == KitPiece::GateBayGrid || piece == KitPiece::WallTower;
}

bool onGrid(double v, double offset)
{
    const double r = std::fmod(v - offset, kKitGridM);
    return std::abs(r) < 1e-9 || std::abs(std::abs(r) - kKitGridM) < 1e-9;
}

}

TEST_CASE("every building gets a kit piece and only villages and up get a wall", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const Built built = build(field, nullptr);
    const SettlementKitSet& set = built.kit.set;
    for (const SettlementSite& site : sites()) {
        const std::size_t buildings = built.buildings.set.ofSettlement(site.id).size();
        REQUIRE(buildings >= 1);
        const std::size_t placed = count(set, site.id, KitPiece::SmallHouse) + count(set, site.id, KitPiece::TempleFront) + count(set, site.id, KitPiece::ColonnadeBay) / 2;
        CHECK(placed == buildings);
    }
    const auto walls = [&](const char* id) {
        return static_cast<std::size_t>(std::count_if(set.all().begin(), set.all().end(), [&](const KitInstance& i) { return i.settlementId == id && isWall(i.piece); }));
    };
    CHECK(walls("settlement_0000") == 0);
    CHECK(count(set, "settlement_0001", KitPiece::PalisadeRunGrid) > 50);
    CHECK(count(set, "settlement_0001", KitPiece::WallTower) == 0);
    CHECK(count(set, "settlement_0002", KitPiece::WallSegmentGrid) > 50);
    CHECK(count(set, "settlement_0002", KitPiece::WallTower) >= 4);
    CHECK(count(set, "settlement_0002", KitPiece::GateBayGrid) == 4);
    CHECK(count(set, "settlement_0003", KitPiece::GateBayGrid) == 4);
    CHECK(count(set, "settlement_0000", KitPiece::TempleFront) == 0);
    CHECK(count(set, "settlement_0003", KitPiece::TempleFront) <= 1);
}

TEST_CASE("wall pieces sit on the 4 m grid", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const Built built = build(field, nullptr);
    std::size_t checked = 0;
    for (const KitInstance& i : built.kit.set.all()) {
        const bool alongX = (i.quarterTurns & 1u) == 0;
        switch (i.piece) {
        case KitPiece::WallSegmentGrid:
        case KitPiece::PalisadeRunGrid:
        case KitPiece::GateBayGrid:
            CHECK(onGrid(alongX ? i.x : i.z, 2.0));
            CHECK(onGrid(alongX ? i.z : i.x, 0.0));
            ++checked;
            break;
        case KitPiece::WallTower:
            CHECK(onGrid(i.x, 0.0));
            CHECK(onGrid(i.z, 0.0));
            ++checked;
            break;
        default: break;
        }
    }
    CHECK(checked > 200);
}

TEST_CASE("neighbouring bays of one side touch edge to edge", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const Built built = build(field, nullptr);
    std::map<double, std::vector<double>> northSide;
    for (const KitInstance& i : built.kit.set.all()) {
        if (i.settlementId == "settlement_0001" && i.piece == KitPiece::PalisadeRunGrid && i.quarterTurns == 0) northSide[i.z].push_back(i.x);
    }
    REQUIRE(northSide.size() == 2);
    for (auto& [z, xs] : northSide) {
        std::sort(xs.begin(), xs.end());
        REQUIRE(xs.size() > 10);
        for (std::size_t k = 1; k < xs.size(); ++k) CHECK(xs[k] - xs[k - 1] == kKitGridM);
    }
}

TEST_CASE("kit pieces stay clear of roads and leave a gap where one crosses", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const StripRoad road(940.0, 1010.0);
    const Built open = build(field, nullptr);
    const Built crossed = build(field, &road);
    const auto walls = [](const Built& b, const char* id) {
        return std::count_if(b.kit.set.all().begin(), b.kit.set.all().end(), [&](const KitInstance& i) { return i.settlementId == id && isWall(i.piece); });
    };
    REQUIRE(walls(open, "settlement_0001") > 0);
    CHECK(walls(crossed, "settlement_0001") < walls(open, "settlement_0001"));
    const PlacedSettlementParams ground;
    for (const KitInstance& i : crossed.kit.set.all()) {
        const KitPieceInfo& info = kitPieceInfo(i.piece);
        const double hw = (i.quarterTurns & 1u) ? info.halfDepthM : info.halfWidthM;
        CHECK((i.x + hw + ground.roadMarginM < 940.0 || i.x - hw - ground.roadMarginM > 1010.0));
    }
}

TEST_CASE("no tower stands inside a gate bay", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    // A town whose wall half-extent is a multiple of the tower spacing puts a tower on the gate's line.
    std::vector<SettlementSite> list = {{"settlement_0000", kTown, 600.0, 600.0}, {"settlement_0001", kCity, 1400.0, 1400.0}};
    PlacedSettlementParams ground;
    ground.tiers[kTown].radiusM = 88.0f; // 88 + 8 margin = 96, a multiple of 24
    SettlementKitParams params;
    params.ground = ground;
    const PlacedSettlements buildings = placeSettlementBuildings(list, field, nullptr, nullptr, ground);
    const PlacedKit kit = placeSettlementKit(list, buildings.set, field, nullptr, nullptr, params);
    REQUIRE(kit.ok());
    std::size_t gates = 0;
    for (const KitInstance& gate : kit.set.all()) {
        if (gate.piece != KitPiece::GateBayGrid) continue;
        ++gates;
        const KitPieceInfo& g = kitPieceInfo(gate.piece);
        const double ghw = (gate.quarterTurns & 1u) ? g.halfDepthM : g.halfWidthM;
        const double ghd = (gate.quarterTurns & 1u) ? g.halfWidthM : g.halfDepthM;
        for (const KitInstance& tower : kit.set.all()) {
            if (tower.piece != KitPiece::WallTower || tower.settlementId != gate.settlementId) continue;
            const KitPieceInfo& t = kitPieceInfo(tower.piece);
            CHECK(!(std::abs(tower.x - gate.x) < t.halfWidthM + ghw && std::abs(tower.z - gate.z) < t.halfDepthM + ghd));
        }
    }
    CHECK(gates == 8);
}

TEST_CASE("two sites with one id are refused", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const PlacedSettlements none = placeSettlementBuildings({}, field, nullptr, nullptr);
    const std::vector<SettlementSite> twice = {{"settlement_0000", kTown, 600.0, 600.0}, {"settlement_0000", kTown, 900.0, 900.0}};
    CHECK_FALSE(placeSettlementKit(twice, none.set, field, nullptr, nullptr).ok());
}

TEST_CASE("wall pieces never overlap a building", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const Built built = build(field, nullptr);
    for (const KitInstance& i : built.kit.set.all()) {
        if (!isWall(i.piece)) continue;
        const KitPieceInfo& info = kitPieceInfo(i.piece);
        const double hw = (i.quarterTurns & 1u) ? info.halfDepthM : info.halfWidthM;
        const double hd = (i.quarterTurns & 1u) ? info.halfWidthM : info.halfDepthM;
        for (const PlacedBuilding& b : built.buildings.set.ofSettlement(i.settlementId)) {
            CHECK(!(std::abs(i.x - b.x) < hw + b.halfWidthM && std::abs(i.z - b.z) < hd + b.halfDepthM));
        }
    }
}

TEST_CASE("the same inputs give the same kit whatever order the sites come in", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    std::vector<SettlementSite> reversed(sites().rbegin(), sites().rend());
    const Built a = build(field, nullptr);
    const Built b = build(field, nullptr, reversed);
    REQUIRE(a.kit.set.all().size() == b.kit.set.all().size());
    for (std::size_t k = 0; k < a.kit.set.all().size(); ++k) {
        const KitInstance& x = a.kit.set.all()[k];
        const KitInstance& y = b.kit.set.all()[k];
        CHECK(x.piece == y.piece);
        CHECK(x.x == y.x);
        CHECK(x.z == y.z);
        CHECK(x.quarterTurns == y.quarterTurns);
        CHECK(x.settlementId == y.settlementId);
    }
}

TEST_CASE("kit pieces are found by their cell and every piece is in one", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const Built built = build(field, nullptr);
    std::size_t total = 0;
    const std::uint32_t side = field.desc().cellsPerSide();
    for (std::uint32_t z = 0; z < side; ++z) {
        for (std::uint32_t x = 0; x < side; ++x) total += built.kit.set.inCell({static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)}).size();
    }
    CHECK(total == built.kit.set.all().size());
    CHECK(built.kit.set.inCell({-1, 0}).empty());
}

TEST_CASE("hostile kit input is refused or yields nothing", "[bake][settlements][kit]")
{
    const Heightfield field = flatField();
    const PlacedSettlements none = placeSettlementBuildings({}, field, nullptr, nullptr);
    CHECK(placeSettlementKit({}, none.set, field, nullptr, nullptr).set.empty());
    const std::vector<SettlementSite> nan = {{"settlement_0000", kCity, std::numeric_limits<double>::quiet_NaN(), 10.0}};
    CHECK_FALSE(placeSettlementKit(nan, none.set, field, nullptr, nullptr).ok());
    SettlementKitParams bad;
    bad.sinkM = std::numeric_limits<float>::infinity();
    CHECK_FALSE(placeSettlementKit({}, none.set, field, nullptr, nullptr, bad).ok());
    CHECK_FALSE(placeSettlementKit({}, none.set, Heightfield(), nullptr, nullptr).ok());
    // A city at the world's corner has its wall off the map: pieces drop, nothing is forced and nothing crashes.
    const std::vector<SettlementSite> corner = {{"settlement_0000", kCity, 4.0, 4.0}};
    const PlacedSettlements cornerBuildings = placeSettlementBuildings(corner, field, nullptr, nullptr);
    CHECK(placeSettlementKit(corner, cornerBuildings.set, field, nullptr, nullptr).ok());
}

TEST_CASE("quarter turns send the piece's axes to exact directions", "[bake][settlements][kit]")
{
    CHECK(kitBasis(0) * glm::vec3(0, 0, 1) == glm::vec3(0, 0, 1));
    CHECK(kitBasis(1) * glm::vec3(1, 0, 0) == glm::vec3(0, 0, -1));
    CHECK(kitBasis(1) * glm::vec3(0, 0, 1) == glm::vec3(1, 0, 0));
    CHECK(kitBasis(2) * glm::vec3(0, 0, 1) == glm::vec3(0, 0, -1));
    CHECK(kitBasis(3) * glm::vec3(0, 0, 1) == glm::vec3(-1, 0, 0));
    CHECK(kitBasis(4) == kitBasis(0));
}

TEST_CASE("the exported GLBs cook to the sizes the kit table says", "[bake][settlements][kit]")
{
    const std::filesystem::path folder = std::filesystem::path(ICHOR_ART_DIR) / "settlement_kit";
    SceneBundle bundle;
    KitMeshes meshes;
    std::array<bool, kKitPieceCount> all{};
    all.fill(true);
    REQUIRE(cookSettlementKit(folder, all, bundle, meshes));
    REQUIRE(bundle.meshes.size() == kKitPieceCount);
    for (std::size_t i = 0; i < kKitPieceCount; ++i) {
        const KitPieceInfo& info = kitPieceInfo(static_cast<KitPiece>(i));
        const Mesh& mesh = bundle.meshes[meshes.mesh[i]];
        INFO(info.file);
        CHECK(mesh.boundsMax.x - mesh.boundsMin.x == Catch::Approx(2.0f * info.halfWidthM).margin(0.05));
        CHECK(mesh.boundsMax.z - mesh.boundsMin.z == Catch::Approx(2.0f * info.halfDepthM).margin(0.05));
        CHECK(mesh.boundsMax.y == Catch::Approx(info.heightM).margin(0.05));
        CHECK(mesh.boundsMin.y == Catch::Approx(0.0f).margin(0.05));
        for (const Submesh& submesh : mesh.submeshes) CHECK(submesh.material < bundle.materials.size());
    }
    CHECK_FALSE(bundle.textures.empty());
    SceneBundle untouched;
    CHECK_FALSE(cookSettlementKit(folder / "nowhere", all, untouched, meshes));
    CHECK(untouched.meshes.empty());
}
