#include "ichor/world/settlement_kit.h"

#include "ichor/assets/cooker.h"
#include "ichor/core/log.h"
#include "ichor/world/settlement_streets.h"

#include <worldforge/subseed.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <set>

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

constexpr const char* kRoleId[kKitPieceCount] = {
    "SmallHouse", "TempleFront", "ColonnadeBay", "WallTower", "GateBayGrid", "WallSegmentGrid", "PalisadeRunGrid",
};
constexpr const char* kRoleCategory[kKitPieceCount] = {"building", "building", "building", "tower", "gate", "wall", "wall"};

constexpr std::size_t kMaxIdChars = 64;
constexpr double kMaxPieceM = 1000.0;

PlacedKit failure(std::string message)
{
    PlacedKit out;
    out.error = std::move(message);
    return out;
}

double snapToGrid(double v) { return std::floor(v / kKitGridM + 0.5) * kKitGridM; }

struct Context {
    const SettlementKitParams& params;
    const std::array<KitPieceInfo, kKitPieceCount>& infos;
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
    const KitPieceInfo& info = c.infos[static_cast<std::size_t>(piece)];
    const bool turned = (quarter & 1u) != 0;
    const double hw = turned ? info.halfDepthM : info.halfWidthM;
    const double hd = turned ? info.halfWidthM : info.halfDepthM;
    float lowest = 0.0f;
    if (!footprintStandable(x, z, hw, hd, c.field, c.roads, c.water, c.params.ground, lowest)) return;
    if (!clearOfBuildings(c, x, z, hw, hd)) return;
    SettlementKitBuilder::add(c.out, {piece, c.site.id, x, z, lowest - c.params.sinkM, quarter, {}});
}

// Where a facing (quarter turns, the kit's convention) looks, as a unit step in x and z.
void facingStep(std::uint8_t quarter, double& dx, double& dz)
{
    dx = quarter == 1 ? 1.0 : quarter == 3 ? -1.0 : 0.0;
    dz = quarter == 0 ? 1.0 : quarter == 2 ? -1.0 : 0.0;
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
        // A house on a street lot looks at its street; a scattered one looks at the settlement's centre.
        const bool onStreet = b->facing >= 0;
        const auto facing = static_cast<std::uint8_t>(onStreet ? b->facing : alongX ? (dx > 0 ? 1 : 3) : (dz > 0 ? 0 : 2));
        const bool facesX = (facing & 1u) != 0;
        const float acrossHalf = facesX ? b->halfWidthM : b->halfDepthM; // the lot's depth, away from the street
        const float alongHalf = facesX ? b->halfDepthM : b->halfWidthM;  // its frontage
        const float smaller = std::min(b->halfWidthM, b->halfDepthM);
        const float larger = onStreet ? alongHalf : std::max(b->halfWidthM, b->halfDepthM);
        const float ground = b->groundY - c.params.sinkM;
        double stepX = 0.0;
        double stepZ = 0.0;
        facingStep(facing, stepX, stepZ);
        // A piece smaller than its lot stands at the lot's front, so its door is on the street and not behind a yard.
        const auto atFront = [&](KitPiece piece, double x, double z, std::uint8_t quarter) {
            const double back = onStreet ? std::max(0.0, static_cast<double>(acrossHalf) - static_cast<double>(c.infos[static_cast<std::size_t>(piece)].halfDepthM)) : 0.0;
            SettlementKitBuilder::add(c.out, {piece, c.site.id, x + stepX * back, z + stepZ * back, ground, quarter, {}});
        };
        if (b->tier >= kVillage && !templePlaced && smaller >= c.params.minTempleHalfM) {
            templePlaced = true;
            atFront(KitPiece::TempleFront, b->x, b->z, facing);
            continue;
        }
        if (b->tier >= kTown && larger >= c.params.minColonnadeHalfM && pick % 3 == 0) {
            const KitPieceInfo& bay = c.infos[static_cast<std::size_t>(KitPiece::ColonnadeBay)];
            const bool runsAlongX = onStreet ? !facesX : b->halfWidthM >= b->halfDepthM;
            const std::uint8_t quarter = onStreet ? facing : runsAlongX ? (dz > 0 ? 0 : 2) : (dx > 0 ? 1 : 3);
            for (const double side : {-1.0, 1.0}) {
                const double along = side * bay.halfWidthM;
                atFront(KitPiece::ColonnadeBay, b->x + (runsAlongX ? along : 0.0), b->z + (runsAlongX ? 0.0 : along), quarter);
            }
            continue;
        }
        atFront(KitPiece::SmallHouse, b->x, b->z, facing);
    }
}

