#include <forgecli/walk_probe.h>

#include "ichor/world/forged_game.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/water_field.h"
#include "ichor/world/world_desc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace forgecli {

namespace {

constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDz[8] = {0, 1, 1, 1, 0, -1, -1, -1};

double flat(double ax, double az, double bx, double bz)
{
    const double dx = ax - bx;
    const double dz = az - bz;
    return std::sqrt(dx * dx + dz * dz);
}

// The walking lattice: nodes every `step` metres over the world, a node is walkable when the rules accept its ground.
class Lattice {
public:
    Lattice(const ichor::ForgedWalking& walking, double step)
        : walking_(walking)
        , step_(step)
        , side_(static_cast<int>(walking.sizeMetres() / step) + 1)
        , state_(static_cast<std::size_t>(side_) * static_cast<std::size_t>(side_), 0)
    {
    }

    int side() const { return side_; }
    double step() const { return step_; }
    std::size_t count() const { return state_.size(); }
    std::size_t index(int i, int j) const { return static_cast<std::size_t>(j) * static_cast<std::size_t>(side_) + static_cast<std::size_t>(i); }
    double x(int i) const { return i * step_; }

    int nearest(double v) const { return std::clamp(static_cast<int>(std::lround(v / step_)), 0, side_ - 1); }

    bool walkable(int i, int j)
    {
        std::int8_t& s = state_[index(i, j)];
        if (s == 0) s = clear(x(i), x(j)) ? 1 : 2;
        return s == 1;
    }

    // A step to the neighbour is taken when its node is walkable and so is the ground half way.
    bool canStep(int i, int j, int dir)
    {
        const int ni = i + kDx[dir];
        const int nj = j + kDz[dir];
        if (ni < 0 || nj < 0 || ni >= side_ || nj >= side_ || !walkable(ni, nj)) return false;
        return walking_.assess((x(i) + x(ni)) * 0.5, (x(j) + x(nj)) * 0.5).walkable();
    }

    // The ground and the ground a probe away on each side: the walker's own filter looks that far ahead, so a route
    // that grazes a cliff foot would stop it.
    bool clear(double px, double pz) const
    {
        const double r = walking_.params().probeMetres * 1.5;
        return walking_.assess(px, pz).walkable() && walking_.assess(px + r, pz).walkable() && walking_.assess(px - r, pz).walkable()
            && walking_.assess(px, pz + r).walkable() && walking_.assess(px, pz - r).walkable();
    }

    double paceAt(int i, int j) const { return std::max(0.2, static_cast<double>(walking_.assess(x(i), x(j)).speedFactor())); }

private:
    const ichor::ForgedWalking& walking_;
    double step_;
    int side_;
    std::vector<std::int8_t> state_;
};

bool usableStep(double step) { return std::isfinite(step) && step >= 1.0 && step <= 64.0; }

}

double walkReachMetres(const ichor::ForgedWalking& walking, double x, double z, double capMetres, double latticeMetres)
{
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(capMetres) || capMetres <= 0.0 || !usableStep(latticeMetres)) return 0.0;
    if (!walking.assess(x, z).walkable() && walking.assess(x, z).block == ichor::WalkBlock::OffMap) return 0.0;
    Lattice lattice(walking, latticeMetres);
    const int si = lattice.nearest(x);
    const int sj = lattice.nearest(z);
    std::vector<std::uint8_t> seen(lattice.count(), 0);
    std::vector<std::pair<int, int>> queue{{si, sj}};
    seen[lattice.index(si, sj)] = 1;
    double reach = 0.0;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto [i, j] = queue[head];
        reach = std::max(reach, flat(lattice.x(i), lattice.x(j), x, z));
        if (reach >= capMetres) return capMetres;
        for (int dir = 0; dir < 8; ++dir) {
            if (!lattice.canStep(i, j, dir)) continue;
            const int ni = i + kDx[dir];
            const int nj = j + kDz[dir];
            if (seen[lattice.index(ni, nj)]) continue;
            seen[lattice.index(ni, nj)] = 1;
            queue.emplace_back(ni, nj);
        }
    }
    return std::min(reach, capMetres);
}

