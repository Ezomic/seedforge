#pragma once

#include "ichor/graph/graph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ichor::graph {

struct NodePattern {
    std::string type;
};

// Endpoints are indices into the rule's node list: match nodes first, then
// the nodes the rule adds.
struct EdgePattern {
    int from = 0;
    int to = 0;
    std::string type;
};

struct Retype {
    int node = 0;
    std::string type;
};

// Pattern to replacement. The match is an injective mapping of matchNodes to
// graph nodes where every matchEdge exists with the same type and direction;
// extra edges between the matched nodes do not prevent a match.
//
// Applying a rule removes every matched edge, deletes deleteNodes (with all
// their edges, matched or not), retypes, then adds addNodes and addEdges.
// Matched nodes that are not deleted keep their ID and their other edges.
struct Rule {
    std::string name;
    std::uint32_t weight = 1;
    std::vector<NodePattern> matchNodes;
    std::vector<EdgePattern> matchEdges;
    std::vector<int> deleteNodes;
    std::vector<Retype> retypes;
    std::vector<std::string> addNodes;
    std::vector<EdgePattern> addEdges;
};

struct RuleSet {
    std::vector<Rule> rules;
};

// Empty when the rule is well formed. An empty node pattern type matches any
// node; an empty edge type matches only edges whose type is empty.
std::string validate(const Rule& rule);

// Every match of the rule's pattern, as graph node IDs per pattern node, in
// lexicographic ID order. Pattern node types match exactly; an empty type
// matches any node.
std::vector<std::vector<NodeId>> findMatches(const Graph& graph, const Rule& rule);

struct RewriteOptions {
    std::uint64_t seed = 0;
    std::uint32_t maxIterations = 100;
};

enum class StopReason : std::uint8_t {
    FixedPoint,
    BudgetExhausted,
    InvalidRule,
};

struct RewriteResult {
    StopReason reason = StopReason::FixedPoint;
    std::uint32_t iterations = 0;
    std::vector<std::uint32_t> applied; // rule index per iteration
    std::string error;
    std::uint64_t digest = 0;
};

// Each iteration picks a rule by weight among those with at least one match,
// then one of its matches uniformly, and applies it. Stops when nothing
// matches or the iteration budget is spent.
RewriteResult rewrite(Graph& graph, const RuleSet& rules, const RewriteOptions& options);

}
