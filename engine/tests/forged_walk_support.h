#pragma once

#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_navmesh.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walk_route.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/placed_settlements.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ichor::walksupport {

constexpr double kSettlementArrive = 12.0;
constexpr double kMinTrip = 60.0;
constexpr std::size_t kRouteNodeBudget = 30'000; // grid nodes the ground-route searches may expand before no other spawn settlement is tried

struct SeedWalk {
    std::uint64_t seed = 0;
    bool hadSettlement = false;
    bool hadPath = false;
    WalkResult result;
    double trip = 0.0;
    // Where the walk would have blocked and why, for the report (SEED-555).
    double straightBlockMetres = -1.0; // along the straight line from the spawn to the goal; negative when it is all walkable
    WalkBlock straightBlock = WalkBlock::None;
    bool usedNavmesh = false;      // the walker followed a navmesh path
    bool usedGroundRoute = false;  // or a route searched over the ground the walker itself assesses
    double routeMetres = 0.0;
    WalkRouteFailure routeFailure = WalkRouteFailure::BadInput;
    int homeRank = 0;  // 0 spawns at the settlement nearest the middle of the world; higher when its ground is a pocket
    int goalRank = 0;      // 0 is the nearest settlement; higher when nearer ones are cut off by cliffs or deep water
    std::string nearerCut; // why the nearer settlements were skipped
    std::string enclosure; // what bounds the ground the spawn can reach, when the nearest settlement is cut off
    std::size_t routeNodesSearched = 0; // grid nodes expanded by every ground-route search of this walk
};

inline std::string describeRoute(const SeedWalk& walk)
{
    std::string text = walk.straightBlockMetres < 0.0 ? "straight line clear"
                                                      : std::format("straight line {} at {:.0f} m", walkBlockName(walk.straightBlock), walk.straightBlockMetres);
    text += walk.usedNavmesh       ? ", navmesh path"
        : walk.usedGroundRoute     ? std::format(", ground route {:.0f} m", walk.routeMetres)
                                   : std::format(", no route ({}; {})", walkRouteFailureName(walk.routeFailure), walk.enclosure);
    if (walk.homeRank > 0) {
        text += std::format(", spawn moved to the settlement {} from the middle (the nearest is a pocket)", walk.homeRank + 1);
    }
    if (walk.goalRank > 0 || !walk.nearerCut.empty()) {
        text += std::format(", goal rank {} (nearer cut off: {})", walk.goalRank, walk.nearerCut);
    }
    return text;
}

