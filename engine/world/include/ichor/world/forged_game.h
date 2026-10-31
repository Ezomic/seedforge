#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_biome.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_navmesh.h"
#include "ichor/world/forged_region_tiers.h"
#include "ichor/world/forged_roads.h"
#include "ichor/world/forged_scenery_scatter.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/heightfield.h"
#include "ichor/data/data_set.h"
#include "ichor/data/world_config.h"
#include "ichor/graph/quest_expansion.h"
#include "ichor/world/map_layers.h"
#include "ichor/world/placed_cast.h"
#include "ichor/world/placed_dungeons.h"
#include "ichor/world/placed_loot.h"
#include "ichor/world/placed_quests.h"
#include "ichor/world/placed_pois.h"
#include "ichor/world/placed_settlements.h"
#include "ichor/world/settlement_kit.h"
#include "ichor/world/settlement_streets.h"
#include "ichor/world/water_field.h"

#include <worldforge/forge_world.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ichor {

// The forged world as the app plays it (doc 08 phase 12, the integration of the bake batch): one place
// that turns a finished ForgeJob into everything the app and the streamer read, so main.cpp only hands
// these pieces to the streamer, the renderer and the map screen. The stand-in world never goes through
// here; the app builds one of these only behind --forged.
//
// Every piece is a pure function of the forged world, so one seed and grid size always give the same
// game, whatever thread forged it and however it was watched. stateHash() folds the pieces into one
// number the determinism tests compare.

struct ForgedSpawn {
    bool found = false; // false: no dry, gentle ground was found and (x, z) is the world centre
    double x = 0.0;
    double z = 0.0;
    float groundY = 0.0f;
    double faceX = 0.0; // the point the search aimed at (the settlement, else the world centre): where the view looks
    double faceZ = 0.0;
};

struct ForgedSpawnParams {
    float searchStepMetres = 4.0f;
    float edgeMarginMetres = 32.0f;     // the spawn keeps this far from the world's edge
    float footprintMetres = 6.0f;       // ground within this radius must be dry and gentle
    float minUpY = 0.94f;               // the surface normal's y at and around the spawn (about 20 degrees)
    float minAboveSeaMetres = 1.0f;     // ground height over the water level
};

// Dry land with low slope nearest the target (the settlement closest to the world centre, else the
// centre). `water` and `blocked` are optional: water rules out any wet ground in the footprint, and
// blocked (world x, z) rules out a position (a building, say). Deterministic: the first ring, in
// square rings around the target, that holds a valid point, and the nearest point of that ring.
ForgedSpawn findForgedSpawn(const Heightfield& heightfield, const WaterField* water, const std::vector<std::pair<double, double>>& settlements,
    const std::function<bool(double, double)>& blocked = {}, const ForgedSpawnParams& params = {});

// What the manifest-driven content needs from the data layer: the item catalogue the loot is placed from, the
// dungeon skeletons, the quest rules and the cast's schedules. A default-built value places none of them.
struct ForgedContentInputs {
    std::vector<LootItemInfo> lootCatalogue;
    std::shared_ptr<const DungeonSkeletonCatalog> skeletons;
    std::shared_ptr<const graph::quest::Ruleset> questRules;
    PlacedCastParams cast;
};

struct ForgedContentSetup {
    ForgedContentInputs inputs;
    worldforge::ForgeConfig config; // names the items, cast archetypes and dungeon themes, so the forged manifest has them
    std::vector<std::string> problems; // what could not be loaded; the content that needs it is left out
};

// Reads the catalogue (tables notable_items and catalogue), the skeletons (dungeon_skeletons/) and the quest rules
// (quests/expansion_rules.json). The cast archetypes are a fixed list: the shipped data has no table for them yet.
ForgedContentSetup loadForgedContentSetup(const DataSet& data, const std::filesystem::path& dataDir);

// The building tiers the forged settlements are laid out and filled with: the placement defaults, with the counts a street
// grid carries (a hamlet 4 houses, a village 12, a town 30, a city 64).
PlacedSettlementParams forgedStreetTierParams();

struct ForgedGame {
    ForgedGame() = default;
    ForgedGame(const ForgedGame&) = delete;
    ForgedGame& operator=(const ForgedGame&) = delete;

