#include "ichor/world/forged_walk_route.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <format>
#include <queue>
#include <utility>

namespace ichor {

namespace {

constexpr double kMinPace = 0.1;
constexpr int kSnapRings = 6;
constexpr int kLookahead = 64;
constexpr double kMaxPieces = 4096.0;
constexpr double kMaxParamMetres = 1.0e6;
constexpr std::size_t kMaxNodesLimit = 50'000'000;

bool finite2(const glm::dvec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.z);
}

double flat(double dx, double dz)
{
    return std::sqrt(dx * dx + dz * dz);
}

bool onMap(const ForgedWalking& walking, const glm::dvec3& p)
{
    const double size = walking.sizeMetres();
    return p.x >= 0.0 && p.z >= 0.0 && p.x <= size && p.z <= size;
}

bool segmentWalkable(const ForgedWalking& walking, double ax, double az, double bx, double bz, double checkMetres)
{
    const double length = flat(bx - ax, bz - az);
    const int pieces = std::max(1, static_cast<int>(std::min(std::ceil(length / checkMetres), kMaxPieces)));
    for (int i = 1; i <= pieces; ++i) {
        const double t = static_cast<double>(i) / pieces;
        if (!walking.assess(ax + (bx - ax) * t, az + (bz - az) * t).walkable()) {
            return false;
        }
    }
    return true;
}

bool segmentClear(const ForgedWalking& walking, const glm::dvec3& a, const glm::dvec3& b, double checkMetres, double clearance)
{
    if (!segmentWalkable(walking, a.x, a.z, b.x, b.z, checkMetres)) {
        return false;
    }
    if (!(clearance > 0.0)) {
        return true;
    }
    const double length = flat(b.x - a.x, b.z - a.z);
    if (!(length > 1.0e-9)) {
        return true;
    }
    const double nx = -(b.z - a.z) / length * clearance;
    const double nz = (b.x - a.x) / length * clearance;
    return segmentWalkable(walking, a.x + nx, a.z + nz, b.x + nx, b.z + nz, checkMetres)
        && segmentWalkable(walking, a.x - nx, a.z - nz, b.x - nx, b.z - nz, checkMetres);
}

struct Grid {
    double x0 = 0.0;
    double z0 = 0.0;
    double step = 4.0;
    int nx = 0;
    int nz = 0;