inline SeedWalk walkSeed(std::uint64_t seed, int grid)
{
    SeedWalk out;
    out.seed = seed;

    WorldDesc base;
    base.seed = seed;
    const WorldDesc desc = forgedWorldDesc(base);
    const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, grid, worldforge::ForgeConfig{}, worldforge::ForgePhase::Settlements);
    REQUIRE(world.ok());
    ForgedHeightfield forged = heightfieldFromForgedWorld(world, desc);
    REQUIRE(forged.ok());
    const ForgedWater forgedWater = waterFromForgedWorld(world, forged.field);
    REQUIRE(forgedWater.ok());
    const WaterField water(forged.field, forgedWater.hydrology, forgedWater.rivers);
    const ForgedWalking walking(forged.field, &water, CapabilitySet{});

    const PlacedSettlements placed = placeForgedSettlements(world, forged.field, nullptr, &water);
    REQUIRE(placed.ok());
    REQUIRE(world.settlements().has_value());

    // A settlement is where its buildings stand: the building closest to the settlement's centre is the goal,
    // because the centre itself can be a bog or a ledge. A settlement the ground gave no building has no goal.
    const double cellToMetres = desc.sizeMetres / static_cast<double>(world.size() - 1);
    std::vector<glm::dvec3> goals;
    for (std::size_t i = 0; i < world.settlements()->sites.size(); ++i) {
        const worldforge::Settlement& site = world.settlements()->sites[i];
        const double cx = site.x * cellToMetres;
        const double cz = site.y * cellToMetres;
        double nearest = std::numeric_limits<double>::infinity();
        glm::dvec3 goal{0.0};
        for (const PlacedBuilding& b : placed.set.ofSettlement(worldforge::detail::forgeSettlementId(i))) {
            const double d = std::hypot(b.x - cx, b.z - cz);
            if (d < nearest) {
                nearest = d;
                goal = {b.x, forged.field.heightAt(b.x, b.z), b.z};
            }
        }
        if (std::isfinite(nearest)) {
            goals.push_back(goal);
        }
    }

    // The spawn stands at the settlement nearest the middle of the world, as the game's does; the walk is to the
    // nearest other settlement. Spawn candidates are dry ground the walking rules accept, nearest the settlement
    // first, and the first one with a route on the navmesh wins; with no route at all the first is kept and
    // the walk heads straight, which is how an honest blocker gets reported.
    glm::dvec3 spawn{0.0};
    glm::dvec3 goal{0.0};
    std::vector<glm::dvec3> path;
    bool spawnFound = false;
    const double middle = desc.sizeMetres * 0.5;
    std::size_t home = goals.size();
    double homeDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < goals.size(); ++i) {
        const double d = std::hypot(goals[i].x - middle, goals[i].z - middle);
        if (d < homeDistance) {
            homeDistance = d;
            home = i;
        }
    }
    NavWorld nav;
    REQUIRE(nav.init(desc, desc.cellsPerSide() * desc.cellsPerSide()));
    if (home < goals.size()) {
        // Only the cells around the trip are baked, a cell of margin each side: the whole world is minutes of work.
        double reach = std::numeric_limits<double>::infinity();
        glm::dvec3 target{0.0};
        for (std::size_t i = 0; i < goals.size(); ++i) {
            const double d = std::hypot(goals[i].x - goals[home].x, goals[i].z - goals[home].z);
            if (i != home && d >= kMinTrip && d < reach) {
                reach = d;
                target = goals[i];
            }
        }
        if (std::isfinite(reach)) {
            const auto cellOf = [&](double v) { return static_cast<std::int32_t>(std::clamp(v / desc.cellMetres, 0.0, desc.cellsPerSide() - 1.0)); };
            const std::int32_t last = static_cast<std::int32_t>(desc.cellsPerSide()) - 1;
            const std::int32_t x0 = std::max(0, std::min(cellOf(goals[home].x), cellOf(target.x)) - 1);
            const std::int32_t x1 = std::min(last, std::max(cellOf(goals[home].x), cellOf(target.x)) + 1);
            const std::int32_t z0 = std::max(0, std::min(cellOf(goals[home].z), cellOf(target.z)) - 1);
            const std::int32_t z1 = std::min(last, std::max(cellOf(goals[home].z), cellOf(target.z)) + 1);
            NavParams navParams; // a coarser voxel than the game bakes with: the route is what is tested, and a fine bake of a dozen tiles is minutes
            navParams.cellSize = 2.0f;
            navParams.cellHeight = 1.0f;
            navParams.agentClimb = 1.0f;
            for (std::int32_t z = z0; z <= z1; ++z) {
                for (std::int32_t x = x0; x <= x1; ++x) {
                    NavTile tile;
                    const CellId id{x, z};
                    REQUIRE(buildForgedNavTile(forged.field, water, nullptr, id, navParams, tile));
                    if (!tile.empty()) {
                        REQUIRE(nav.addTile(id, std::move(tile)));
                    }
                }
            }
        }
    }
    // The settlement nearest the middle is home; when the ground around it is a pocket no other settlement can be reached
    // from, the next nearest the middle is tried, which is the reachable side the spawn should have picked.
    std::vector<std::pair<double, std::size_t>> homes;
    for (std::size_t i = 0; i < goals.size(); ++i) {
        homes.emplace_back(std::hypot(goals[i].x - middle, goals[i].z - middle), i);
    }
    std::sort(homes.begin(), homes.end());
    constexpr int kGoalRanks = 4;
    WalkRouteMemo routeMemo;
    for (std::size_t homeRank = 0; homeRank < homes.size() && !out.usedNavmesh && !out.usedGroundRoute && out.routeNodesSearched < kRouteNodeBudget; ++homeRank) {
        const std::size_t candidateHome = homes[homeRank].second;
        for (int attempt = 0; attempt < 12 && !out.usedNavmesh && !out.usedGroundRoute; ++attempt) {
            glm::dvec3 candidate;
            if (!findWalkableSpawn(walking, goals[candidateHome].x + attempt * 6.0, goals[candidateHome].z, 60.0, candidate)) {
                continue;
            }
            candidate.y = forged.field.heightAt(candidate.x, candidate.z);
            std::vector<std::pair<double, std::size_t>> ranked;
            for (std::size_t i = 0; i < goals.size(); ++i) {
                const double d = std::hypot(goals[i].x - candidate.x, goals[i].z - candidate.z);
                if (i != candidateHome && d >= kMinTrip) {
                    ranked.emplace_back(d, i);
                }
            }
            std::sort(ranked.begin(), ranked.end());
            if (ranked.empty()) {
                continue;
            }
            std::string cut;
            bool startBlocked = false;
            for (int rank = 0; rank < kGoalRanks && rank < static_cast<int>(ranked.size()); ++rank) {
                const glm::dvec3& target = goals[ranked[static_cast<std::size_t>(rank)].second];
                const double distance = ranked[static_cast<std::size_t>(rank)].first;
                if (!spawnFound) {
                    spawn = candidate;
                    goal = target;
                    out.trip = distance;
                    spawnFound = true;
                    out.straightBlockMetres = firstBlockOnLine(walking, spawn, goal, out.straightBlock);
                }
                std::vector<glm::dvec3> found;
                bool viaNavmesh = false;
                WalkRoute route;
                if (nav.findPath(candidate, target, found)) {
                    viaNavmesh = true;
                } else {
                    WalkRouteParams routeParams;
                    routeParams.goalRadiusMetres = kSettlementArrive - 2.0;
                    route = findWalkRoute(walking, candidate, target, routeParams, &routeMemo);
                    out.routeNodesSearched += route.nodesSearched;
                    if (!route.found) {
                        if (rank == 0) {
                            out.routeFailure = route.failure;
                            out.enclosure = describeEnclosure(route);
                        }
                        startBlocked = startBlocked || route.failure == WalkRouteFailure::StartBlocked;
                        cut += std::format("{}[{:.0f} m: {}]", cut.empty() ? "" : ", ", distance, walkRouteFailureName(route.failure));
                        continue;
                    }
                    found = route.waypoints;
                }
                spawn = candidate;
                goal = target;
                out.trip = distance;
                out.goalRank = rank;
                out.homeRank = static_cast<int>(homeRank);
                out.nearerCut = cut;
                out.hadPath = viaNavmesh;
                out.usedNavmesh = viaNavmesh;
                out.usedGroundRoute = !viaNavmesh;
                out.routeMetres = route.metres;
                out.routeFailure = WalkRouteFailure::None;
                out.straightBlockMetres = firstBlockOnLine(walking, spawn, goal, out.straightBlock);
                path = std::move(found);
                break;
            }
            if (!out.usedNavmesh && !out.usedGroundRoute) {
                if (homeRank == 0 && attempt == 0) {
                    out.nearerCut = cut;
                }
                if (!startBlocked) {
                    break; // the same pocket whichever dry spot of it the spawn takes
                }
            }
        }
    }
    out.hadSettlement = spawnFound;
    if (!spawnFound) {
        return out;
    }
    if (!out.usedNavmesh && !out.usedGroundRoute) {
        path.clear();
    }

    WalkBotParams bot;
    bot.arriveMetres = kSettlementArrive;
    out.result = walkTo(walking, spawn, goal, path, bot);
    return out;
}

}