void placePerimeter(const Context& c, std::uint8_t tier)
{
    const SettlementTierParams& t = c.params.ground.tiers[tier];
    const double half = settlementHalfM(t, c.params.wallMarginM);
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

    const KitManifest& manifest = params.manifest != nullptr ? *params.manifest : builtinKitManifest();
    std::array<KitPieceInfo, kKitPieceCount> infos{};
    for (std::size_t i = 0; i < kKitPieceCount; ++i) {
        const std::size_t at = manifest.role(static_cast<KitPiece>(i));
        if (at >= manifest.pieces.size()) return failure(std::format("the kit manifest has no {} piece", kRoleId[i]));
        const KitManifestPiece& piece = manifest.pieces[at];
        infos[i] = {piece.file.c_str(), piece.halfWidthM, piece.halfDepthM, piece.heightM};
    }

    PlacedKit out;
    for (const SettlementSite* site : ordered) {
        const std::uint8_t tier = std::min<std::uint8_t>(site->tier, kSettlementTierCount - 1);
        static const std::vector<const PlacedBuilding*> none;
        const auto found = byId.find(site->id);
        const Context context{params, infos, heightfield, roads, water, *site, found == byId.end() ? none : found->second, out.set};
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

namespace {

KitManifestResult manifestFailure(std::string message)
{
    KitManifestResult out;
    out.error = "kit manifest: " + std::move(message);
    return out;
}

bool validId(const std::string& id)
{
    if (id.empty() || id.size() > kMaxIdChars) return false;
    return std::all_of(id.begin(), id.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; });
}

bool validFile(const std::string& file)
{
    if (file.size() < 5 || file.size() > 128 || !file.ends_with(".glb") || file.front() == '.' || file.find("..") != std::string::npos) return false;
    return std::all_of(file.begin(), file.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-' || ch == '.'; });
}

bool validSize(const nlohmann::json& value, float& out)
{
    if (!value.is_number()) return false;
    const double v = value.get<double>();
    if (!std::isfinite(v) || v <= 0.0 || v > kMaxPieceM) return false;
    out = static_cast<float>(v);
    return true;
}

KitManifestResult parseEntries(const nlohmann::json& root)
{
    if (!root.is_object()) return manifestFailure("the top level is not an object");
    const auto list = root.find("pieces");
    if (list == root.end() || !list->is_array()) return manifestFailure("\"pieces\" is missing or not an array");
    if (list->size() > kMaxKitManifestPieces) return manifestFailure(std::format("{} pieces, at most {} allowed", list->size(), kMaxKitManifestPieces));
    KitManifestResult out;
    std::set<std::string> ids;
    std::set<std::string> files;
    for (std::size_t n = 0; n < list->size(); ++n) {
        const nlohmann::json& entry = (*list)[n];
        if (!entry.is_object()) return manifestFailure(std::format("piece {} is not an object", n));
        const auto text = [&](const char* key, std::string& dest) {
            const auto found = entry.find(key);
            if (found == entry.end() || !found->is_string()) return false;
            dest = found->get<std::string>();
            return true;
        };
        KitManifestPiece piece;
        if (!text("id", piece.id) || !validId(piece.id)) {
            return manifestFailure(std::format("piece {} has a missing or invalid id (letters, digits and underscores, at most {})", n, kMaxIdChars));
        }
        if (!ids.insert(piece.id).second) return manifestFailure(std::format("{} is listed twice", piece.id));
        if (!text("file", piece.file) || !validFile(piece.file)) return manifestFailure(std::format("{} needs a \"file\" that is a plain .glb name inside the kit folder", piece.id));
        if (!files.insert(piece.file).second) return manifestFailure(std::format("{} reuses the file {}", piece.id, piece.file));
        if (!text("category", piece.category) || !validId(piece.category)) return manifestFailure(std::format("{} has a missing or invalid category", piece.id));
        const auto footprint = entry.find("footprint");
        if (footprint == entry.end() || !footprint->is_object()) return manifestFailure(std::format("{} has no \"footprint\" object", piece.id));
        const auto size = [&](const char* key, float& dest) {
            const auto found = footprint->find(key);
            return found != footprint->end() && validSize(*found, dest);
        };
        if (!size("halfWidthM", piece.halfWidthM) || !size("halfDepthM", piece.halfDepthM) || !size("heightM", piece.heightM)) {
            return manifestFailure(std::format("{} needs a footprint with halfWidthM, halfDepthM and heightM, each above 0 and at most {} m", piece.id, kMaxPieceM));
        }
        out.manifest.pieces.push_back(std::move(piece));
    }
    for (const char* role : kRoleId) {
        if (out.manifest.find(role) < 0) return manifestFailure(std::format("the piece {} that placement needs is missing", role));
    }
    return out;
}

}

int KitManifest::find(std::string_view id) const
{
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (pieces[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

std::size_t KitManifest::role(KitPiece piece) const
{
    const int at = find(kRoleId[std::min<std::size_t>(static_cast<std::size_t>(piece), kKitPieceCount - 1)]);
    return at < 0 ? pieces.size() : static_cast<std::size_t>(at);
}

const KitManifest& builtinKitManifest()
{
    static const KitManifest manifest = [] {
        KitManifest m;
        for (std::size_t i = 0; i < kKitPieceCount; ++i) {
            m.pieces.push_back({kRoleId[i], kInfo[i].file, kRoleCategory[i], kInfo[i].halfWidthM, kInfo[i].halfDepthM, kInfo[i].heightM});
        }
        return m;
    }();
    return manifest;
}

KitManifestResult parseKitManifest(std::string_view text)
{
    if (text.size() > kMaxKitManifestBytes) return manifestFailure(std::format("{} bytes, at most {} allowed", text.size(), kMaxKitManifestBytes));
    try {
        const nlohmann::json root = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
        if (root.is_discarded()) return manifestFailure("not valid JSON");
        return parseEntries(root);
    } catch (const std::exception& e) {
        return manifestFailure(std::format("rejected: {}", e.what()));
    }
}

KitManifestResult loadKitManifest(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) return manifestFailure(std::format("{} cannot be read", file.string()));
    if (size > kMaxKitManifestBytes) return manifestFailure(std::format("{} is {} bytes, at most {} allowed", file.string(), size, kMaxKitManifestBytes));
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && !in.eof()) return manifestFailure(std::format("{} cannot be read", file.string()));
    KitManifestResult result = parseKitManifest(bytes);
    if (!result.ok()) result.error = std::format("{}: {}", file.string(), result.error);
    return result;
}

std::vector<bool> kitManifestPiecesUsed(const SettlementKitSet& set, const KitManifest& manifest)
{
    std::vector<bool> used(manifest.pieces.size(), false);
    for (const KitInstance& instance : set.all()) {
        const std::size_t at = manifest.role(instance.piece);
        if (at < used.size()) used[at] = true;
    }
    return used;
}

namespace {

std::uint64_t hashBytes(const std::vector<std::uint8_t>& data)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint8_t byte : data) h = (h ^ byte) * 1099511628211ull;
    return h;
}

bool sameImage(const Texture& a, const Texture& b)
{
    return a.width == b.width && a.height == b.height && a.mipCount == b.mipCount && a.format == b.format && a.pixels == b.pixels;
}

using MaterialKey = std::tuple<std::string, float, float, float, float, float, float, std::uint32_t, std::uint32_t, std::uint32_t>;

}

bool mergeKitPieces(std::vector<SceneBundle>& pieces, const std::vector<bool>& wanted, SceneBundle& bundle, std::vector<std::uint32_t>& meshIndex, KitMergeStats& stats,
    std::string& error)
{
    SceneBundle staged;
    std::map<std::uint64_t, std::vector<std::size_t>> textureByHash;
    std::map<MaterialKey, std::uint32_t> materialByKey;
    const auto textureBase = static_cast<std::uint32_t>(bundle.textures.size());
    const auto materialBase = static_cast<std::uint32_t>(bundle.materials.size());
    const auto meshBase = static_cast<std::uint32_t>(bundle.meshes.size());
    stats = {};
    meshIndex.assign(pieces.size(), 0);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (i >= wanted.size() || !wanted[i]) continue;
        SceneBundle& piece = pieces[i];
        if (piece.meshes.size() != 1) {
            error = std::format("piece {} holds {} meshes, expected one", i, piece.meshes.size());
            return false;
        }
        std::vector<std::uint32_t> textureMap(piece.textures.size());
        for (std::size_t t = 0; t < piece.textures.size(); ++t) {
            ++stats.texturesIn;
            Texture& texture = piece.textures[t];
            std::vector<std::size_t>& same = textureByHash[hashBytes(texture.pixels)];
            const auto twin = std::find_if(same.begin(), same.end(), [&](std::size_t at) { return sameImage(staged.textures[at], texture); });
            if (twin != same.end()) {
                textureMap[t] = textureBase + static_cast<std::uint32_t>(*twin);
                continue;
            }
            same.push_back(staged.textures.size());
            textureMap[t] = textureBase + static_cast<std::uint32_t>(staged.textures.size());
            staged.textures.push_back(std::move(texture));
        }
        std::vector<std::uint32_t> materialMap(piece.materials.size());
        for (std::size_t m = 0; m < piece.materials.size(); ++m) {
            ++stats.materialsIn;
            Material& material = piece.materials[m];
            for (std::uint32_t* slot : {&material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture}) {
                if (*slot == kNoTexture) continue;
                if (*slot >= textureMap.size()) {
                    error = std::format("material {} points at texture {} but its file holds {}", material.name, *slot, textureMap.size());
                    return false;
                }
                *slot = textureMap[*slot];
            }
            const MaterialKey key{material.name, material.baseColorFactor.x, material.baseColorFactor.y, material.baseColorFactor.z, material.baseColorFactor.w,
                material.metallicFactor, material.roughnessFactor, material.baseColorTexture, material.metallicRoughnessTexture, material.normalTexture};
            const auto [found, added] = materialByKey.try_emplace(key, materialBase + static_cast<std::uint32_t>(staged.materials.size()));
            materialMap[m] = found->second;
            if (added) staged.materials.push_back(std::move(material));
        }
        for (Submesh& submesh : piece.meshes[0].submeshes) {
            if (submesh.material >= materialMap.size()) {
                error = std::format("piece {} has a submesh with material {} but holds {}", i, submesh.material, materialMap.size());
                return false;
            }
            submesh.material = materialMap[submesh.material];
        }
        meshIndex[i] = meshBase + static_cast<std::uint32_t>(staged.meshes.size());
        staged.meshes.push_back(std::move(piece.meshes[0]));
    }
    stats.materialsOut = staged.materials.size();
    stats.texturesOut = staged.textures.size();
    if (bundle.materials.size() + staged.materials.size() > kMaxSceneMaterials) {
        error = std::format("the kit needs {} material slots ({} materials before sharing) and {} are already used, but the renderer holds {} in total",
            staged.materials.size(), stats.materialsIn, bundle.materials.size(), kMaxSceneMaterials);
        return false;
    }
    for (Texture& texture : staged.textures) bundle.textures.push_back(std::move(texture));
    for (Material& material : staged.materials) bundle.materials.push_back(std::move(material));
    for (Mesh& mesh : staged.meshes) bundle.meshes.push_back(std::move(mesh));
    return true;
}

