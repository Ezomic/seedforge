#include "ichor/world/forged_game.h"

#include "ichor/core/hash.h"
#include "ichor/core/log.h"
#include "ichor/world/road_surface.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace ichor {

PlacedSettlementParams forgedStreetTierParams()
{
    PlacedSettlementParams params;
    constexpr int kBuildings[kSettlementTierCount] = {4, 12, 30, 64};
    for (std::size_t t = 0; t < kSettlementTierCount; ++t) params.tiers[t].buildings = kBuildings[t];
    return params;
}

namespace {

bool gentleAndDry(const Heightfield& heightfield, const WaterField* water, const std::function<bool(double, double)>& blocked, double x, double z,
    const ForgedSpawnParams& params)
{
    const WorldDesc& desc = heightfield.desc();
    const double size = desc.sizeMetres;
    const double footprint = params.footprintMetres;
    const float seaFloor = desc.hasWater() ? desc.waterLevel() + params.minAboveSeaMetres : -std::numeric_limits<float>::max();
    constexpr int kTaps = 8;
    for (int tap = -1; tap < kTaps; ++tap) {
        double px = x;
        double pz = z;
        if (tap >= 0) {
            static constexpr double kDx[kTaps] = {1.0, 0.7071, 0.0, -0.7071, -1.0, -0.7071, 0.0, 0.7071};
            static constexpr double kDz[kTaps] = {0.0, 0.7071, 1.0, 0.7071, 0.0, -0.7071, -1.0, -0.7071};
            px += kDx[tap] * footprint;
            pz += kDz[tap] * footprint;
        }
        if (!(px >= 0.0 && pz >= 0.0 && px <= size && pz <= size)) {
            return false;
        }
        if (!(heightfield.heightAt(px, pz) >= seaFloor) || !(heightfield.normalAt(px, pz).y >= params.minUpY)) {
            return false;
        }
        if (water != nullptr && water->at(px, pz).wet()) {
            return false;
        }
    }
    return !(blocked && blocked(x, z));
}

}

ForgedSpawn findForgedSpawn(const Heightfield& heightfield, const WaterField* water, const std::vector<std::pair<double, double>>& settlements,
    const std::function<bool(double, double)>& blocked, const ForgedSpawnParams& params)
{
    ForgedSpawn spawn;
    if (heightfield.empty() || !(params.searchStepMetres > 0.0f) || !std::isfinite(params.searchStepMetres) || !(params.edgeMarginMetres >= 0.0f)) {
        return spawn;
    }
    const double size = heightfield.desc().sizeMetres;
    const double centre = size * 0.5;
    double targetX = centre;
    double targetZ = centre;
    double best = std::numeric_limits<double>::max();
    for (const auto& [sx, sz] : settlements) {
        const double d = (sx - centre) * (sx - centre) + (sz - centre) * (sz - centre);
        if (std::isfinite(d) && d < best) {
            best = d;
            targetX = sx;
            targetZ = sz;
        }
    }
    spawn.faceX = targetX;
    spawn.faceZ = targetZ;
    spawn.x = centre;
    spawn.z = centre;
    spawn.groundY = heightfield.heightAt(centre, centre);

    const double step = params.searchStepMetres;
    const double margin = params.edgeMarginMetres;
    constexpr double kMaxRings = 1.0e5;
    const int maxRing = static_cast<int>(std::min(size / step, kMaxRings)) + 1;
    // Only the points inside the margin are visited, so a target near an edge does not walk the empty ring beyond it.
    const auto lowest = [&](double target) { return static_cast<int>(std::max(std::ceil((margin - target) / step), -static_cast<double>(maxRing))); };
    const auto highest = [&](double target) { return static_cast<int>(std::min(std::floor((size - margin - target) / step), static_cast<double>(maxRing))); };
    const int minIx = lowest(targetX);
    const int maxIx = highest(targetX);
    const int minIz = lowest(targetZ);
    const int maxIz = highest(targetZ);
    for (int ring = 0; ring <= maxRing; ++ring) {
        double bestDistance = std::numeric_limits<double>::max();
        double bestX = 0.0;
        double bestZ = 0.0;
        bool any = false;
        for (int iz = std::max(-ring, minIz); iz <= std::min(ring, maxIz); ++iz) {
            const int stride = std::abs(iz) == ring ? 1 : std::max(2 * ring, 1);
            for (int ix = -ring; ix <= ring; ix += stride) {
                if (ix < minIx || ix > maxIx) {
                    continue;
                }
                const double x = targetX + ix * step;
                const double z = targetZ + iz * step;
                if (x < margin || z < margin || x > size - margin || z > size - margin) {
                    continue;
                }
                const double distance = static_cast<double>(ix) * ix + static_cast<double>(iz) * iz;
                if (any && distance >= bestDistance) {
                    continue;
                }
                if (gentleAndDry(heightfield, water, blocked, x, z, params)) {
                    any = true;
                    bestDistance = distance;
                    bestX = x;
                    bestZ = z;
                }
            }
        }
        if (any) {
            spawn.found = true;
            spawn.x = bestX;
            spawn.z = bestZ;
            spawn.groundY = heightfield.heightAt(bestX, bestZ);
            return spawn;
        }
    }
    return spawn;
}

