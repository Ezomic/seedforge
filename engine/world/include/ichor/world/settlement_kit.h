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
#include <string_view>
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

// The kit as data: one entry per piece, read from kit_manifest.json next to the GLBs. The seven pieces placement
// knows by role (KitPiece) must be listed under their role id; any further entry is cooked and reachable by its id
// without a code change (cookSettlementKit returns its mesh in KitMeshes::byManifest). An entry with "variantOf": "<role id>"
// is a look-alike for that role: its footprint must fit inside the role's, and it is cooked and drawn in the role's place
// only when the kit is asked for variants (addSettlementKit's useVariants, the app's --kit-variants), so a default run is unchanged.
// Settlements are placed before the kit folder is known, so with the built-in footprints; addSettlementKit refuses a
// manifest whose role footprints differ. The built-in manifest is those seven, so a kit folder with no manifest behaves as before.
struct KitManifestPiece {
    std::string id;       // letters, digits and underscores; the role pieces use the KitPiece names
    std::string file;     // a .glb directly under the kit folder
    std::string category; // a free label such as building, wall, tower, gate
    float halfWidthM = 0.0f;
    float halfDepthM = 0.0f;
    float heightM = 0.0f;
    std::string variantOf; // empty, or the id of the role piece this one may stand in for (see KitMeshes::variants)
};

struct KitManifest {
    std::vector<KitManifestPiece> pieces;

    // The index of the piece with this id, or -1.
    int find(std::string_view id) const;
    // The index of a role piece; every valid manifest has all seven.
    std::size_t role(KitPiece piece) const;
};

constexpr const char* kKitManifestFile = "kit_manifest.json";
constexpr std::size_t kMaxKitManifestBytes = 1u << 20;
constexpr std::size_t kMaxKitManifestPieces = 128;

const KitManifest& builtinKitManifest();

struct KitManifestResult {
    KitManifest manifest;
    std::string error; // empty on success; names the entry and the rule that failed

    bool ok() const { return error.empty(); }
};

// Never throws or crashes on any bytes: bad JSON, wrong types, a duplicate id, a path that leaves the kit folder, a
// non-finite or non-positive size, a missing role piece and an oversized file are all errors.
KitManifestResult parseKitManifest(std::string_view text);
KitManifestResult loadKitManifest(const std::filesystem::path& file);

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
    const KitManifest* manifest = nullptr; // the footprints placement uses; null means the built-in manifest
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
    std::array<std::uint32_t, kKitPieceCount> mesh{}; // by KitPiece
    std::vector<std::uint32_t> byManifest;            // by position in the manifest the kit was cooked with
    std::array<std::vector<std::uint32_t>, kKitPieceCount> variants; // by KitPiece: the cooked look-alikes of that role, in manifest order
};

// The mesh to draw for a placed piece: one of the role's variants chosen from the piece's position (so a settlement's
// houses differ from each other and the same world always draws the same), else the role's own mesh.
std::uint32_t kitMeshFor(const KitMeshes& meshes, const KitInstance& instance);

// Cooks the pieces marked in `wanted` through the glTF cooker and appends them to `bundle` (textures, materials
// and meshes, rebased). Pieces not wanted keep mesh index 0. Returns false, with `bundle` untouched, when a file
// is missing, fails to cook, does not hold exactly one mesh, or the materials would not fit the renderer.
bool cookSettlementKit(const std::filesystem::path& folder, const std::array<bool, kKitPieceCount>& wanted, SceneBundle& bundle, KitMeshes& meshes);

// The same for any manifest: `wanted` has one flag per manifest piece.
bool cookSettlementKit(const std::filesystem::path& folder, const KitManifest& manifest, const std::vector<bool>& wanted, SceneBundle& bundle, KitMeshes& meshes);

// Which manifest pieces the set uses (the role pieces it places).
// With `variants`, the look-alikes of every used role count as used too.
std::vector<bool> kitManifestPiecesUsed(const SettlementKitSet& set, const KitManifest& manifest, bool variants = false);

// How the appended kit used the renderer's material slots, filled by a successful merge.
struct KitMergeStats {
    std::size_t materialsIn = 0;     // materials across the cooked pieces
    std::size_t materialsOut = 0;    // slots they take after sharing
    std::size_t texturesIn = 0;
    std::size_t texturesOut = 0;
};

// The merge step of cookSettlementKit: appends one cooked bundle per wanted piece (`pieces[i]` holds one mesh) to
// `bundle`, sharing a material slot between materials of the same name, factors and texture set, and a texture
// between identical images. Fails, with `error` set and `bundle` untouched, when the total would pass
// kMaxSceneMaterials. `meshIndex[i]` is where piece i landed.
bool mergeKitPieces(std::vector<SceneBundle>& pieces, const std::vector<bool>& wanted, SceneBundle& bundle, std::vector<std::uint32_t>& meshIndex, KitMergeStats& stats,
    std::string& error);

}
