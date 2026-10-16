#include "ichor/graph/routing.h"

#include "ichor/graph/mission.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <limits>
#include <set>

namespace ichor::graph::routing {

namespace {

constexpr std::array<std::array<int, 3>, 6> kDirections = {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}}};

struct Window {
    std::int64_t lo[3] = {0, 0, 0};
    std::int64_t hi[3] = {0, 0, 0};

    bool contains(const Cell& c) const
    {
        const std::int64_t v[3] = {c.x, c.y, c.floor};
        for (int i = 0; i < 3; ++i)
            if (v[i] < lo[i] || v[i] > hi[i])
                return false;
        return true;
    }
};

Window windowOf(const std::map<NodeId, Cell>& cells, const Params& params)
{
    const std::int64_t extent = std::clamp<std::int64_t>(params.maxExtent, 1, embedding::kMaxExtentLimit);
    const std::int64_t floors = std::clamp<std::int64_t>(params.maxFloors, 1, embedding::kMaxFloorsLimit);
    const std::int64_t limit[3] = {extent, extent, floors};
    const std::int64_t margin = std::min<std::int64_t>(params.margin, kMaxMargin);
    Window w;
    bool first = true;
    for (const auto& [id, c] : cells) {
        const std::int64_t v[3] = {c.x, c.y, c.floor};
        for (int i = 0; i < 3; ++i) {
            w.lo[i] = first ? v[i] : std::min(w.lo[i], v[i]);
            w.hi[i] = first ? v[i] : std::max(w.hi[i], v[i]);
        }
        first = false;
    }
    for (int i = 0; i < 3; ++i) {
        const std::int64_t spare = std::max<std::int64_t>(0, limit[i] - (w.hi[i] - w.lo[i] + 1));
        const std::int64_t below = std::min(margin, spare / 2);
        const std::int64_t above = std::min(margin, spare - below);
        w.lo[i] -= below;
        w.hi[i] += above;
    }
    return w;
}

bool touching(const Cell& a, const Cell& b)
{
    return std::llabs(std::int64_t{a.x} - b.x) + std::llabs(std::int64_t{a.y} - b.y) + std::llabs(std::int64_t{a.floor} - b.floor) == 1;
}

Step stepBetween(const Cell& a, const Cell& b, bool oneWay)
{
    if (a.floor == b.floor)
        return Step::Walk;
    if (!oneWay)
        return Step::Stair;
    return b.floor < a.floor ? Step::Drop : Step::Climb;
}

bool routeCorridor(const Cell& from, const Cell& to, const Window& window, const std::set<Cell>& blocked, std::vector<Cell>& out)
{
    std::map<Cell, Cell> parent;
    std::deque<Cell> queue{from};
    parent[from] = from;
    std::uint64_t visits = 0;
    while (!queue.empty()) {
        const Cell at = queue.front();
        queue.pop_front();
        for (const auto& d : kDirections) {
            if (++visits > kMaxVisits)
                return false;
            const Cell next{at.x + d[0], at.y + d[1], at.floor + d[2]};
            if (!window.contains(next) || parent.count(next) != 0)
                continue;
            if (next == to) {
                parent[next] = at;
                out.clear();
                for (Cell c = next; c != from; c = parent.at(c))
                    out.push_back(c);
                out.push_back(from);
                std::reverse(out.begin(), out.end());
                return true;
            }
            if (blocked.count(next) != 0)
                continue;
            parent[next] = at;
            queue.push_back(next);
        }
    }
    return false;
}

Result fail(std::string message)
{
    Result r;
    r.error = std::move(message);
    return r;
}

constexpr std::size_t kMaxRetries = 24;

bool oneWayType(const std::string& type)
{
    return type == mission::kShortcut;
}

}

Result route(const Graph& graph, const std::map<NodeId, Cell>& cells, const Params& params)
{
    embedding::Params footprint;
    footprint.maxExtent = params.maxExtent;
    footprint.maxFloors = params.maxFloors;
    const std::vector<std::string> problems = embedding::check(graph, cells, footprint);
    if (!problems.empty())
        return fail("embedding rejected: " + problems.front());

    constexpr std::int64_t kGuard = kMaxMargin + 2;
    for (const auto& [id, c] : cells)
        for (const std::int64_t v : {std::int64_t{c.x}, std::int64_t{c.y}, std::int64_t{c.floor}})
            if (v < std::numeric_limits<std::int32_t>::min() + kGuard || v > std::numeric_limits<std::int32_t>::max() - kGuard)
                return fail("embedding too close to the coordinate limits");

    const Window window = windowOf(cells, params);
    std::vector<EdgeId> order;
    for (const auto& [id, e] : graph.edges())
        if (e.type != mission::kPath)
            order.push_back(id);

    std::string lastError;
    for (std::size_t attempt = 0; attempt <= kMaxRetries; ++attempt) {
        std::set<Cell> blocked;
        for (const auto& [id, c] : cells)
            blocked.insert(c);
        std::map<EdgeId, std::vector<Cell>> routed;
        EdgeId failed = 0;
        for (const EdgeId id : order) {
            const Edge& e = *graph.edge(id);
            const Cell a = cells.at(e.from);
            const Cell b = cells.at(e.to);
            std::vector<Cell> path;
            if (a == b) {
                return fail("edge " + std::to_string(id) + " joins a room to itself");
            }
            if (!routeCorridor(a, b, window, blocked, path)) {
                failed = id;
                break;
            }
            for (std::size_t i = 1; i + 1 < path.size(); ++i)
                blocked.insert(path[i]);
            routed[id] = std::move(path);
        }
        if (failed != 0) {
            lastError = "no route for edge " + std::to_string(failed);
            if (failed == order.front())
                break;
            order.erase(std::find(order.begin(), order.end(), failed));
            order.insert(order.begin(), failed);
            continue;
        }

        Result result;
        for (const auto& [id, e] : graph.edges()) {
            Connection conn;
            conn.edge = id;
            conn.from = e.from;
            conn.to = e.to;
            conn.path = e.type == mission::kPath ? std::vector<Cell>{cells.at(e.from), cells.at(e.to)} : std::move(routed.at(id));
            const bool oneWay = oneWayType(e.type);
            for (std::size_t i = 0; i + 1 < conn.path.size(); ++i)
                conn.steps.push_back(stepBetween(conn.path[i], conn.path[i + 1], oneWay));
            result.connections.push_back(std::move(conn));
        }
        return result;
    }
    return fail(lastError);
}