// Turns the spawn's view toward the most to see: sixteen headings, each scored by how far the eye sees along it
// before the ground rises above eye level, plus a bonus for a place of interest or a road ahead.
static void chooseForgedView(ForgedGame& game)
{
    ForgedSpawn& spawn = game.spawn;
    if (!spawn.found || game.heightfield.empty()) {
        return;
    }
    constexpr int kHeadings = 16;
    constexpr double kReach = 300.0;
    const double eye = spawn.groundY + 1.6;
    double bestScore = -1.0;
    double bestX = 0.0;
    double bestZ = -1.0;
    for (int i = 0; i < kHeadings; ++i) {
        const double angle = 6.283185307179586 * i / kHeadings;
        const double dx = std::sin(angle);
        const double dz = -std::cos(angle);
        double seen = 0.0;
        for (double d = 10.0; d <= kReach; d += 10.0) {
            const double x = spawn.x + dx * d;
            const double z = spawn.z + dz * d;
            if (x < 0.0 || z < 0.0 || x > game.desc.sizeMetres || z > game.desc.sizeMetres || game.heightfield.heightAt(x, z) > eye) {
                break;
            }
            seen = d;
        }
        double bonus = 0.0;
        if (game.pois) {
            for (const PlacedPoi& poi : game.pois->all()) {
                const double px = poi.x - spawn.x;
                const double pz = poi.z - spawn.z;
                const double distance = std::hypot(px, pz);
                if (distance > 8.0 && distance < 500.0 && (px * dx + pz * dz) / distance > 0.94) {
                    bonus = std::max(bonus, 150.0);
                }
            }
        }
        if (game.roads) {
            for (double d = 10.0; d <= 120.0; d += 10.0) {
                if (game.roads->coverageAt(spawn.x + dx * d, spawn.z + dz * d) > 0.5) {
                    bonus = std::max(bonus, 100.0);
                    break;
                }
            }
        }
        const double score = seen + bonus;
        if (score > bestScore) {
            bestScore = score;
            bestX = dx;
            bestZ = dz;
        }
    }
    spawn.faceX = spawn.x + bestX * 100.0;
    spawn.faceZ = spawn.z + bestZ * 100.0;
}

std::uint64_t ForgedGame::stateHash() const
{
    std::uint64_t h = hashCombine(desc.seed, world ? world->worldHash() : 0);
    h = hashCombine(h, heightfield.digest());
    h = hashCombine(h, spawn.found ? 1 : 0);
    h = hashCombine(h, std::bit_cast<std::uint64_t>(spawn.x));
    h = hashCombine(h, std::bit_cast<std::uint64_t>(spawn.z));
    h = hashCombine(h, roads ? static_cast<std::uint64_t>(roads->segmentCount()) : 0);
    h = hashCombine(h, pois ? static_cast<std::uint64_t>(pois->all().size()) : 0);
    h = hashCombine(h, buildings ? static_cast<std::uint64_t>(buildings->all().size()) : 0);
    if (pois) {
        for (const PlacedPoi& poi : pois->all()) {
            h = hashCombine(h, hash64(poi.id));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(poi.x));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(poi.z));
        }
    }
    if (buildings) {
        for (const PlacedBuilding& building : buildings->all()) {
            h = hashCombine(h, hash64(building.id));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(building.x));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(building.z));
        }
    }
    if (loot) {
        for (const PlacedLoot& item : loot->all()) {
            h = hashCombine(h, hash64(item.itemId));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(item.x));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(item.z));
        }
    }
    if (cast) {
        for (const NpcSpawn& npc : cast->all()) {
            h = hashCombine(h, hash64(npc.npcId));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(npc.x));
            h = hashCombine(h, std::bit_cast<std::uint64_t>(npc.z));
            h = hashCombine(h, scheduleDigest(npc.schedule));
        }
    }
    if (quests) {
        for (const QuestInstance& quest : quests->all()) {
            h = hashCombine(h, quest.graphDigest);
        }
    }
    for (const std::uint8_t tier : tiers.tiers) {
        h = hashCombine(h, tier);
    }
    if (water.hydrology) {
        h = hashCombine(h, water.hydrology->side);
        h = hashCombine(h, static_cast<std::uint64_t>(water.rivers.riverThreshold));
    }
    return h;
}

