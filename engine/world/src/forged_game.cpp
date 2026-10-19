#include "ichor/world/forged_game.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace ichor {

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
    for (const std::uint8_t tier : tiers.tiers) {
        h = hashCombine(h, tier);
    }
    if (water.hydrology) {
        h = hashCombine(h, water.hydrology->side);
        h = hashCombine(h, static_cast<std::uint64_t>(water.rivers.riverThreshold));
    }
    return h;
}

std::unique_ptr<ForgedGame> buildForgedGame(const ForgeJobResult& result, const WorldDesc& requested)
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

    PlacedSettlements settlements = placeForgedSettlements(world, game->heightfield, game->roads.get(), game->waterField.get());
    if (!settlements.ok()) {
        game->error = "settlements: " + settlements.error;
        return game;
    }
    game->buildings = std::make_shared<const PlacedBuildingSet>(std::move(settlements.set));

    std::vector<SettlementSite> sites;
    std::string siteError;
    if (!forgedSettlementSites(world, game->heightfield, sites, siteError)) {
        game->error = "settlements: " + siteError;
        return game;
    }
    game->sites = sites;
    PlacedKit kit = placeSettlementKit(sites, *game->buildings, game->heightfield, game->roads.get(), game->waterField.get());
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
    game->mapLayers = MapLayerStack(renderMapImage(game->heightfield), world);

    std::vector<std::pair<double, double>> towns;
    for (const PlacedPoi& poi : game->pois->all()) {
        if (poi.kind == PlacedPoiKind::Settlement) {
            towns.emplace_back(poi.x, poi.z);
        }
    }
    const PlacedBuildingSet& buildings = *game->buildings;
    constexpr double kClearM = 4.0;
    game->spawn = findForgedSpawn(game->heightfield, game->waterField.get(), towns, [&](double x, double z) {
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

bool spawnAtSettlement(ForgedGame& game, std::size_t index)
{
    if (index >= game.sites.size() || !game.buildings || !game.kit) return false;
    const SettlementSite& site = game.sites[index];
    constexpr double kClearM = 4.0;
    game.spawn = findForgedSpawn(game.heightfield, game.waterField.get(), {{site.x, site.z}}, [&](double x, double z) {
        for (const PlacedBuilding& b : game.buildings->ofSettlement(site.id)) {
            if (std::abs(x - b.x) < b.halfWidthM + kClearM && std::abs(z - b.z) < b.halfDepthM + kClearM) return true;
        }
        for (const KitInstance& piece : game.kit->all()) {
            if (piece.settlementId == site.id && std::abs(x - piece.x) < kClearM + 2.0 && std::abs(z - piece.z) < kClearM + 2.0) return true;
        }
        return false;
    });
    game.spawn.faceX = site.x;
    game.spawn.faceZ = site.z;
    return true;
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
    material("road", {0.22f, 0.17f, 0.12f}, 0.95f);
    material("settlement", {0.62f, 0.38f, 0.22f}, 0.8f);
    material("dungeon", {0.30f, 0.20f, 0.36f}, 0.8f);
    material("landmark", {0.62f, 0.60f, 0.54f}, 0.85f);
    material("building", {0.55f, 0.52f, 0.48f}, 0.9f);

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
    if (game.pois) {
        for (const PlacedPoi& poi : game.pois->all()) {
            const std::uint32_t kind = static_cast<std::uint32_t>(poi.kind);
            scenery.poiMesh[poi.id] = push(placedPoiMarkerMesh(poi.kind, poi.silhouetteM), 1 + kind);
        }
    }
    scenery.buildingMesh = push(placedBuildingMesh(0.5f, 0.5f, 1.0f, 0.0f), 4);
    return scenery;
}

bool addSettlementKit(ForgedScenery& scenery, const ForgedGame& game, const std::filesystem::path& folder)
{
    if (!game.kit || game.kit->empty()) return false;
    SceneBundle staged = scenery.bundle;
    KitMeshes meshes;
    if (!cookSettlementKit(folder, kitPiecesUsed(*game.kit), staged, meshes)) return false;
    scenery.bundle = std::move(staged);
    scenery.kitMesh = meshes;
    scenery.kitLoaded = true;
    return true;
}

}