    int index(int i, int j) const { return j * nx + i; }
    double xOf(int i) const { return x0 + i * step; }
    double zOf(int j) const { return z0 + j * step; }
};

Grid makeGrid(const ForgedWalking& walking, double minX, double minZ, double maxX, double maxZ, const WalkRouteParams& params)
{
    const double size = walking.sizeMetres();
    Grid g;
    g.x0 = std::max(0.0, minX);
    g.z0 = std::max(0.0, minZ);
    const double w = std::min(size, maxX) - g.x0;
    const double h = std::min(size, maxZ) - g.z0;
    g.step = params.stepMetres;
    const auto count = [&](double span) { return std::floor(std::max(0.0, span) / g.step) + 1.0; };
    while (count(w) * count(h) > static_cast<double>(params.maxNodes)) {
        g.step *= 1.05;
    }
    g.nx = static_cast<int>(count(w));
    g.nz = static_cast<int>(count(h));
    return g;
}

struct Search {
    bool found = false;
    bool startOk = false;
    bool goalOk = false;
    std::vector<glm::dvec3> nodes;
    std::size_t searched = 0;
    double reachableSquareMetres = 0.0;
    std::size_t steep = 0;
    std::size_t deepWater = 0;
    std::size_t mapEdge = 0;
};

Search searchWindow(const ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& goal, const Grid& grid, double goalRadius, double checkMetres)
{
    Search out;
    const std::size_t total = static_cast<std::size_t>(grid.nx) * static_cast<std::size_t>(grid.nz);
    std::vector<float> pace(total, -1.0f);
    const auto paceAt = [&](int i, int j) {
        float& p = pace[static_cast<std::size_t>(grid.index(i, j))];
        if (p < 0.0f) {
            const WalkAssessment a = walking.assess(grid.xOf(i), grid.zOf(j));
            p = a.walkable() ? std::clamp(a.speedFactor(), static_cast<float>(kMinPace), 1.0f) : 0.0f;
        }
        return p;
    };
    const auto nodeNear = [&](const glm::dvec3& p, int& bi, int& bj, bool mustReach) {
        const int ci = std::clamp(static_cast<int>(std::lround((p.x - grid.x0) / grid.step)), 0, grid.nx - 1);
        const int cj = std::clamp(static_cast<int>(std::lround((p.z - grid.z0) / grid.step)), 0, grid.nz - 1);
        double best = std::numeric_limits<double>::infinity();
        bool any = false;
        for (int j = std::max(0, cj - kSnapRings); j <= std::min(grid.nz - 1, cj + kSnapRings); ++j) {
            for (int i = std::max(0, ci - kSnapRings); i <= std::min(grid.nx - 1, ci + kSnapRings); ++i) {
                if (paceAt(i, j) <= 0.0f) continue;
                const double d = flat(grid.xOf(i) - p.x, grid.zOf(j) - p.z);
                if (d < best && (!mustReach || segmentWalkable(walking, p.x, p.z, grid.xOf(i), grid.zOf(j), checkMetres))) {
                    best = d;
                    bi = i;
                    bj = j;
                    any = true;
                }
            }
        }
        return any;
    };

    int si = 0;
    int sj = 0;
    out.startOk = nodeNear(from, si, sj, true);
    const double goalReach = std::max(goalRadius, grid.step);
    bool goalAny = false;
    {
        const int ci = std::clamp(static_cast<int>(std::lround((goal.x - grid.x0) / grid.step)), 0, grid.nx - 1);
        const int cj = std::clamp(static_cast<int>(std::lround((goal.z - grid.z0) / grid.step)), 0, grid.nz - 1);
        const int rings = static_cast<int>(std::ceil(goalReach / grid.step)) + 1;
        for (int j = std::max(0, cj - rings); j <= std::min(grid.nz - 1, cj + rings) && !goalAny; ++j) {
            for (int i = std::max(0, ci - rings); i <= std::min(grid.nx - 1, ci + rings); ++i) {
                if (flat(grid.xOf(i) - goal.x, grid.zOf(j) - goal.z) <= goalReach && paceAt(i, j) > 0.0f) {
                    goalAny = true;
                    break;
                }
            }
        }
    }
    out.goalOk = goalAny;
    if (!out.startOk || !out.goalOk) {
        return out;
    }

    constexpr int kDi[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    constexpr int kDj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    std::vector<double> cost(total, std::numeric_limits<double>::infinity());
    std::vector<std::int32_t> parent(total, -1);
    using Entry = std::pair<double, std::int32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    const auto heuristic = [&](int i, int j) { return flat(grid.xOf(i) - goal.x, grid.zOf(j) - goal.z); };
    const std::int32_t start = grid.index(si, sj);
    cost[static_cast<std::size_t>(start)] = 0.0;
    open.emplace(heuristic(si, sj), start);

    std::int32_t reached = -1;
    while (!open.empty()) {
        const std::int32_t at = open.top().second;
        const double f = open.top().first;
        open.pop();
        const int i = at % grid.nx;
        const int j = at / grid.nx;
        if (f > cost[static_cast<std::size_t>(at)] + heuristic(i, j) + 1.0e-9) {
            continue;
        }
        ++out.searched;
        if (heuristic(i, j) <= goalReach) {
            reached = at;
            break;
        }
        for (int k = 0; k < 8; ++k) {
            const int ni = i + kDi[k];
            const int nj = j + kDj[k];
            if (ni < 0 || nj < 0 || ni >= grid.nx || nj >= grid.nz) continue;
            const float p = paceAt(ni, nj);
            if (p <= 0.0f) continue;
            if (!segmentWalkable(walking, grid.xOf(i), grid.zOf(j), grid.xOf(ni), grid.zOf(nj), checkMetres)) continue;
            const double d = flat(grid.xOf(ni) - grid.xOf(i), grid.zOf(nj) - grid.zOf(j));
            const double c = cost[static_cast<std::size_t>(at)] + d / static_cast<double>(p);
            const std::int32_t to = grid.index(ni, nj);
            if (c < cost[static_cast<std::size_t>(to)]) {
                cost[static_cast<std::size_t>(to)] = c;
                parent[static_cast<std::size_t>(to)] = at;
                open.emplace(c + heuristic(ni, nj), to);
            }
        }
    }
    if (reached < 0) {
        for (int j = 0; j < grid.nz; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                if (!(cost[static_cast<std::size_t>(grid.index(i, j))] < std::numeric_limits<double>::infinity())) continue;
                out.reachableSquareMetres += grid.step * grid.step;
                for (int k = 0; k < 4; ++k) {
                    const int ni = i + kDi[k];
                    const int nj = j + kDj[k];
                    if (ni < 0 || nj < 0 || ni >= grid.nx || nj >= grid.nz) {
                        ++out.mapEdge;
                    } else if (paceAt(ni, nj) <= 0.0f) {
                        const WalkBlock block = walking.assess(grid.xOf(ni), grid.zOf(nj)).block;
                        if (block == WalkBlock::DeepWater) {
                            ++out.deepWater;
                        } else if (block == WalkBlock::OffMap) {
                            ++out.mapEdge;
                        } else {
                            ++out.steep;
                        }
                    }
                }
            }
        }
        return out;
    }
    for (std::int32_t n = reached; n >= 0; n = parent[static_cast<std::size_t>(n)]) {
        out.nodes.push_back({grid.xOf(n % grid.nx), 0.0, grid.zOf(n / grid.nx)});
    }
    std::reverse(out.nodes.begin(), out.nodes.end());
    out.found = true;
    return out;
}

std::vector<glm::dvec3> pull(const ForgedWalking& walking, const std::vector<glm::dvec3>& nodes, const WalkRouteParams& params)
{
    std::vector<glm::dvec3> out;
    std::size_t i = 0;
    out.push_back(nodes[0]);
    while (i + 1 < nodes.size()) {
        std::size_t best = i + 1;
        const std::size_t far = std::min(nodes.size() - 1, i + static_cast<std::size_t>(kLookahead));
        for (std::size_t j = far; j > i + 1; --j) {
            if (segmentClear(walking, nodes[i], nodes[j], params.checkMetres, params.clearanceMetres)) {
                best = j;
                break;
            }
        }
        out.push_back(nodes[best]);
        i = best;
    }
    return out;
}

}

