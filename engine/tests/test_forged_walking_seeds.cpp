#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_navmesh.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/placed_settlements.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr int kGrid = 129;
constexpr std::array<std::uint64_t, 8> kSeeds = {1, 2, 3, 4, 5, 6, 7, 8};
constexpr double kSettlementArrive = 12.0;
constexpr double kMinTrip = 60.0;

struct SeedWalk {
    std::uint64_t seed = 0;
    bool hadSettlement = false;
    bool hadPath = false;
    WalkResult result;
    double trip = 0.0;
};

SeedWalk walkSeed(std::uint64_t seed)
{
    SeedWalk out;
    out.seed = seed;

    WorldDesc base;
    base.seed = seed;
    const WorldDesc desc = forgedWorldDesc(base);
    const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, kGrid, worldforge::ForgeConfig{}, worldforge::ForgePhase::Settlements);
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
    if (home < goals.size()) {
        for (int attempt = 0; attempt < 12; ++attempt) {
            glm::dvec3 candidate;
            if (!findWalkableSpawn(walking, goals[home].x + attempt * 6.0, goals[home].z, 60.0, candidate)) {
                continue;
            }
            candidate.y = forged.field.heightAt(candidate.x, candidate.z);
            double best = std::numeric_limits<double>::infinity();
            glm::dvec3 nearest{0.0};
            for (std::size_t i = 0; i < goals.size(); ++i) {
                const double d = std::hypot(goals[i].x - candidate.x, goals[i].z - candidate.z);
                if (i != home && d >= kMinTrip && d < best) {
                    best = d;
                    nearest = goals[i];
                }
            }
            if (!std::isfinite(best)) {
                continue;
            }
            if (!spawnFound) {
                spawn = candidate;
                goal = nearest;
                out.trip = best;
                spawnFound = true;
            }
            if (nav.findPath(candidate, nearest, path)) {
                spawn = candidate;
                goal = nearest;
                out.trip = best;
                out.hadPath = true;
                break;
            }
        }
    }
    out.hadSettlement = spawnFound;
    if (!spawnFound) {
        return out;
    }
    if (!out.hadPath) {
        path.clear();
    }

    WalkBotParams bot;
    bot.arriveMetres = kSettlementArrive;
    out.result = walkTo(walking, spawn, goal, path, bot);
    return out;
}

}

TEST_CASE("from the spawn toward the nearest settlement on 8 seeds the walker arrives or names what stopped it", "[walking][bake]")
{
    int arrived = 0;
    int blocked = 0;
    int withSettlement = 0;
    std::string log;
    for (const std::uint64_t seed : kSeeds) {
        const SeedWalk walk = walkSeed(seed);
        if (!walk.hadSettlement) {
            log += std::format("seed {}: no settlement in range\n", seed);
            continue;
        }
        ++withSettlement;
        const WalkResult& r = walk.result;
        log += std::format("seed {}: trip {:.0f} m, path {}, outcome {}, walked {:.0f} m in {:.0f} s, {:.0f} m short {}\n", seed, walk.trip, walk.hadPath ? "yes" : "no",
            r.outcome == WalkOutcome::Arrived ? "arrived" : r.outcome == WalkOutcome::Blocked ? "blocked" : "TIMED OUT", r.metresWalked, r.seconds, r.metresLeft, r.report);
        INFO(log);
        // Never silent: a walk that is still moving at the time limit is a failure, and a walk that stopped says why.
        REQUIRE(r.outcome != WalkOutcome::TimedOut);
        if (r.outcome == WalkOutcome::Arrived) {
            ++arrived;
            REQUIRE(r.metresLeft <= kSettlementArrive);
        } else {
            ++blocked;
            REQUIRE(r.blocker != WalkBlock::None);
            REQUIRE_FALSE(r.report.empty());
        }
        // Whatever happened, the walker made an honest attempt: it walked, not froze at the spawn.
        REQUIRE(r.metresWalked > 1.0);
    }
    INFO(log);
    REQUIRE(withSettlement == static_cast<int>(kSeeds.size()));
    REQUIRE(arrived + blocked == withSettlement);
    // A walker that stops at every obstacle would still report honestly; the rules must also let it get places.
    REQUIRE(arrived >= 3);
}