bool routeOnFoot(const ichor::ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& to, double latticeMetres, std::size_t maxExpansions, std::vector<glm::dvec3>& path)
{
    path.clear();
    if (!std::isfinite(from.x) || !std::isfinite(from.z) || !std::isfinite(to.x) || !std::isfinite(to.z) || !usableStep(latticeMetres)) return false;
    Lattice lattice(walking, latticeMetres);
    const int si = lattice.nearest(from.x);
    const int sj = lattice.nearest(from.z);
    const double gx = to.x;
    const double gz = to.z;
    constexpr double kInf = std::numeric_limits<double>::infinity();
    std::vector<double> cost(lattice.count(), kInf);
    std::vector<std::int64_t> parent(lattice.count(), -1);
    using Entry = std::pair<double, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    const std::size_t start = lattice.index(si, sj);
    cost[start] = 0.0;
    open.emplace(flat(lattice.x(si), lattice.x(sj), gx, gz), static_cast<std::uint32_t>(start));
    std::size_t expansions = 0;
    std::int64_t reached = -1;
    while (!open.empty() && expansions < maxExpansions) {
        const auto [f, at] = open.top();
        open.pop();
        const int i = static_cast<int>(at % static_cast<std::uint32_t>(lattice.side()));
        const int j = static_cast<int>(at / static_cast<std::uint32_t>(lattice.side()));
        if (f - flat(lattice.x(i), lattice.x(j), gx, gz) > cost[at] + 1e-9) continue;
        ++expansions;
        if (flat(lattice.x(i), lattice.x(j), gx, gz) <= lattice.step() * 1.5) {
            reached = at;
            break;
        }
        for (int dir = 0; dir < 8; ++dir) {
            if (!lattice.canStep(i, j, dir)) continue;
            const int ni = i + kDx[dir];
            const int nj = j + kDz[dir];
            const std::size_t n = lattice.index(ni, nj);
            const double step = lattice.step() * ((dir & 1) ? 1.4142135623730951 : 1.0);
            const double next = cost[at] + step / lattice.paceAt(ni, nj);
            if (next >= cost[n]) continue;
            cost[n] = next;
            parent[n] = at;
            open.emplace(next + flat(lattice.x(ni), lattice.x(nj), gx, gz), static_cast<std::uint32_t>(n));
        }
    }
    if (reached < 0) return false;
    std::vector<glm::dvec3> reversed;
    for (std::int64_t at = reached; at >= 0 && at != static_cast<std::int64_t>(start); at = parent[static_cast<std::size_t>(at)]) {
        const int i = static_cast<int>(at % lattice.side());
        const int j = static_cast<int>(at / lattice.side());
        reversed.emplace_back(lattice.x(i), 0.0, lattice.x(j));
    }
    path.assign(reversed.rbegin(), reversed.rend());
    return true;
}

WalkProbeResult probeWalk(const worldforge::ForgedWorld& world, const WalkProbeParams& params)
{
    WalkProbeResult out;
    if (!world.ok() || !world.roads() || !world.settlements()) {
        out.error = "the world has no roads or settlements";
        return out;
    }
    ichor::WorldDesc base;
    base.seed = world.seed();
    const ichor::WorldDesc desc = ichor::forgedWorldDesc(base);
    ichor::ForgedHeightfield forged = ichor::heightfieldFromForgedRoads(world, desc);
    if (!forged.ok()) {
        out.error = forged.error;
        return out;
    }
    const ichor::ForgedWater water = ichor::waterFromForgedWorld(world, forged.field);
    if (!water.ok()) {
        out.error = water.error;
        return out;
    }
    const ichor::WaterField waterField(forged.field, water.hydrology, water.rivers);
    const ichor::ForgedWalking walking(forged.field, &waterField, ichor::CapabilitySet{});

    out.metresPerCell = static_cast<double>(desc.sizeMetres) / static_cast<double>(world.size() - 1);
    std::vector<std::pair<double, double>> towns;
    for (const worldforge::Settlement& s : world.settlements()->sites) towns.emplace_back(s.x * out.metresPerCell, s.y * out.metresPerCell);

    const ichor::ForgedSpawn spawn = ichor::findForgedSpawn(forged.field, &waterField, towns);
    if (!spawn.found) return out;
    out.spawnFound = true;
    out.spawnX = spawn.x;
    out.spawnZ = spawn.z;
    out.reachMetres = walkReachMetres(walking, spawn.x, spawn.z, params.pocketCapMetres, params.latticeMetres);
    out.pocket = out.reachMetres < params.pocketReachMetres;

    // The game spawns at the settlement nearest the middle of the world; the trip is to the nearest other one.
    const double middle = static_cast<double>(desc.sizeMetres) * 0.5;
    std::size_t home = towns.size();
    double homeDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < towns.size(); ++i) {
        const double d = flat(towns[i].first, towns[i].second, middle, middle);
        if (d < homeDistance) {
            homeDistance = d;
            home = i;
        }
    }
    std::vector<std::pair<double, std::size_t>> byDistance;
    for (std::size_t i = 0; i < towns.size(); ++i) {
        const double d = flat(towns[i].first, towns[i].second, spawn.x, spawn.z);
        if (i != home && d >= params.minTripMetres) byDistance.emplace_back(d, i);
    }
    std::sort(byDistance.begin(), byDistance.end());
    for (const auto& [distance, i] : byDistance) {
        glm::dvec3 goal;
        if (!ichor::findWalkableSpawn(walking, towns[i].first, towns[i].second, params.goalSearchMetres, goal)) continue;
        goal.y = forged.field.heightAt(goal.x, goal.z);
        out.hasTarget = true;
        out.tripMetres = distance;
        glm::dvec3 from{spawn.x, forged.field.heightAt(spawn.x, spawn.z), spawn.z};
        std::vector<glm::dvec3> path;
        out.routeFound = routeOnFoot(walking, from, goal, params.latticeMetres, params.maxExpansions, path);
        for (glm::dvec3& p : path) p.y = forged.field.heightAt(p.x, p.z);
        ichor::WalkBotParams bot;
        bot.arriveMetres = params.arriveMetres;
        const ichor::WalkResult walk = ichor::walkTo(walking, from, goal, path, bot);
        out.arrived = walk.outcome == ichor::WalkOutcome::Arrived;
        out.walkedMetres = walk.metresWalked;
        if (!out.arrived) out.blocker = walk.outcome == ichor::WalkOutcome::TimedOut ? "timeout" : ichor::walkBlockName(walk.blocker);
        break;
    }
    return out;
}

WalkProbe makeWalkProbe(const WalkProbeParams& params)
{
    return [params](const worldforge::ForgedWorld& world) {
        try {
            return probeWalk(world, params);
        } catch (const std::exception& e) {
            WalkProbeResult failed;
            failed.error = e.what();
            return failed;
        }
    };
}

}