const char* walkRouteFailureName(WalkRouteFailure failure)
{
    switch (failure) {
    case WalkRouteFailure::None: return "route found";
    case WalkRouteFailure::BadInput: return "bad input (not a number, or off the map)";
    case WalkRouteFailure::StartBlocked: return "no walkable ground at the start";
    case WalkRouteFailure::GoalBlocked: return "no walkable ground at the goal";
    case WalkRouteFailure::NoGroundRoute: return "cliffs or deep water cut the goal off from the start";
    }
    return "unknown";
}

WalkRoute findWalkRoute(const ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& goal, const WalkRouteParams& params)
{
    WalkRoute route;
    const bool paramsOk = std::isfinite(params.stepMetres) && params.stepMetres >= 0.5 && std::isfinite(params.checkMetres) && params.checkMetres >= 0.05
        && std::isfinite(params.clearanceMetres) && params.clearanceMetres >= 0.0 && std::isfinite(params.marginMetres) && params.marginMetres >= 0.0
        && params.stepMetres <= kMaxParamMetres && std::isfinite(params.goalRadiusMetres) && params.goalRadiusMetres >= 0.0
        && params.goalRadiusMetres <= kMaxParamMetres && params.maxNodes >= 16 && params.maxNodes <= kMaxNodesLimit;
    if (!paramsOk || !finite2(from) || !finite2(goal) || !onMap(walking, from) || !onMap(walking, goal)) {
        return route;
    }

    const double size = walking.sizeMetres();
    const double minX = std::min(from.x, goal.x) - params.marginMetres;
    const double maxX = std::max(from.x, goal.x) + params.marginMetres;
    const double minZ = std::min(from.z, goal.z) - params.marginMetres;
    const double maxZ = std::max(from.z, goal.z) + params.marginMetres;
    const bool wholeWorld = minX <= 0.0 && minZ <= 0.0 && maxX >= size && maxZ >= size;

    Search search = searchWindow(walking, from, goal, makeGrid(walking, minX, minZ, maxX, maxZ, params), params.goalRadiusMetres, params.checkMetres);
    route.nodesSearched = search.searched;
    bool retried = false;
    if (!search.found && !wholeWorld && search.startOk && search.goalOk) {
        retried = true;
        search = searchWindow(walking, from, goal, makeGrid(walking, 0.0, 0.0, size, size, params), params.goalRadiusMetres, params.checkMetres);
        route.nodesSearched += search.searched;
    }
    if (retried && (!search.startOk || !search.goalOk)) {
        route.failure = WalkRouteFailure::NoGroundRoute; // the coarser grid of the whole world lost an end the window had
        return route;
    }
    if (!search.startOk) {
        route.failure = WalkRouteFailure::StartBlocked;
        return route;
    }
    if (!search.goalOk) {
        route.failure = WalkRouteFailure::GoalBlocked;
        return route;
    }
    if (!search.found) {
        route.failure = WalkRouteFailure::NoGroundRoute;
        route.reachableSquareMetres = search.reachableSquareMetres;
        route.boundarySteep = search.steep;
        route.boundaryDeepWater = search.deepWater;
        route.boundaryMapEdge = search.mapEdge;
        return route;
    }

    std::vector<glm::dvec3> nodes;
    nodes.push_back({from.x, 0.0, from.z});
    nodes.insert(nodes.end(), search.nodes.begin(), search.nodes.end());
    route.waypoints = pull(walking, nodes, params);
    route.waypoints.push_back({goal.x, 0.0, goal.z});
    for (std::size_t i = 1; i < route.waypoints.size(); ++i) {
        route.metres += flat(route.waypoints[i].x - route.waypoints[i - 1].x, route.waypoints[i].z - route.waypoints[i - 1].z);
    }
    route.found = true;
    route.failure = WalkRouteFailure::None;
    return route;
}