std::unique_ptr<ForgedGame> buildForgedGame(const ForgeJobResult& result, const WorldDesc& requested, const ForgedContentInputs& content)
{
    auto game = std::make_unique<ForgedGame>();
    if (!result.world) {
        game->error = "the forge produced no world";
        return game;
    }
    game->world = result.world;
    game->desc = forgedWorldDesc(requested);
    const worldforge::ForgedWorld& world = *game->world;

    ForgedHeightfield terrain = heightfieldFromForgedRoads(world, game->desc);
    if (!terrain.ok()) {
        game->error = "terrain: " + terrain.error;
        return game;
    }
    game->heightfield = std::move(terrain.field);

    ForgedBiomes biomes = biomesFromForgedWorld(world, game->desc);
    if (!biomes.ok()) {
        game->error = "biomes: " + biomes.error;
        return game;
    }
    game->biomes = std::make_shared<const ForgedBiomeField>(std::move(biomes.field));

    game->water = waterFromForgedWorld(world, game->heightfield);
    if (!game->water.ok()) {
        game->error = "water: " + game->water.error;
        return game;
    }
    game->waterField = std::make_unique<WaterField>(game->heightfield, game->water.hydrology, game->water.rivers);

    game->roads = std::make_shared<const ForgedRoads>(world, game->desc);
    game->navSurface = std::make_shared<const ForgedNavSurface>(*game->waterField, game->roads.get());

    PlacedPois pois = placeForgedPois(world, game->heightfield);
    if (!pois.ok()) {
        game->error = "points of interest: " + pois.error;
        return game;
    }
    game->pois = std::make_shared<const PlacedPoiSet>(std::move(pois.set));

    std::string siteError;
    if (!forgedSettlementSites(world, game->heightfield, game->sites, siteError)) {
        game->error = "settlements: " + siteError;
        return game;
    }
    const PlacedSettlementParams streetTiers = forgedStreetTierParams();
    SettlementStreets streets = layoutSettlementStreets(game->sites, game->heightfield, streetTiers);
    if (!streets.ok()) {
        game->error = "settlement streets: " + streets.error;
        return game;
    }
    game->streets = std::make_shared<const SettlementStreetSet>(std::move(streets.set));

    PlacedSettlements settlements = placeForgedSettlements(world, game->heightfield, game->roads.get(), game->waterField.get(), streetTiers, game->streets.get());
    if (!settlements.ok()) {
        game->error = "settlements: " + settlements.error;
        return game;
    }
    game->buildings = std::make_shared<const PlacedBuildingSet>(std::move(settlements.set));

    PlacedKit kit = placeSettlementKit(game->sites, *game->buildings, game->heightfield, game->roads.get(), game->waterField.get());
    if (!kit.ok()) {
        game->error = "settlement kit: " + kit.error;
        return game;
    }
    game->kit = std::make_shared<const SettlementKitSet>(std::move(kit.set));

    game->tiers = forgedRegionTiers(world, game->desc);
    if (!game->tiers.ok()) {
        game->error = "region tiers: " + game->tiers.error;
        return game;
    }
    if (!content.lootCatalogue.empty() && world.manifest()) {
        // Content is optional: one that cannot be placed is left out and named, the terrain and the rest still play.
        PlacedLootResult loot = placeForgedLoot(world, *game->pois, content.lootCatalogue, game->heightfield);
        if (loot.ok()) {
            game->loot = std::make_shared<const PlacedLootSet>(std::move(loot.set));
        } else {
            game->contentProblems.push_back("loot: " + loot.error);
        }
    }
    if (world.manifest() && !world.manifest()->cast.empty()) {
        PlacedCast cast = placeForgedCast(world, *game->pois, game->heightfield, content.cast);
        if (cast.ok()) {
            game->cast = std::make_shared<const PlacedCastSet>(std::move(cast.set));
        } else {
            game->contentProblems.push_back("cast: " + cast.error);
        }
    }
    if (content.questRules && world.manifest()) {
        PlacedQuests quests = placeManifestQuests(*world.manifest(), *content.questRules);
        if (quests.ok()) {
            game->quests = std::make_shared<const PlacedQuestSet>(std::move(quests.set));
        } else {
            game->contentProblems.push_back("quests: " + quests.error);
        }
    }
    if (content.skeletons && world.manifest()) {
        game->skeletons = content.skeletons;
        game->dungeons = std::make_unique<DungeonLayouts>(*world.manifest(), *game->skeletons);
    }
    game->mapLayers = MapLayerStack(renderMapImage(game->heightfield), world);

    std::vector<std::pair<double, double>> towns;
    for (const PlacedPoi& poi : game->pois->all()) {
        if (poi.kind == PlacedPoiKind::Settlement) {
            towns.emplace_back(poi.x, poi.z);
        }
    }
    const PlacedBuildingSet& buildings = *game->buildings;
    constexpr double kClearM = 4.0;
    constexpr double kPoiClearM = 30.0; // a marker pillar is tall: not the first thing the player sees
    game->spawn = findForgedSpawn(game->heightfield, game->waterField.get(), towns, [&](double x, double z) {
        for (const PlacedPoi& poi : game->pois->all()) {
            if (std::hypot(x - poi.x, z - poi.z) < kPoiClearM) {
                return true;
            }
        }
        for (const PlacedBuilding& b : buildings.all()) {
            if (std::abs(x - b.x) < b.halfWidthM + kClearM && std::abs(z - b.z) < b.halfDepthM + kClearM) {
                return true;
            }
        }
        return false;
    });
    chooseForgedView(*game);
    return game;
}