bool cookSettlementKit(const std::filesystem::path& folder, const KitManifest& manifest, const std::vector<bool>& wanted, SceneBundle& bundle, KitMeshes& meshes)
{
    std::vector<SceneBundle> cooked(manifest.pieces.size());
    for (std::size_t i = 0; i < manifest.pieces.size(); ++i) {
        if (i >= wanted.size() || !wanted[i]) continue;
        CookStats stats;
        const std::filesystem::path file = folder / manifest.pieces[i].file;
        if (!cookGltf(file, cooked[i], stats)) {
            logf(LogLevel::Warn, "settlement kit: {} ({}) could not be cooked", manifest.pieces[i].id, file.string());
            return false;
        }
        if (cooked[i].meshes.size() != 1) {
            logf(LogLevel::Warn, "settlement kit: {} holds {} meshes, expected one", file.string(), cooked[i].meshes.size());
            return false;
        }
    }
    std::vector<std::uint32_t> index;
    KitMergeStats stats;
    std::string error;
    if (!mergeKitPieces(cooked, wanted, bundle, index, stats, error)) {
        logf(LogLevel::Error, "settlement kit: {}", error);
        return false;
    }
    logf(LogLevel::Info, "settlement kit: {} materials share {} slots, {} textures share {}", stats.materialsIn, stats.materialsOut, stats.texturesIn, stats.texturesOut);
    meshes = KitMeshes{};
    meshes.byManifest = index;
    for (std::size_t r = 0; r < kKitPieceCount; ++r) {
        const std::size_t at = manifest.role(static_cast<KitPiece>(r));
        if (at < index.size()) meshes.mesh[r] = index[at];
    }
    return true;
}

bool cookSettlementKit(const std::filesystem::path& folder, const std::array<bool, kKitPieceCount>& wanted, SceneBundle& bundle, KitMeshes& meshes)
{
    const KitManifest& manifest = builtinKitManifest();
    std::vector<bool> flags(manifest.pieces.size(), false);
    for (std::size_t r = 0; r < kKitPieceCount; ++r) {
        if (wanted[r]) flags[manifest.role(static_cast<KitPiece>(r))] = true;
    }
    return cookSettlementKit(folder, manifest, flags, bundle, meshes);
}

}
