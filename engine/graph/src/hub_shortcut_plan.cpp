#include "ichor/graph/hub_shortcut_plan.h"

#include "ichor/core/rng.h"
#include "ichor/graph/mission.h"

#include <algorithm>
#include <deque>
#include <map>

namespace ichor::graph::hubshortcut {

namespace {

using namespace mission;

using Adjacency = std::map<NodeId, std::vector<NodeId>>;

Adjacency pathEdges(const Graph& graph)
{
    Adjacency adjacency;
    for (const auto& [id, edge] : graph.edges()) {
        if (edge.type == kPath) {
            adjacency[edge.from].push_back(edge.to);
        }
    }
    return adjacency;
}

NodeId findEntry(const Graph& graph)
{
    for (const auto& [id, node] : graph.nodes()) {
        if (node.type == kEntry) {
            return id;
        }
    }
    return 0;
}

std::map<NodeId, std::uint32_t> depthFrom(const Adjacency& adjacency, NodeId start)
{
    std::map<NodeId, std::uint32_t> depth{{start, 0}};
    std::deque<NodeId> queue{start};
    while (!queue.empty()) {
        const NodeId at = queue.front();
        queue.pop_front();
        const auto out = adjacency.find(at);
        if (out == adjacency.end()) {
            continue;
        }
        for (const NodeId next : out->second) {
            if (depth.emplace(next, depth.at(at) + 1).second) {
                queue.push_back(next);
            }
        }
    }
    return depth;
}

// Reachable from the entry along path and shortcut edges with every gate shut.
std::set<NodeId> shutReach(const Graph& graph, NodeId entry)
{
    std::map<NodeId, std::vector<NodeId>> out;
    for (const auto& [id, edge] : graph.edges()) {
        if (edge.type == kPath || edge.type == kShortcut) {
            out[edge.from].push_back(edge.to);
        }
    }
    std::set<NodeId> seen{entry};
    std::deque<NodeId> queue{entry};
    while (!queue.empty()) {
        const NodeId at = queue.front();
        queue.pop_front();
        for (const NodeId next : out[at]) {
            if (graph.node(next)->type != kGate && seen.insert(next).second) {
                queue.push_back(next);
            }
        }
    }
    return seen;
}

bool hasShortcutFrom(const Graph& graph, NodeId from)
{
    return std::any_of(graph.edges().begin(), graph.edges().end(), [&](const auto& pair) {
        return pair.second.type == kShortcut && pair.second.from == from;
    });
}

template <typename T>
void shuffle(std::vector<T>& items, Rng& rng)
{
    for (std::size_t i = items.size(); i > 1; --i) {
        std::swap(items[i - 1], items[rng.range(0, static_cast<std::uint32_t>(i - 1))]);
    }
}

std::vector<NodeId> chooseHubs(const Graph& graph, NodeId entry, std::uint32_t maxHubs, std::uint64_t seed)
{
    std::map<NodeId, std::uint32_t> degree;
    for (const auto& [id, edge] : graph.edges()) {
        if (edge.type == kPath) {
            ++degree[edge.from];
            ++degree[edge.to];
        }
    }
    std::vector<NodeId> candidates;
    for (const auto& [id, node] : graph.nodes()) {
        const bool junction = node.type == kJunction && degree[id] >= 3;
        const bool start = id == entry && degree[id] >= 2;
        if (junction || start) {
            candidates.push_back(id);
        }
    }
    Rng rng = Rng(seed).derive("hub_shortcut/hubs");
    shuffle(candidates, rng);
    std::stable_sort(candidates.begin(), candidates.end(), [&](NodeId a, NodeId b) { return degree[a] > degree[b]; });
    candidates.resize(std::min<std::size_t>(candidates.size(), maxHubs));
    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

}

std::string refuseShortcut(const Graph& mission, NodeId farSide, NodeId hub)
{
    if (mission.node(farSide) == nullptr || mission.node(hub) == nullptr) {
        return "unknown node";
    }
    if (farSide == hub) {
        return "a shortcut cannot lead to its own source";
    }
    const NodeId entry = findEntry(mission);
    if (entry == 0) {
        return "the mission has no entry";
    }
    const Adjacency adjacency = pathEdges(mission);
    const auto depth = depthFrom(adjacency, entry);
    const auto from = depth.find(farSide);
    const auto to = depth.find(hub);
    if (from == depth.end() || to == depth.end() || to->second >= from->second || !depthFrom(adjacency, hub).contains(farSide)) {
        return "the hub is not a path ancestor of the far side";
    }
    for (const auto& [id, edge] : mission.edges()) {
        if (edge.type == kShortcut && edge.from == farSide && edge.to == hub) {
            return "the shortcut already exists";
        }
    }
    const std::set<NodeId> shut = shutReach(mission, entry);
    if (shut.contains(farSide) && !shut.contains(hub)) {
        return "the shortcut would bypass a gate";
    }
    return {};
}

Plan addHubShortcuts(const Graph& mission, const HubParams& params)
{
    Plan plan;
    if (mission.nodes().size() > kMaxNodes) {
        plan.error = "mission graph is too large";
        return plan;
    }
    if (const std::vector<std::string> problems = validateMission(mission); !problems.empty()) {
        plan.error = "not a valid mission: " + problems.front();
        return plan;
    }
    plan.graph = mission;

    const NodeId entry = findEntry(mission);
    const std::uint32_t maxHubs = std::min(params.maxHubs, kMaxHubs);
    const std::uint32_t perHub = std::min(params.shortcutsPerHub, kMaxShortcutsPerHub);
    const Adjacency adjacency = pathEdges(mission);
    const auto depth = depthFrom(adjacency, entry);

    for (const NodeId hub : chooseHubs(mission, entry, maxHubs, params.seed)) {
        std::vector<NodeId> farSides;
        for (const auto& [id, hubDepth] : depthFrom(adjacency, hub)) {
            const std::string& type = mission.node(id)->type;
            const auto at = depth.find(id);
            if (id != hub && at != depth.end() && at->second >= depth.at(hub) + 2 && type != kGate && type != kKey && type != kEntry) {
                farSides.push_back(id);
            }
        }
        Rng rng = Rng(params.seed).derive("hub_shortcut/far/" + std::to_string(hub));
        shuffle(farSides, rng);

        std::uint32_t added = 0;
        for (const NodeId far : farSides) {
            if (added >= perHub) {
                break;
            }
            if (hasShortcutFrom(plan.graph, far) || !refuseShortcut(plan.graph, far, hub).empty()) {
                continue;
            }
            const EdgeId edge = plan.graph.addEdge(far, hub, kShortcut);
            plan.shortcuts.push_back({edge, far, hub});
            ++added;
        }
        if (added > 0) {
            plan.hubs.push_back(hub);
        }
    }

    if (const std::vector<std::string> problems = validateMission(plan.graph); !problems.empty()) {
        plan = Plan{};
        plan.error = "hub shortcuts broke the mission: " + problems.front();
    }
    return plan;
}

bool unlock(const Plan& plan, UnlockState& state, EdgeId edge, NodeId actorAt)
{
    const auto found = std::find_if(plan.shortcuts.begin(), plan.shortcuts.end(), [&](const HubShortcut& s) { return s.edge == edge; });
    if (found == plan.shortcuts.end() || found->farSide != actorAt) {
        return false;
    }
    return state.open.insert(edge).second;
}

bool canTraverse(const Plan& plan, const UnlockState& state, NodeId from, NodeId to)
{
    return std::any_of(plan.shortcuts.begin(), plan.shortcuts.end(), [&](const HubShortcut& s) {
        return s.farSide == from && s.hub == to && state.open.contains(s.edge);
    });
}

}