ForgedContentSetup loadForgedContentSetup(const DataSet& data, const std::filesystem::path& dataDir)
{
    ForgedContentSetup setup;
    for (const char* tableName : {"notable_items", "catalogue"}) {
        const DataTable* table = data.table(tableName);
        if (table == nullptr) {
            setup.problems.push_back(std::string("there is no ") + tableName + " table, so those items are not placed");
            continue;
        }
        for (const auto& [id, record] : table->records) {
            if (std::find(setup.config.itemIds.begin(), setup.config.itemIds.end(), id) != setup.config.itemIds.end()) {
                setup.problems.push_back("item " + id + " is in two tables; the second is not placed");
                continue;
            }
            const auto text = [&](const char* key) {
                const auto it = record.fields.find(key);
                return it != record.fields.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : std::string{};
            };
            setup.inputs.lootCatalogue.push_back({id, text("tier"), text("class")});
            setup.config.itemIds.push_back(id);
        }
    }
    setup.config.npcArchetypes = {"smith", "priest", "merchant", "ferryman", "healer", "guard"};

    auto skeletons = std::make_shared<DungeonSkeletonCatalog>(DungeonSkeletonCatalog::loadDirectory(dataDir / "dungeon_skeletons"));
    for (const std::string& error : skeletons->errors()) {
        setup.problems.push_back("dungeon skeleton: " + error);
    }
    for (const DungeonSkeleton& skeleton : skeletons->all()) {
        auto& themes = setup.config.dungeonArchetypes;
        if (std::find(themes.begin(), themes.end(), skeleton.theme) == themes.end()) {
            themes.push_back(skeleton.theme);
        }
    }
    setup.inputs.skeletons = std::move(skeletons);

    graph::quest::Ruleset rules = graph::quest::loadRuleset(dataDir / "quests" / "expansion_rules.json");
    if (rules.error.empty()) {
        setup.inputs.questRules = std::make_shared<const graph::quest::Ruleset>(std::move(rules));
    } else {
        setup.problems.push_back("quest rules: " + rules.error);
    }
    return setup;
}

