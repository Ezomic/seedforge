#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/world/placed_settlements.h"

#include <glm/mat3x3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ichor {

// The textured settlement kit (art/settlement_kit) standing where the grey boxes of placed_settlements stood.
//
// Every placed building becomes one kit piece (or two bays of colonnade): a small house, the settlement's
// temple, or a colonnade in the bigger towns. Villages, towns and cities also get a perimeter: a palisade
// for a village, stone wall with corner and spaced towers and a gate bay at the middle of each side for a
// town or city. The wall pieces are the Grid variants and sit on the 4 m grid: bay centres at 4k + 2 along
// the run, run lines and tower centres on multiples of 4. A piece is kept only where the ground is dry,
// gentle, clear of every road (a margin included) and clear of the settlement's buildings, so a road or a
// stream that crosses the perimeter leaves a gap and no piece is forced.
//
// The plain pieces (house, temple, colonnade, tower) are not grid sized: they measure 3.9 to 4.2 m (the
// palisade run 5.4 m, which is why only its Grid variant is used) and stand unsnapped on the building's own
// centre. Pieces are never scaled. Everything depends only on the world seed, the sites, the buildings and
// the terrain, never on the order settlements or cells are visited in.

enum class KitPiece : std::uint8_t {
    SmallHouse,
    TempleFront,
    ColonnadeBay,
    WallTower,
    GateBayGrid,
    WallSegmentGrid,
    PalisadeRunGrid,
};

constexpr std::size_t kKitPieceCount = 7;
constexpr double kKitGridM = 4.0;

struct KitPieceInfo {
    const char* file;    // under the kit folder
    float halfWidthM;    // along the piece's own x, as exported
    float halfDepthM;    // along the piece's own z
    float heightM;
};

// Measured from the exported GLBs (every piece is centred on the origin in x and z with its base on y = 0).
const KitPieceInfo& kitPieceInfo(KitPiece piece);

// Turns a piece about the vertical in exact quarter turns (no libm): quarter 0 leaves it, 1 sends +x to -z.
glm::mat3 kitBasis(std::uint8_t quarterTurns);

struct KitInstance {
    KitPiece piece = KitPiece::SmallHouse;
    std::string settlementId;
    double x = 0.0; // world metres, the piece's origin
    double z = 0.0;
    float groundY = 0.0f; // where the piece's base stands
    std::uint8_t quarterTurns = 0;
    CellId cell;          // the cell the origin lies in
};

class SettlementKitSet {
public:
    const std::vector<KitInstance>& all() const { return instances_; }
    bool empty() const { return instances_.empty(); }
    // The pieces standing in `id`, in placement order; empty for a cell with none or outside the world.
    std::vector<KitInstance> inCell(CellId id) const;

private:
    friend struct SettlementKitBuilder;
    std::vector<KitInstance> instances_;
    std::map<CellId, std::vector<std::size_t>> byCell_;
};

struct SettlementKitParams {
    float sinkM = 0.2f;           // a piece stands this far below the lowest ground under it
    float wallMarginM = 8.0f;     // the perimeter stands this far beyond the buildings' disc
    float towerSpacingM = 24.0f;  // stone walls get a tower this often along a side (a multiple of the grid)
    float buildingGapM = 1.5f;    // clearance between a wall piece and a building
    float minTempleHalfM = 4.0f;  // a building at least this half-extent both ways can hold the temple
    float minColonnadeHalfM = 4.1f;
    PlacedSettlementParams ground; // the slope, road margin and water rules; defaults match the buildings'
};

struct PlacedKit {
    SettlementKitSet set;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// Fails, with a message, on an empty heightfield or non-finite site or building values.
PlacedKit placeSettlementKit(const std::vector<SettlementSite>& sites, const PlacedBuildingSet& buildings, const Heightfield& heightfield, const RoadField* roads,
    const WaterField* water, const SettlementKitParams& params = {});

// Which pieces the set uses, for cooking only those.
std::array<bool, kKitPieceCount> kitPiecesUsed(const SettlementKitSet& set);

// Where each cooked piece landed in a bundle: indices into SceneBundle::meshes.
struct KitMeshes {
    std::array<std::uint32_t, kKitPieceCount> mesh{};
};

// Cooks the pieces marked in `wanted` through the glTF cooker and appends them to `bundle` (textures, materials
// and meshes, rebased). Pieces not wanted keep mesh index 0. Returns false, with `bundle` untouched, when a file
// is missing, fails to cook, or does not hold exactly one mesh.
bool cookSettlementKit(const std::filesystem::path& folder, const std::array<bool, kKitPieceCount>& wanted, SceneBundle& bundle, KitMeshes& meshes);

}
