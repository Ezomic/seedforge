#include "ichor/graph/guide_light_plan.h"

#include "ichor/graph/mission.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <utility>

namespace ichor::graph::guidelight {

namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

bool unit(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

Result failure(std::string message)
{
    Result r;
    r.error = std::move(message);
    return r;
}

std::string validate(const Params& p)
{
    const float values[] = {p.pathFloor, p.ambientFloor, p.offPathFactor, p.neutralLevel, p.pathWarmthFloor, p.ambientWarmth, p.neutralWarmth};
    for (const float v : values) {
        if (!unit(v)) {
            return "light parameters must be finite numbers in 0..1";
        }
    }
    if (p.ambientFloor > p.pathFloor || p.ambientWarmth > p.pathWarmthFloor) {
        return "ambient floors must not exceed the path floors";
    }
    return {};
}

float ramp(float base, std::uint32_t step, std::uint32_t steps)
{
    if (steps == 0 || step >= steps) {
        return 1.0f;
    }
    const float t = static_cast<float>(step) / static_cast<float>(steps);
    return std::min(1.0f, base + (1.0f - base) * t);
}

void setNeutral(Result& r, const std::vector<NodeId>& ids, const Params& p)
{
    r.guided = false;
    r.criticalPath.clear();
    r.rooms.clear();
    for (const NodeId id : ids) {
        RoomLight l;
        l.room = id;
        l.level = p.neutralLevel;
        l.warmth = p.neutralWarmth;
        r.rooms.push_back(l);
    }
}

} // namespace

Result plan(const Graph& graph, const Params& params)
{
    if (graph.nodes().empty()) {
        return failure("graph has no rooms");
    }
    if (graph.nodes().size() > kMaxRooms) {
        return failure("graph has too many rooms");
    }
    if (const std::string bad = validate(params); !bad.empty()) {
        return failure(bad);
    }

    std::vector<NodeId> ids;
    ids.reserve(graph.nodes().size());
    std::map<NodeId, std::size_t> index;
    NodeId entryId = params.entry;
    for (const auto& [id, node] : graph.nodes()) {
        index[id] = ids.size();
        ids.push_back(id);
        if (entryId == 0 && node.type == mission::kEntry) {
            entryId = id;
        }
    }
    if (!index.count(params.objective)) {
        return failure("objective is not a room of the graph");
    }
    if (entryId == 0) {
        return failure("graph has no entry room");
    }
    if (!index.count(entryId)) {
        return failure("entry is not a room of the graph");
    }

    const std::size_t count = ids.size();
    std::vector<std::vector<std::size_t>> adj(count);
    for (const auto& [eid, edge] : graph.edges()) {
        if (edge.type != mission::kPath) {
            continue;
        }
        const auto from = index.find(edge.from);
        const auto to = index.find(edge.to);
        if (from == index.end() || to == index.end() || from->second == to->second) {
            continue;
        }
        adj[from->second].push_back(to->second);
        adj[to->second].push_back(from->second);
    }
    for (auto& list : adj) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    Result r;
    const std::size_t objective = index[params.objective];
    const std::size_t entry = index[entryId];

    std::vector<std::uint32_t> dist(count, kUnreached);
    std::deque<std::size_t> queue;
    dist[objective] = 0;
    queue.push_back(objective);
    while (!queue.empty()) {
        const std::size_t at = queue.front();
        queue.pop_front();
        for (const std::size_t next : adj[at]) {
            if (dist[next] == kUnreached) {
                dist[next] = dist[at] + 1;
                queue.push_back(next);
            }
        }
    }

    if (!params.enabled || dist[entry] == kUnreached) {
        setNeutral(r, ids, params);
        return r;
    }

    std::vector<std::size_t> towards(count, kNone);
    for (std::size_t i = 0; i < count; ++i) {
        if (dist[i] == 0 || dist[i] == kUnreached) {
            continue;
        }
        for (const std::size_t next : adj[i]) {
            if (dist[next] + 1 == dist[i]) {
                towards[i] = next;
                break;
            }
        }
    }

    const std::uint32_t steps = dist[entry];
    std::vector<std::size_t> path;
    for (std::size_t at = entry; at != kNone; at = towards[at]) {
        path.push_back(at);
    }

    std::vector<float> level(count, 0.0f);
    std::vector<float> warmth(count, 0.0f);
    std::vector<char> done(count, 0);
    std::vector<bool> onPath(count, false);
    queue.clear();
    for (std::size_t step = 0; step < path.size(); ++step) {
        const std::size_t at = path[step];
        level[at] = ramp(params.pathFloor, static_cast<std::uint32_t>(step), steps);
        warmth[at] = ramp(params.pathWarmthFloor, static_cast<std::uint32_t>(step), steps);
        done[at] = 1;
        onPath[at] = true;
        queue.push_back(at);
        r.criticalPath.push_back(ids[at]);
    }

    // Off-path rooms inherit from the critical room they branch from.
    std::vector<std::size_t> anchor(count, kNone);
    for (const std::size_t at : path) {
        anchor[at] = at;
    }
    while (!queue.empty()) {
        const std::size_t at = queue.front();
        queue.pop_front();
        for (const std::size_t next : adj[at]) {
            if (!done[next]) {
                done[next] = 1;
                anchor[next] = anchor[at];
                level[next] = std::max(params.ambientFloor, params.offPathFactor * level[anchor[next]]);
                warmth[next] = std::max(params.ambientWarmth, params.offPathFactor * warmth[anchor[next]]);
                queue.push_back(next);
            }
        }
    }

    r.guided = true;
    r.rooms.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        RoomLight l;
        l.room = ids[i];
        l.onCriticalPath = onPath[i];
        if (done[i]) {
            l.level = level[i];
            l.warmth = warmth[i];
            l.distance = dist[i];
            l.towards = towards[i] == kNone ? 0 : ids[towards[i]];
        } else {
            l.level = params.neutralLevel;
            l.warmth = params.neutralWarmth;
        }
        r.rooms.push_back(l);
    }
    return r;
}

float meanLevel(const Result& result, bool onPath)
{
    double sum = 0.0;
    std::size_t n = 0;
    for (const RoomLight& l : result.rooms) {
        if (l.onCriticalPath == onPath) {
            sum += static_cast<double>(l.level);
            ++n;
        }
    }
    return n == 0 ? 0.0f : static_cast<float>(sum / static_cast<double>(n));
}

} // namespace ichor::graph::guidelight