std::vector<std::string> check(const Graph& graph, const std::map<NodeId, Cell>& cells, const Result& result, const Params& params)
{
    std::vector<std::string> errors;
    const Window window = windowOf(cells, params);
    std::set<Cell> roomCells;
    for (const auto& [id, c] : cells)
        roomCells.insert(c);
    std::set<Cell> used;
    std::map<EdgeId, int> perEdge;
    std::map<NodeId, std::vector<NodeId>> moves;

    for (const Connection& c : result.connections) {
        const std::string name = "connection " + std::to_string(c.edge);
        const Edge* e = graph.edge(c.edge);
        if (e == nullptr || ++perEdge[c.edge] > 1 || e->from != c.from || e->to != c.to) {
            errors.push_back(name + " does not match a graph edge");
            continue;
        }
        const auto a = cells.find(c.from);
        const auto b = cells.find(c.to);
        if (a == cells.end() || b == cells.end() || c.path.size() < 2 || c.path.front() != a->second || c.path.back() != b->second) {
            errors.push_back(name + " does not run from its first room to its second");
            continue;
        }
        if (c.steps.size() + 1 != c.path.size()) {
            errors.push_back(name + " has a wrong number of steps");
            continue;
        }
        const bool oneWay = oneWayType(e->type);
        if (e->type == mission::kPath && c.path.size() != 2)
            errors.push_back(name + " is a path link but is not a single step");
        bool walkable = true;
        for (std::size_t i = 0; i + 1 < c.path.size(); ++i) {
            if (!touching(c.path[i], c.path[i + 1])) {
                errors.push_back(name + " has a gap");
                walkable = false;
                break;
            }
            if (c.steps[i] != stepBetween(c.path[i], c.path[i + 1], oneWay))
                errors.push_back(name + " has a step that does not match its cells");
        }
        for (std::size_t i = 1; i + 1 < c.path.size(); ++i) {
            if (roomCells.count(c.path[i]) != 0)
                errors.push_back(name + " runs through a room");
            if (!used.insert(c.path[i]).second)
                errors.push_back(name + " overlaps another connection");
            if (!window.contains(c.path[i]))
                errors.push_back(name + " leaves the footprint");
        }
        if (walkable) {
            moves[c.from].push_back(c.to);
            if (!oneWay)
                moves[c.to].push_back(c.from);
        }
    }
    for (const auto& [id, e] : graph.edges())
        if (perEdge.count(id) == 0)
            errors.push_back("edge " + std::to_string(id) + " has no connection");

    std::set<NodeId> reached;
    std::vector<NodeId> stack;
    for (const auto& [id, node] : graph.nodes())
        if (node.type == mission::kEntry && reached.insert(id).second)
            stack.push_back(id);
    while (!stack.empty()) {
        const NodeId at = stack.back();
        stack.pop_back();
        for (const NodeId n : moves[at])
            if (reached.insert(n).second)
                stack.push_back(n);
    }
    for (const auto& [id, node] : graph.nodes())
        if (reached.count(id) == 0)
            errors.push_back("room " + std::to_string(id) + " is not reachable along the connections");
    return errors;
}

Layout3d layout(const Graph& graph, const embedding::Params& embedParams, const Params& routeParams, std::uint32_t attempts)
{
    Layout3d out;
    embedding::Params ep = embedParams;
    const std::uint32_t tries = std::clamp<std::uint32_t>(attempts, 1, kMaxLayoutAttempts);
    for (std::uint32_t i = 0; i < tries; ++i) {
        embedding::Result e = embedding::embed(graph, ep);
        if (!e.error.empty()) {
            out.error = e.error;
            return out;
        }
        Result r = route(graph, e.cells, routeParams);
        if (r.error.empty()) {
            out.cells = std::move(e.cells);
            out.routing = std::move(r);
            out.error.clear();
            return out;
        }
        out.error = r.error;
        ++ep.seed;
    }
    return out;
}

}
