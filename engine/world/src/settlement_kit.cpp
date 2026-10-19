#include "ichor/world/settlement_kit.h"

#include "ichor/assets/cooker.h"
#include "ichor/core/log.h"

#include <worldforge/subseed.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace ichor {

struct SettlementKitBuilder {
    static void add(SettlementKitSet& set, KitInstance instance) { set.instances_.push_back(std::move(instance)); }

    static void index(SettlementKitSet& set, const Heightfield& heightfield)
    {
        const auto last = static_cast<std::int32_t>(heightfield.desc().cellsPerSide()) - 1;
        for (std::size_t i = 0; i < set.instances_.size(); ++i) {
            KitInstance& instance = set.instances_[i];
            CellId cell = heightfield.cellAt(instance.x, instance.z);
            cell.x = std::clamp(cell.x, 0, last);
            cell.z = std::clamp(cell.z, 0, last);
            instance.cell = cell;
            set.byCell_[cell].push_back(i);
        }
    }
};

namespace {

constexpr std::uint64_t kKitDomain = 0x6b69747061636b73ull;
constexpr double kMaxWallHalfM = 400.0;
constexpr std::uint8_t kVillage = 1;
constexpr std::uint8_t kTown = 2;

constexpr KitPieceInfo kInfo[kKitPieceCount] = {
    {"SM_SmallHouse.glb", 1.97f, 1.84f, 3.38f},
    {"SM_TempleFront.glb", 4.00f, 4.00f, 7.03f},
    {"SM_ColonnadeBay.glb", 2.02f, 0.45f, 4.00f},
    {"SM_WallTower.glb", 2.01f, 2.02f, 8.00f},
    {"SM_GateBayGrid.glb", 2.00f, 0.60f, 4.50f},
    {"SM_WallSegmentGrid.glb", 2.00f, 0.30f, 3.20f},
    {"SM_PalisadeRunGrid.glb", 2.00f, 0.15f, 2.50f},
};

PlacedKit failure(std::string message)
{
    PlacedKit out;
    out.error = std::move(message);
    return out;
}

double snapToGrid(double v) { return std::floor(v / kKitGridM + 0.5) * kKitGridM; }

struct Context {
    const SettlementKitParams& params;
    const Heightfield& field;
    const RoadField* roads;
    const WaterField* water;
    const SettlementSite& site;
    const std::vector<const PlacedBuilding*>& buildings;
    SettlementKitSet& out;
};

bool clearOfBuildings(const Context& c, double x, double z, double hw, double hd)
{
    const double gap = c.params.buildingGapM;
    return std::none_of(c.buildings.begin(), c.buildings.end(), [&](const PlacedBuilding* b) {
        return std::abs(x - b->x) < hw + b->halfWidthM + gap && std::abs(z - b->z) < hd + b->halfDepthM + gap;
    });
}

// A wall piece stands only on ground that passes the buildings' own rule and clear of the settlement's buildings.
void tryWallPiece(const Context& c, KitPiece piece, double x, double z, std::uint8_t quarter)
{
    const KitPieceInfo& info = kitPieceInfo(piece);
    const bool turned = (quarter & 1u) != 0;
    const double hw = turned ? info.halfDepthM : info.halfWidthM;
    const double hd = turned ? info.halfWidthM : info.halfDepthM;
    float lowest = 0.0f;
    if (!footprintStandable(x, z, hw, hd, c.field, c.roads, c.water, c.params.ground, lowest)) return;
    if (!clearOfBuildings(c, x, z, hw, hd)) return;
    SettlementKitBuilder::add(c.out, {piece, c.site.id, x, z, lowest - c.params.sinkM, quarter, {}});
}

void placeBuildings(const Context& c)
{
    const std::uint64_t seed = worldforge::splitmix64(c.field.desc().seed ^ kKitDomain);
    bool templePlaced = false;
    for (const PlacedBuilding* b : c.buildings) {
        const std::uint64_t pick = worldforge::splitmix64(seed ^ worldforge::hashText(b->id));
        const double dx = c.site.x - b->x;
        const double dz = c.site.z - b->z;
        const bool alongX = std::abs(dx) > std::abs(dz);
        const float smaller = std::min(b->halfWidthM, b->halfDepthM);
        const float larger = std::max(b->halfWidthM, b->halfDepthM);
        const float ground = b->groundY - c.params.sinkM;
        if (b->tier >= kVillage && !templePlaced && smaller >= c.params.minTempleHalfM) {
            templePlaced = true;
            SettlementKitBuilder::add(c.out, {KitPiece::TempleFront, c.site.id, b->x, b->z, ground, static_cast<std::uint8_t>(alongX ? (dx > 0 ? 1 : 3) : (dz > 0 ? 0 : 2)), {}});
            continue;
        }
        if (b->tier >= kTown && larger >= c.params.minColonnadeHalfM && pick % 3 == 0) {
            const KitPieceInfo& bay = kitPieceInfo(KitPiece::ColonnadeBay);
            const bool runsAlongX = b->halfWidthM >= b->halfDepthM;
            const std::uint8_t quarter = runsAlongX ? (dz > 0 ? 0 : 2) : (dx > 0 ? 1 : 3);
            for (const double side : {-1.0, 1.0}) {
                const double along = side * bay.halfWidthM;
                SettlementKitBuilder::add(c.out, {KitPiece::ColonnadeBay, c.site.id, b->x + (runsAlongX ? along : 0.0), b->z + (runsAlongX ? 0.0 : along), ground, quarter, {}});
            }
            continue;
        }
        SettlementKitBuilder::add(c.out, {KitPiece::SmallHouse, c.site.id, b->x, b->z, ground, static_cast<std::uint8_t>(alongX ? (dx > 0 ? 1 : 3) : (dz > 0 ? 0 : 2)), {}});
    }
}

void placePerimeter(const Context& c, std::uint8_t tier)
{
    const SettlementTierParams& t = c.params.ground.tiers[tier];
    const double half = std::ceil((static_cast<double>(t.radiusM) + c.params.wallMarginM) / kKitGridM) * kKitGridM;
    if (!(half >= kKitGridM) || half > kMaxWallHalfM) return;
    const bool stone = tier >= kTown;
    const KitPiece run = stone ? KitPiece::WallSegmentGrid : KitPiece::PalisadeRunGrid;
    const double cx = snapToGrid(c.site.x);
    const double cz = snapToGrid(c.site.z);
    const int bays = static_cast<int>(half / 2.0);
    const int gate = bays / 2 - 1;
    for (const double side : {-1.0, 1.0}) {
        for (int i = 0; i < bays; ++i) {
            const double along = -half + 2.0 + kKitGridM * i;
            const KitPiece piece = stone && i == gate ? KitPiece::GateBayGrid : run;
            tryWallPiece(c, piece, cx + along, cz + side * half, 0);
        }
    }
    for (const double side : {-1.0, 1.0}) {
        for (int i = 0; i < bays; ++i) {
            const double along = -half + 2.0 + kKitGridM * i;
            const KitPiece piece = stone && i == gate ? KitPiece::GateBayGrid : run;
            tryWallPiece(c, piece, cx + side * half, cz + along, 1);
        }
    }
    if (!stone) return;
    const double spacing = std::max(kKitGridM, snapToGrid(c.params.towerSpacingM));
    for (const double sx : {-1.0, 1.0}) {
        for (const double sz : {-1.0, 1.0}) tryWallPiece(c, KitPiece::WallTower, cx + sx * half, cz + sz * half, 0);
    }
    const double gateLo = -kKitGridM * 1.5;
    const double gateHi = kKitGridM * 0.5;
    for (double along = -half + spacing; along < half; along += spacing) {
        if (along > gateLo && along < gateHi) continue; // a tower there would stand half inside the gate bay
        for (const double side : {-1.0, 1.0}) {
            tryWallPiece(c, KitPiece::WallTower, cx + along, cz + side * half, 0);
            tryWallPiece(c, KitPiece::WallTower, cx + side * half, cz + along, 0);
        }
    }
}

}

