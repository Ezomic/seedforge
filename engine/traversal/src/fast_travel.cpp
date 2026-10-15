#include "ichor/traversal/fast_travel.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace ichor {

namespace {

using Reached = std::map<std::string, std::pair<std::uint64_t, std::string>>;

struct Graph {
    std::map<std::string, bool> discovered;
    std::map<std::string, std::map<std::string, std::uint32_t>> edges;
};

void addEdge(Graph& graph, const std::string& x, const std::string& y, std::uint32_t length)
{
    const auto [slot, inserted] = graph.edges[x].emplace(y, length);
    if (!inserted) {
        slot->second = std::min(slot->second, length);
    }
}

Graph buildGraph(const TravelNetwork& network, bool securedOnly)
{
    Graph graph;
    for (const TravelSettlement& settlement : network.settlements) {
        graph.discovered.emplace(settlement.id, settlement.discovered);
    }
    for (const TravelLink& link : network.links) {
        if (link.a == link.b || (securedOnly && !link.secured)) {
            continue;
        }
        if (graph.discovered.count(link.a) == 0 || graph.discovered.count(link.b) == 0) {
            continue;
        }
        addEdge(graph, link.a, link.b, link.length);
        addEdge(graph, link.b, link.a, link.length);
    }
    return graph;
}

// Lengths are 32 bit and a path has fewer edges than there are settlements, so the 64 bit sums cannot overflow.
Reached search(const Graph& graph, const std::string& from, bool discoveredOnly)
{
    Reached reached;
    std::set<std::pair<std::uint64_t, std::string>> frontier;
    std::set<std::string> settled;
    reached[from] = {0, std::string()};
    frontier.emplace(0, from);
    while (!frontier.empty()) {
        const auto [distance, id] = *frontier.begin();
        frontier.erase(frontier.begin());
        if (!settled.insert(id).second) {
            continue;
        }
        const auto edges = graph.edges.find(id);
        if (edges == graph.edges.end()) {
            continue;
        }
        for (const auto& [next, length] : edges->second) {
            if (settled.count(next) != 0 || (discoveredOnly && !graph.discovered.at(next))) {
                continue;
            }
            const std::uint64_t candidate = distance + length;
            const auto known = reached.find(next);
            const bool better = known == reached.end() || candidate < known->second.first
                || (candidate == known->second.first && id < known->second.second);
            if (!better) {
                continue;
            }
            if (known != reached.end()) {
                frontier.erase({known->second.first, next});
            }
            reached[next] = {candidate, id};
            frontier.emplace(candidate, next);
        }
    }
    return reached;
}

TravelPlan refuse(TravelRefusal why)
{
    TravelPlan plan;
    plan.refusal = why;
    return plan;
}

}

TravelPlan planFastTravel(const TravelNetwork& network, const std::string& from, const std::string& to)
{
    const Graph secured = buildGraph(network, true);
    if (secured.discovered.count(from) == 0 || secured.discovered.count(to) == 0) {
        return refuse(TravelRefusal::UnknownSettlement);
    }
    if (from == to) {
        return refuse(TravelRefusal::SameSettlement);
    }
    if (!secured.discovered.at(from) || !secured.discovered.at(to)) {
        return refuse(TravelRefusal::NotDiscovered);
    }
    const Reached reached = search(secured, from, true);
    const auto goal = reached.find(to);
    if (goal != reached.end()) {
        TravelPlan plan;
        plan.allowed = true;
        plan.totalLength = goal->second.first;
        for (std::string at = to; at != from; at = reached.at(at).second) {
            plan.path.push_back(at);
        }
        plan.path.push_back(from);
        std::reverse(plan.path.begin(), plan.path.end());
        return plan;
    }
    const Graph all = buildGraph(network, false);
    if (search(all, from, true).count(to) != 0) {
        return refuse(TravelRefusal::RouteCut);
    }
    if (search(all, from, false).count(to) != 0) {
        return refuse(TravelRefusal::NotDiscovered);
    }
    return refuse(TravelRefusal::NoRoad);
}

std::vector<std::string> fastTravelDestinations(const TravelNetwork& network, const std::string& from)
{
    const Graph secured = buildGraph(network, true);
    const auto start = secured.discovered.find(from);
    if (start == secured.discovered.end() || !start->second) {
        return {};
    }
    std::vector<std::string> out;
    for (const auto& entry : search(secured, from, true)) {
        if (entry.first != from) {
            out.push_back(entry.first);
        }
    }
    return out;
}

}