std::string describeEnclosure(const WalkRoute& route)
{
    const double total = static_cast<double>(route.boundarySteep + route.boundaryDeepWater + route.boundaryMapEdge);
    if (route.found || !(total > 0.0)) {
        return "not enclosed";
    }
    const auto percent = [&](std::size_t n) { return 100.0 * static_cast<double>(n) / total; };
    return std::format("enclosed: {:.2f} km2 reachable, bounded {:.0f}% by cliffs, {:.0f}% by deep water, {:.0f}% by the map edge", route.reachableSquareMetres / 1.0e6,
        percent(route.boundarySteep), percent(route.boundaryDeepWater), percent(route.boundaryMapEdge));
}

double firstBlockOnLine(const ForgedWalking& walking, const glm::dvec3& from, const glm::dvec3& goal, WalkBlock& block)
{
    block = WalkBlock::None;
    if (!finite2(from) || !finite2(goal)) {
        block = WalkBlock::OffMap;
        return 0.0;
    }
    const double length = flat(goal.x - from.x, goal.z - from.z);
    const int pieces = static_cast<int>(std::min(std::ceil(length), 1.0e6));
    for (int i = 0; i <= pieces; ++i) {
        const double t = pieces > 0 ? static_cast<double>(i) / pieces : 0.0;
        const WalkAssessment a = walking.assess(from.x + (goal.x - from.x) * t, from.z + (goal.z - from.z) * t);
        if (!a.walkable()) {
            block = a.block;
            return length * t;
        }
    }
    return -1.0;
}

}
