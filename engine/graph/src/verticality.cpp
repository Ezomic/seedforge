#include "ichor/graph/verticality.h"

#include "ichor/graph/mission.h"

#include <algorithm>
#include <deque>
#include <set>

namespace ichor::graph::verticality {

namespace {

struct Endpoints {
    NodeId entry = 0;
    NodeId climax = 0;
    bool ok = false;
};

Endpoints findEndpoints(const Graph& graph)
{
    Endpoints e;
    int entries = 0;
    int climaxes = 0;
    for (const auto& [id, node] : graph.nodes()) {
        if (node.type == mission::kEntry) {
            e.entry = id;
            ++entries;
        } else if (node.type == mission::kClimax) {
            e.climax = id;
            ++climaxes;
        }
    }
    e.ok = entries == 1 && climaxes == 1;
    return e;
}

Connector connectorOf(const Layout& layout, EdgeId id)
{
    const auto it = layout.connectors.find(id);
    return it == layout.connectors.end() ? Connector::Flat : it->second;
}

bool requirementsInRange(const Requirements& req)
{
    return req.minFloors <= kMaxRequired + 1 && req.minLevelChanges <= kMaxRequired && req.minShafts <= kMaxRequired;
}

}

std::vector<EdgeId> criticalPath(const Graph& graph)
{
    const Endpoints ends = findEndpoints(graph);
    if (!ends.ok) {
        return {};
    }
    std::map<NodeId, EdgeId> via;
    std::set<NodeId> seen{ends.entry};
    std::deque<NodeId> queue{ends.entry};
    while (!queue.empty() && !seen.contains(ends.climax)) {
        const NodeId at = queue.front();
        queue.pop_front();
        for (const auto& [id, edge] : graph.edges()) {
            if (edge.from == at && edge.type == mission::kPath && seen.insert(edge.to).second) {
                via[edge.to] = id;
                queue.push_back(edge.to);
            }
        }
    }
    if (!seen.contains(ends.climax)) {
        return {};
    }
    std::vector<EdgeId> path;
    for (NodeId at = ends.climax; at != ends.entry;) {
        const EdgeId id = via.at(at);
        path.push_back(id);
        at = graph.edge(id)->from;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<std::string> check(const Graph& graph, const Layout& layout, const Requirements& req)
{
    std::vector<std::string> out;
    const Endpoints ends = findEndpoints(graph);
    if (!ends.ok) {
        out.push_back("graph needs exactly one entry and one climax");
    }

    std::set<std::int32_t> distinct;
    for (const auto& [id, node] : graph.nodes()) {
        const auto it = layout.floors.find(id);
        if (it == layout.floors.end()) {
            out.push_back("room " + std::to_string(id) + " has no floor");
        } else if (it->second < -kMaxFloor || it->second > kMaxFloor) {
            out.push_back("room " + std::to_string(id) + " floor is out of range");
        } else {
            distinct.insert(it->second);
        }
    }
    for (const auto& [id, floor] : layout.floors) {
        if (graph.node(id) == nullptr) {
            out.push_back("layout has a floor for unknown room " + std::to_string(id));
        }
    }
    for (const auto& [id, connector] : layout.connectors) {
        if (graph.edge(id) == nullptr) {
            out.push_back("layout has a connector for unknown edge " + std::to_string(id));
        }
    }

    const auto floorOf = [&](NodeId id) -> const std::int32_t* {
        const auto it = layout.floors.find(id);
        if (it == layout.floors.end() || it->second < -kMaxFloor || it->second > kMaxFloor) {
            return nullptr;
        }
        return &it->second;
    };

    std::uint32_t shafts = 0;
    for (const auto& [id, edge] : graph.edges()) {
        const std::int32_t* a = floorOf(edge.from);
        const std::int32_t* b = floorOf(edge.to);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        const Connector c = connectorOf(layout, id);
        const std::int64_t delta = static_cast<std::int64_t>(*b) - *a;
        const std::string name = "edge " + std::to_string(id);
        if (delta == 0 && c != Connector::Flat) {
            out.push_back(name + " has a vertical connector between rooms on the same floor");
        } else if (delta != 0 && c == Connector::Flat) {
            out.push_back(name + " joins different floors without a stair, shaft or drop");
        } else if (c == Connector::Stair && (delta > 1 || delta < -1)) {
            out.push_back(name + " is a stair spanning more than one floor");
        } else if (c == Connector::Drop && delta > 0) {
            out.push_back(name + " is a drop that rises");
        }
        if (c == Connector::Shaft && delta != 0) {
            ++shafts;
        }
    }

    if (!requirementsInRange(req)) {
        out.push_back("requirements exceed the supported maximum");
        return out;
    }
    if (distinct.size() < req.minFloors) {
        out.push_back("layout uses " + std::to_string(distinct.size()) + " floors, needs " + std::to_string(req.minFloors));
    }
    if (shafts < req.minShafts) {
        out.push_back("layout has " + std::to_string(shafts) + " shafts, needs " + std::to_string(req.minShafts));
    }

    if (ends.ok) {
        const std::vector<EdgeId> path = criticalPath(graph);
        if (path.empty()) {
            out.push_back("no path from entry to climax");
        } else {
            std::uint32_t changes = 0;
            for (const EdgeId id : path) {
                const Edge* edge = graph.edge(id);
                const std::int32_t* a = floorOf(edge->from);
                const std::int32_t* b = floorOf(edge->to);
                if (a == nullptr || b == nullptr || *a == *b) {
                    continue;
                }
                ++changes;
                const Connector c = connectorOf(layout, id);
                if (c != Connector::Stair && c != Connector::Shaft) {
                    out.push_back("critical path edge " + std::to_string(id) + " changes floor without a two-way stair or shaft");
                }
            }
            if (changes < req.minLevelChanges) {
                out.push_back("critical path changes level " + std::to_string(changes) + " times, needs " + std::to_string(req.minLevelChanges));
            }
        }
    }
    return out;
}

EnforceResult enforce(const Graph& graph, const Requirements& req)
{
    EnforceResult result;
    if (!requirementsInRange(req)) {
        result.error = "requirements exceed the supported maximum";
        return result;
    }
    const std::vector<EdgeId> path = criticalPath(graph);
    if (path.empty()) {
        result.error = "no path from entry to climax";
        return result;
    }
    const std::uint32_t changes = std::max(req.minLevelChanges, req.minFloors > 0 ? req.minFloors - 1 : 0u);
    if (changes > path.size()) {
        result.error = "critical path has " + std::to_string(path.size()) + " edges, cannot hold " + std::to_string(changes) + " level changes";
        return result;
    }

    std::set<EdgeId> changeAt;
    for (std::uint32_t j = 0; j < changes; ++j) {
        changeAt.insert(path[static_cast<std::size_t>(j) * path.size() / changes]);
    }

    Layout& layout = result.layout;
    std::deque<NodeId> queue;
    std::int32_t floor = 0;
    const NodeId start = graph.edge(path.front())->from;
    layout.floors[start] = floor;
    queue.push_back(start);
    for (const EdgeId id : path) {
        const Edge* edge = graph.edge(id);
        if (changeAt.contains(id)) {
            ++floor;
        }
        layout.floors[edge->to] = floor;
        queue.push_back(edge->to);
    }
    while (!queue.empty()) {
        const NodeId at = queue.front();
        queue.pop_front();
        for (const auto& [id, edge] : graph.edges()) {
            if (edge.from != at && edge.to != at) {
                continue;
            }
            const NodeId other = edge.from == at ? edge.to : edge.from;
            if (layout.floors.try_emplace(other, layout.floors.at(at)).second) {
                queue.push_back(other);
            }
        }
    }
    for (const auto& [id, node] : graph.nodes()) {
        layout.floors.try_emplace(id, 0);
    }

    std::vector<EdgeId> vertical;
    for (const auto& [id, edge] : graph.edges()) {
        const std::int64_t delta = static_cast<std::int64_t>(layout.floors.at(edge.to)) - layout.floors.at(edge.from);
        if (delta != 0) {
            vertical.push_back(id);
            layout.connectors[id] = (delta == 1 || delta == -1) ? Connector::Stair : Connector::Shaft;
        }
    }
    if (vertical.size() < req.minShafts) {
        result.error = "only " + std::to_string(vertical.size()) + " vertical edges, cannot place " + std::to_string(req.minShafts) + " shafts";
        return result;
    }
    for (std::uint32_t i = 0; i < req.minShafts; ++i) {
        layout.connectors[vertical[i]] = Connector::Shaft;
    }
    return result;
}

}