ForgedScenery buildForgedScenery(const ForgedGame& game)
{
    ForgedScenery scenery;
    SceneBundle& bundle = scenery.bundle;
    const auto material = [&](const char* name, glm::vec3 colour, float roughness) {
        Material m;
        m.name = name;
        m.baseColorFactor = glm::vec4(colour, 1.0f);
        m.metallicFactor = 0.0f;
        m.roughnessFactor = roughness;
        bundle.materials.push_back(m);
    };
    bundle.textures.push_back(dirtRoadTexture());
    material("road", {1.0f, 1.0f, 1.0f}, 0.95f);
    bundle.materials.back().baseColorTexture = 0;
    material("settlement", {0.62f, 0.38f, 0.22f}, 0.8f);
    material("dungeon", {0.30f, 0.20f, 0.36f}, 0.8f);
    material("landmark", {0.62f, 0.60f, 0.54f}, 0.85f);
    material("building", {0.55f, 0.52f, 0.48f}, 0.9f);
    material("loot", {0.85f, 0.70f, 0.20f}, 0.6f);
    material("npc", {0.30f, 0.42f, 0.62f}, 0.8f);

    const auto push = [&](Mesh mesh, std::uint32_t materialIndex) {
        for (Submesh& submesh : mesh.submeshes) {
            submesh.material = materialIndex;
        }
        bundle.meshes.push_back(std::move(mesh));
        return static_cast<std::uint32_t>(bundle.meshes.size() - 1);
    };
    if (game.roads && !game.roads->empty()) {
        const std::uint32_t side = game.desc.cellsPerSide();
        for (std::uint32_t z = 0; z < side; ++z) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const CellId id{static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)};
                Mesh ribbon = game.roads->buildMesh(game.heightfield, id);
                if (ribbon.vertices.empty()) {
                    continue;
                }
                if (ribbon.submeshes.empty()) {
                    ribbon.submeshes.push_back({0, static_cast<std::uint32_t>(ribbon.indices.size()), 0});
                }
                scenery.roadMesh[id] = push(std::move(ribbon), 0);
            }
        }
    }
    if (game.streets && !game.streets->empty()) {
        const std::uint32_t side = game.desc.cellsPerSide();
        for (std::uint32_t z = 0; z < side; ++z) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const CellId id{static_cast<std::int32_t>(x), static_cast<std::int32_t>(z)};
                Mesh strip = game.streets->buildMesh(game.heightfield, id, game.roads.get(), game.waterField.get());
                if (strip.vertices.empty()) {
                    continue;
                }
                scenery.streetMesh[id] = push(std::move(strip), 0);
            }
        }
    }
    if (game.pois) {
        for (const PlacedPoi& poi : game.pois->all()) {
            const std::uint32_t kind = static_cast<std::uint32_t>(poi.kind);
            scenery.poiMesh[poi.id] = push(placedPoiMarkerMesh(poi.kind, poi.silhouetteM), 1 + kind);
        }
    }
    scenery.buildingMesh = push(placedBuildingMesh(0.5f, 0.5f, 1.0f, 0.0f), 4);
    scenery.lootMesh = push(placedBuildingMesh(0.5f, 0.5f, 1.0f, 0.0f), 5);
    scenery.castMesh = push(placedBuildingMesh(0.5f, 0.5f, 1.0f, 0.0f), 6);
    return scenery;
}

bool addSettlementKit(ForgedScenery& scenery, const ForgedGame& game, const std::filesystem::path& folder, bool useVariants)
{
    if (!game.kit || game.kit->empty()) return false;
    KitManifest manifest = builtinKitManifest();
    const std::filesystem::path manifestFile = folder / kKitManifestFile;
    std::error_code exists;
    if (std::filesystem::exists(manifestFile, exists)) {
        KitManifestResult loaded = loadKitManifest(manifestFile);
        if (!loaded.ok()) {
            logf(LogLevel::Error, "{}", loaded.error);
            return false;
        }
        for (std::size_t r = 0; r < kKitPieceCount; ++r) {
            const KitManifestPiece& file = loaded.manifest.pieces[loaded.manifest.role(static_cast<KitPiece>(r))];
            const KitManifestPiece& built = builtinKitManifest().pieces[r];
            if (file.halfWidthM != built.halfWidthM || file.halfDepthM != built.halfDepthM || file.heightM != built.heightM) {
                logf(LogLevel::Error, "kit manifest: the footprint of {} differs from the one the settlements were placed with", file.id);
                return false;
            }
        }
        manifest = std::move(loaded.manifest);
    }
    SceneBundle staged = scenery.bundle;
    KitMeshes meshes;
    if (!cookSettlementKit(folder, manifest, kitManifestPiecesUsed(*game.kit, manifest, useVariants), staged, meshes)) return false;
    scenery.bundle = std::move(staged);
    scenery.kitMesh = meshes;
    scenery.kitLoaded = true;
    return true;
}