    WorldDesc desc; // forgedWorldDesc of the requested one: the height range and water level of the forge
    std::shared_ptr<const WorldConfig> worldConfig; // doc 08 phase 0, the seed's axioms; carried only, so it is not in stateHash. Null when they did not load
    std::shared_ptr<const worldforge::ForgedWorld> world;
    Heightfield heightfield; // the roads phase's terrain
    std::shared_ptr<const ForgedBiomeField> biomes;
    ForgedWater water; // the forged rivers and lakes as the engine's Hydrology, with the parameters to pair it with
    std::unique_ptr<WaterField> waterField; // over `heightfield` and `water`; what the player wades through
    std::shared_ptr<const ForgedRoads> roads;
    std::shared_ptr<const PlacedPoiSet> pois;
    std::shared_ptr<const PlacedBuildingSet> buildings;
    std::vector<SettlementSite> sites; // the settlements in the forge's order, in world metres
    std::shared_ptr<const SettlementStreetSet> streets; // the streets inside the settlements, and the lots the buildings front
    std::shared_ptr<const SettlementKitSet> kit; // the textured pieces that stand in for the buildings
    std::shared_ptr<const NavSurface> navSurface; // water and roads as the navmesh sees them; reads `waterField` and `roads`
    ForgedRegionTiers tiers;
    std::shared_ptr<const PlacedLootSet> loot; // the manifest's items, lying at their dungeons and landmarks; null without content inputs
    std::shared_ptr<const PlacedCastSet> cast; // the manifest's cast, near their home settlements, with schedules
    std::shared_ptr<const PlacedQuestSet> quests;
    std::shared_ptr<const DungeonSkeletonCatalog> skeletons;
    std::unique_ptr<DungeonLayouts> dungeons; // bakes a layout on first ask; one owner, the game thread
    MapLayerStack mapLayers;
    ForgedSpawn spawn;
    std::string error; // empty on success
    std::vector<std::string> contentProblems; // loot, cast or quests that could not be placed and were left out

    bool ok() const { return error.empty(); }

    // A digest of the forged world's hash, the terrain, the spawn, the placed objects, the road count and the
// region tiers, loot, cast and quests: the same seed and grid always give the same value.
    std::uint64_t stateHash() const;
};

// The grey-box meshes the app draws the placed world with: one road ribbon per cell that has a road, a
// pillar for each point of interest and one unit box for the buildings (scaled per building).
// Mesh indices are positions in `bundle.meshes`; add the renderer's mesh count from before the upload.
struct ForgedScenery {
    SceneBundle bundle;
    std::map<CellId, std::uint32_t> roadMesh;
    std::map<CellId, std::uint32_t> streetMesh; // the settlement streets of a cell, with the road's material
    std::map<std::string, std::uint32_t> poiMesh; // by PlacedPoi::id, at its own height with its base on y = 0
    bool kitLoaded = false;                  // the textured kit cooked into `bundle`; when false the buildings are drawn as boxes
    KitMeshes kitMesh;                       // by KitPiece, valid when kitLoaded
    std::uint32_t lootMesh = 0;     // a unit box, drawn small on the ground where an item lies
    std::uint32_t castMesh = 0;     // a unit box, scaled to a person
    std::uint32_t buildingMesh = 0;          // a box 1 m wide and deep and 1 m tall: scale by 2 * half extents and the height
};

ForgedScenery buildForgedScenery(const ForgedGame& game);

// Cooks the settlement kit (the GLBs under `folder`) into the scenery bundle, only the pieces the game uses. On a
// failure (folder missing, a file does not cook) the scenery is left as it was and the buildings stay boxes.
bool addSettlementKit(ForgedScenery& scenery, const ForgedGame& game, const std::filesystem::path& folder, bool useVariants = false);

// What the biome scenery keeps clear of in this game: roads (with their shoulders), the settlements' buildings and
// open water. The guard reads `game`'s roads and water, so the game must outlive it.
SceneryGuard forgedSceneryGuard(const ForgedGame& game);

// Moves the spawn to dry, gentle ground nearest the middle of settlement `index` (in the forge's order), clear of its
// buildings and kit pieces, and aims the view at it. Returns false, leaving the spawn alone, for an index past the end.
bool spawnAtSettlement(ForgedGame& game, std::size_t index);

// The spawn spawnAtSettlement would pick for settlement `index` (which must be below game.sites.size()), without moving game.spawn.
ForgedSpawn settlementSpawn(const ForgedGame& game, std::size_t index);

// Moves the spawn to dry, gentle ground inside `biome`, as near the world's middle as such ground is, and faces it
// north. Returns false, leaving the spawn alone, when the world has no such ground.
bool spawnInBiome(ForgedGame& game, worldforge::BiomeId biome);

// Builds the game from a finished forge. Fails, with a message, when the result is empty or a bake step
// refuses the world. Never throws.
std::unique_ptr<ForgedGame> buildForgedGame(const ForgeJobResult& result, const WorldDesc& requested, const ForgedContentInputs& content = {});

}
