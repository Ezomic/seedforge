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
        if (heightfield.heightAt(px, pz) < seaFloor || heightfield.normalAt(px, pz).y < params.minUpY) {
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
    if (heightfield.empty() || !(params.searchStepMetres > 0.0f)) {
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
    spawn.x = centre;
    spawn.z = centre;
    spawn.groundY = heightfield.heightAt(centre, centre);

    const double step = params.searchStepMetres;
    const double margin = params.edgeMarginMetres;
    const int maxRing = static_cast<int>(size / step) + 1;
    for (int ring = 0; ring <= maxRing; ++ring) {
        double bestDistance = std::numeric_limits<double>::max();
        double bestX = 0.0;
        double bestZ = 0.0;
        bool any = false;
        for (int iz = -ring; iz <= ring; ++iz) {
            const int stride = std::abs(iz) == ring ? 1 : std::max(2 * ring, 1);
            for (int ix = -ring; ix <= ring; ix += stride) {
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

std::uint64_t ForgedGame::stateHash() const
{
    std::uint64_t h = hashCombine(desc.seed, world ? world->worldHash() : 0);
    h = hashCombine(h, heightfield.digest());
    h = hashCombine(h, spawn.found ? 1 : 0);
    h = hashCombine(h, std::bit_cast<std::uint64_t>(spawn.x));
    h = hashCombine(h, std::bit_cast<std::uint64_t>(spawn.z));
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

    ForgedHeightfield terrain = heightfieldFromForgedRoads(*game->world, game->desc);
    if (!terrain.ok()) {
        game->error = "terrain: " + terrain.error;
        return game;
    }
    game->heightfield = std::move(terrain.field);

    ForgedBiomes biomes = biomesFromForgedWorld(*game->world, game->desc);
    if (!biomes.ok()) {
        game->error = "biomes: " + biomes.error;
        return game;
    }
    game->biomes = std::move(biomes.field);

    game->spawn = findForgedSpawn(game->heightfield, nullptr, {});
    return game;
}

}