SceneryGuard forgedSceneryGuard(const ForgedGame& game)
{
    constexpr double kBucketM = 32.0;
    constexpr double kBuildingClearM = 6.0;
    using Bucket = std::pair<int, int>;
    const auto bucketOf = [](double v) { return static_cast<int>(std::floor(v / kBucketM)); };
    auto index = std::make_shared<std::map<Bucket, std::vector<PlacedBuilding>>>();
    if (game.buildings) {
        for (const PlacedBuilding& b : game.buildings->all()) {
            (*index)[{bucketOf(b.x), bucketOf(b.z)}].push_back(b);
        }
    }
    const WaterField* water = game.waterField.get();
    const std::shared_ptr<const ForgedRoads> roads = game.roads;
    SceneryGuard guard;
    guard.wet = [water](double x, double z) { return water != nullptr && water->at(x, z).wet(); };
    guard.blocked = [index, roads, water, bucketOf](double x, double z) {
        if (!std::isfinite(x) || !std::isfinite(z)) return true;
        if (roads && roads->query(x, z).coverage > 0.0f) return true;
        if (water != nullptr && water->at(x, z).wet()) return true;
        const int bx = bucketOf(x);
        const int bz = bucketOf(z);
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                const auto found = index->find({bx + dx, bz + dz});
                if (found == index->end()) continue;
                for (const PlacedBuilding& b : found->second) {
                    if (std::abs(x - b.x) < b.halfWidthM + kBuildingClearM && std::abs(z - b.z) < b.halfDepthM + kBuildingClearM) return true;
                }
            }
        }
        return false;
    };
    return guard;
}

ForgedSpawn settlementSpawn(const ForgedGame& game, std::size_t index)
{
    const SettlementSite& site = game.sites[index];
    constexpr double kClearM = 4.0;
    const std::vector<PlacedBuilding> buildings = game.buildings ? game.buildings->ofSettlement(site.id) : std::vector<PlacedBuilding>{};
    std::vector<KitInstance> pieces;
    if (game.kit) {
        for (const KitInstance& piece : game.kit->all()) {
            if (piece.settlementId == site.id) pieces.push_back(piece);
        }
    }
    ForgedSpawn spawn = findForgedSpawn(game.heightfield, game.waterField.get(), {{site.x, site.z}}, [&](double x, double z) {
        for (const PlacedBuilding& b : buildings) {
            if (std::abs(x - b.x) < b.halfWidthM + kClearM && std::abs(z - b.z) < b.halfDepthM + kClearM) return true;
        }
        for (const KitInstance& piece : pieces) {
            if (std::abs(x - piece.x) < kClearM + 2.0 && std::abs(z - piece.z) < kClearM + 2.0) return true;
        }
        return false;
    });
    spawn.faceX = site.x;
    spawn.faceZ = site.z;
    return spawn;
}

bool spawnAtSettlement(ForgedGame& game, std::size_t index)
{
    if (index >= game.sites.size()) return false;
    game.spawn = settlementSpawn(game, index);
    return true;
}

bool spawnInBiome(ForgedGame& game, worldforge::BiomeId biome)
{
    if (!game.biomes || game.biomes->empty() || game.heightfield.empty()) return false;
    constexpr double kStepM = 24.0;
    constexpr double kInteriorM = 40.0;
    constexpr std::size_t kTries = 64;
    const double size = game.desc.sizeMetres;
    const double centre = size * 0.5;
    const ForgedBiomeField& field = *game.biomes;
    const SceneryGuard guard = forgedSceneryGuard(game);
    const auto inside = [&](double x, double z) {
        return !guard.blocked(x, z) && field.biomeAt(x, z) == biome && field.biomeAt(x + kInteriorM, z) == biome && field.biomeAt(x - kInteriorM, z) == biome
            && field.biomeAt(x, z + kInteriorM) == biome && field.biomeAt(x, z - kInteriorM) == biome;
    };
    std::vector<std::pair<double, std::pair<double, double>>> candidates;
    for (double z = kStepM; z < size - kStepM; z += kStepM) {
        for (double x = kStepM; x < size - kStepM; x += kStepM) {
            if (inside(x, z)) candidates.push_back({(x - centre) * (x - centre) + (z - centre) * (z - centre), {x, z}});
        }
    }
    std::sort(candidates.begin(), candidates.end());
    for (std::size_t i = 0; i < candidates.size() && i < kTries; ++i) {
        const auto [x, z] = candidates[i].second;
        const ForgedSpawn spawn = findForgedSpawn(game.heightfield, game.waterField.get(), {{x, z}}, [&](double px, double pz) { return !inside(px, pz); });
        if (spawn.found) {
            game.spawn = spawn;
            game.spawn.faceX = spawn.x;
            game.spawn.faceZ = spawn.z;
            return true;
        }
    }
    return false;
}

}
