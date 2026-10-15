#include "ichor/graph/rewrite.h"

#include "ichor/core/rng.h"

#include <algorithm>
#include <cstdint>

namespace ichor::graph {

namespace {

bool typeMatches(const std::string& pattern, const std::string& actual)
{
    return pattern.empty() || pattern == actual;
}

bool edgeExists(const Graph& graph, NodeId from, NodeId to, const std::string& type)
{
    return std::any_of(graph.edges().begin(), graph.edges().end(), [&](const auto& entry) {
        const Edge& e = entry.second;
        return e.from == from && e.to == to && e.type == type;
    });
}

void extend(const Graph& graph, const Rule& rule, std::vector<NodeId>& current, std::vector<std::vector<NodeId>>& out)
{
    const std::size_t depth = current.size();
    if (depth == rule.matchNodes.size()) {
        out.push_back(current);
        return;
    }
    for (const auto& [id, node] : graph.nodes()) {
        if (!typeMatches(rule.matchNodes[depth].type, node.type)
            || std::find(current.begin(), current.end(), id) != current.end()) {
            continue;
        }
        current.push_back(id);
        bool ok = true;
        for (const EdgePattern& e : rule.matchEdges) {
            if (std::max(e.from, e.to) == static_cast<int>(depth)
                && !edgeExists(graph, current[static_cast<std::size_t>(e.from)], current[static_cast<std::size_t>(e.to)], e.type)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            extend(graph, rule, current, out);
        }
        current.pop_back();
    }
}

NodeId resolve(int index, const std::vector<NodeId>& match, const std::vector<NodeId>& added)
{
    const auto i = static_cast<std::size_t>(index);
    return i < match.size() ? match[i] : added[i - match.size()];
}

void apply(Graph& graph, const Rule& rule, const std::vector<NodeId>& match)
{
    for (const EdgePattern& e : rule.matchEdges) {
        const NodeId from = match[static_cast<std::size_t>(e.from)];
        const NodeId to = match[static_cast<std::size_t>(e.to)];
        for (const auto& [id, edge] : graph.edges()) {
            if (edge.from == from && edge.to == to && edge.type == e.type) {
                graph.removeEdge(id);
                break;
            }
        }
    }
    for (int index : rule.deleteNodes) {
        graph.removeNode(match[static_cast<std::size_t>(index)]);
    }
    for (const Retype& r : rule.retypes) {
        graph.node(match[static_cast<std::size_t>(r.node)])->type = r.type;
    }
    std::vector<NodeId> added;
    for (const std::string& type : rule.addNodes) {
        added.push_back(graph.addNode(type));
    }
    for (const EdgePattern& e : rule.addEdges) {
        graph.addEdge(resolve(e.from, match, added), resolve(e.to, match, added), e.type);
    }
}

bool deletes(const Rule& rule, int node)
{
    return std::find(rule.deleteNodes.begin(), rule.deleteNodes.end(), node) != rule.deleteNodes.end();
}

}

std::string validate(const Rule& rule)
{
    const std::string prefix = "rule '" + rule.name + "': ";
    const auto matched = static_cast<int>(rule.matchNodes.size());
    const auto total = matched + static_cast<int>(rule.addNodes.size());
    if (rule.matchNodes.empty()) {
        return prefix + "empty pattern";
    }
    if (rule.weight == 0) {
        return prefix + "zero weight";
    }
    for (const EdgePattern& e : rule.matchEdges) {
        if (e.from < 0 || e.from >= matched || e.to < 0 || e.to >= matched) {
            return prefix + "match edge endpoint out of range";
        }
    }
    for (int n : rule.deleteNodes) {
        if (n < 0 || n >= matched) {
            return prefix + "deleted node out of range";
        }
    }
    for (const Retype& r : rule.retypes) {
        if (r.node < 0 || r.node >= matched) {
            return prefix + "retyped node out of range";
        }
        if (deletes(rule, r.node)) {
            return prefix + "retyped node is also deleted";
        }
    }
    for (const EdgePattern& e : rule.addEdges) {
        for (int end : {e.from, e.to}) {
            if (end < 0 || end >= total) {
                return prefix + "added edge endpoint out of range";
            }
            if (deletes(rule, end)) {
                return prefix + "added edge touches a deleted node";
            }
        }
    }
    return {};
}

std::vector<std::vector<NodeId>> findMatches(const Graph& graph, const Rule& rule)
{
    std::vector<std::vector<NodeId>> out;
    if (!validate(rule).empty()) {
        return out;
    }
    std::vector<NodeId> current;
    extend(graph, rule, current, out);
    return out;
}

RewriteResult rewrite(Graph& graph, const RuleSet& rules, const RewriteOptions& options)
{
    RewriteResult result;
    std::uint64_t weightSum = 0;
    for (const Rule& rule : rules.rules) {
        result.error = validate(rule);
        weightSum += rule.weight;
        if (result.error.empty() && weightSum > UINT32_MAX) {
            result.error = "rule weights sum past 32 bits";
        }
        if (!result.error.empty()) {
            result.reason = StopReason::InvalidRule;
            result.digest = graph.digest();
            return result;
        }
    }

    Rng rng(options.seed);
    while (result.iterations < options.maxIterations) {
        std::vector<std::vector<std::vector<NodeId>>> matches;
        std::uint32_t totalWeight = 0;
        for (const Rule& rule : rules.rules) {
            matches.push_back(findMatches(graph, rule));
            if (!matches.back().empty()) {
                totalWeight += rule.weight;
            }
        }
        if (totalWeight == 0) {
            result.reason = StopReason::FixedPoint;
            result.digest = graph.digest();
            return result;
        }

        std::uint32_t roll = rng.range(0, totalWeight - 1);
        std::size_t chosen = 0;
        for (std::size_t i = 0; i < rules.rules.size(); ++i) {
            if (matches[i].empty()) {
                continue;
            }
            chosen = i;
            if (roll < rules.rules[i].weight) {
                break;
            }
            roll -= rules.rules[i].weight;
        }

        const auto& candidates = matches[chosen];
        const auto pick = rng.range(0, static_cast<std::uint32_t>(candidates.size() - 1));
        apply(graph, rules.rules[chosen], candidates[pick]);
        result.applied.push_back(static_cast<std::uint32_t>(chosen));
        ++result.iterations;
    }

    result.reason = StopReason::BudgetExhausted;
    result.digest = graph.digest();
    return result;
}

}