const KitPieceInfo& kitPieceInfo(KitPiece piece)
{
    return kInfo[std::min<std::size_t>(static_cast<std::size_t>(piece), kKitPieceCount - 1)];
}

glm::mat3 kitBasis(std::uint8_t quarterTurns)
{
    switch (quarterTurns & 3u) {
    case 1: return glm::mat3(glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(1, 0, 0));
    case 2: return glm::mat3(glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, -1));
    case 3: return glm::mat3(glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0));
    default: return glm::mat3(1.0f);
    }
}

std::vector<KitInstance> SettlementKitSet::inCell(CellId id) const
{
    std::vector<KitInstance> out;
    if (const auto found = byCell_.find(id); found != byCell_.end()) {
        for (const std::size_t i : found->second) out.push_back(instances_[i]);
    }
    return out;
}

PlacedKit placeSettlementKit(const std::vector<SettlementSite>& sites, const PlacedBuildingSet& buildings, const Heightfield& heightfield, const RoadField* roads,
    const WaterField* water, const SettlementKitParams& params)
{
    if (heightfield.empty() || !heightfield.desc().valid()) return failure("the heightfield is empty");
    if (!std::isfinite(params.sinkM) || params.sinkM < 0.0f || !std::isfinite(params.wallMarginM) || params.wallMarginM < 0.0f || !std::isfinite(params.towerSpacingM)
        || !std::isfinite(params.buildingGapM) || params.buildingGapM < 0.0f || !std::isfinite(params.minTempleHalfM) || !std::isfinite(params.minColonnadeHalfM)) {
        return failure("a settlement kit parameter is out of range or not finite");
    }
    for (const SettlementSite& site : sites) {
        if (!std::isfinite(site.x) || !std::isfinite(site.z)) return failure(std::format("{} has a position that is not finite", site.id));
    }
    for (const PlacedBuilding& b : buildings.all()) {
        if (!std::isfinite(b.x) || !std::isfinite(b.z) || !std::isfinite(b.groundY) || !std::isfinite(b.halfWidthM) || !std::isfinite(b.halfDepthM)) {
            return failure(std::format("{} has a value that is not finite", b.id));
        }
    }
    std::vector<const SettlementSite*> ordered;
    for (const SettlementSite& site : sites) ordered.push_back(&site);
    std::sort(ordered.begin(), ordered.end(), [](const SettlementSite* a, const SettlementSite* b) { return a->id < b->id; });
    if (std::adjacent_find(ordered.begin(), ordered.end(), [](const SettlementSite* a, const SettlementSite* b) { return a->id == b->id; }) != ordered.end()) {
        return failure("two settlement sites share an id");
    }
    std::map<std::string, std::vector<const PlacedBuilding*>> byId;
    for (const PlacedBuilding& b : buildings.all()) byId[b.settlementId].push_back(&b);

    PlacedKit out;
    for (const SettlementSite* site : ordered) {
        const std::uint8_t tier = std::min<std::uint8_t>(site->tier, kSettlementTierCount - 1);
        static const std::vector<const PlacedBuilding*> none;
        const auto found = byId.find(site->id);
        const Context context{params, heightfield, roads, water, *site, found == byId.end() ? none : found->second, out.set};
        placeBuildings(context);
        if (tier >= kVillage) placePerimeter(context, tier);
    }
    SettlementKitBuilder::index(out.set, heightfield);
    return out;
}

std::array<bool, kKitPieceCount> kitPiecesUsed(const SettlementKitSet& set)
{
    std::array<bool, kKitPieceCount> used{};
    for (const KitInstance& instance : set.all()) used[static_cast<std::size_t>(instance.piece)] = true;
    return used;
}

bool cookSettlementKit(const std::filesystem::path& folder, const std::array<bool, kKitPieceCount>& wanted, SceneBundle& bundle, KitMeshes& meshes)
{
    std::array<SceneBundle, kKitPieceCount> cooked;
    for (std::size_t i = 0; i < kKitPieceCount; ++i) {
        if (!wanted[i]) continue;
        CookStats stats;
        const std::filesystem::path file = folder / kInfo[i].file;
        if (!cookGltf(file, cooked[i], stats)) {
            logf(LogLevel::Warn, "settlement kit: {} could not be cooked", file.string());
            return false;
        }
        if (cooked[i].meshes.size() != 1) {
            logf(LogLevel::Warn, "settlement kit: {} holds {} meshes, expected one", file.string(), cooked[i].meshes.size());
            return false;
        }
    }
    meshes = KitMeshes{};
    for (std::size_t i = 0; i < kKitPieceCount; ++i) {
        if (!wanted[i]) continue;
        SceneBundle& piece = cooked[i];
        const auto textureBase = static_cast<std::uint32_t>(bundle.textures.size());
        const auto materialBase = static_cast<std::uint32_t>(bundle.materials.size());
        for (Texture& texture : piece.textures) bundle.textures.push_back(std::move(texture));
        for (Material& material : piece.materials) {
            for (std::uint32_t* slot : {&material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture}) {
                if (*slot != kNoTexture) *slot += textureBase;
            }
            bundle.materials.push_back(std::move(material));
        }
        for (Submesh& submesh : piece.meshes[0].submeshes) submesh.material += materialBase;
        meshes.mesh[i] = static_cast<std::uint32_t>(bundle.meshes.size());
        bundle.meshes.push_back(std::move(piece.meshes[0]));
    }
    return true;
}

}
