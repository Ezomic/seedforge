#include "ichor/graph/mission.h"

#include "ichor/core/file_io.h"
#include "ichor/core/rng.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <set>

namespace ichor::graph::mission {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxRules = 256;
constexpr std::size_t kMaxListEntries = 64;
constexpr std::size_t kMaxMatchNodes = 4;
constexpr std::size_t kMaxAddNodes = 8;
constexpr std::uintmax_t kMaxFileBytes = 1u << 20;
constexpr std::uint64_t kMaxWeight = 65535;

std::optional<int> parseIndex(std::string_view text)
{
    if (text.empty() || text.size() > 4) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

// "from>to:type"
std::optional<EdgePattern> parseEdge(std::string_view text)
{
    const auto arrow = text.find('>');
    const auto colon = text.find(':');
    if (arrow == std::string_view::npos || colon == std::string_view::npos || colon < arrow) {
        return std::nullopt;
    }
    const auto from = parseIndex(text.substr(0, arrow));
    const auto to = parseIndex(text.substr(arrow + 1, colon - arrow - 1));
    if (!from || !to) {
        return std::nullopt;
    }
    return EdgePattern{*from, *to, std::string(text.substr(colon + 1))};
}

// An absent field is an empty list; a present field must be a short list of strings.
bool readList(const Json& record, const char* key, std::vector<std::string>& out)
{
    const auto found = record.find(key);
    if (found == record.end()) {
        return true;
    }
    if (!found->is_array() || found->size() > kMaxListEntries) {
        return false;
    }
    for (const Json& item : *found) {
        if (!item.is_string()) {
            return false;
        }
        out.push_back(item.get<std::string>());
    }
    return true;
}

std::string parseRule(const Json& record, std::string& phase, Rule& rule)
{
    if (!record.is_object() || !record.contains("id") || !record["id"].is_string() || record["id"].get<std::string>().empty()) {
        return "every record needs a non-empty string id";
    }
    rule.name = record["id"].get<std::string>();
    const auto bad = [&](const std::string& what) { return rule.name + ": " + what; };
    if (!record.contains("phase") || !record["phase"].is_string()) {
        return bad("needs a string phase");
    }
    phase = record["phase"].get<std::string>();
    if (record.contains("weight")) {
        const Json& weight = record["weight"];
        if (!weight.is_number_unsigned() || weight.get<std::uint64_t>() < 1 || weight.get<std::uint64_t>() > kMaxWeight) {
            return bad("weight must be an integer from 1 to 65535");
        }
        rule.weight = weight.get<std::uint32_t>();
    }

    std::vector<std::string> matchNodes;
    std::vector<std::string> matchEdges;
    std::vector<std::string> deletes;
    std::vector<std::string> retypes;
    std::vector<std::string> addEdges;
    if (!readList(record, "match_nodes", matchNodes) || !readList(record, "match_edges", matchEdges)
        || !readList(record, "delete_nodes", deletes) || !readList(record, "retypes", retypes)
        || !readList(record, "add_nodes", rule.addNodes) || !readList(record, "add_edges", addEdges)) {
        return bad("rule fields must be lists of at most 64 strings");
    }

    for (const std::string& type : matchNodes) {
        rule.matchNodes.push_back({type});
    }
    for (const std::string& text : matchEdges) {
        const auto edge = parseEdge(text);
        if (!edge) {
            return bad("bad match edge '" + text + "'");
        }
        rule.matchEdges.push_back(*edge);
    }
    for (const std::string& text : addEdges) {
        const auto edge = parseEdge(text);
        if (!edge) {
            return bad("bad added edge '" + text + "'");
        }
        rule.addEdges.push_back(*edge);
    }
    for (const std::string& text : deletes) {
        const auto index = parseIndex(text);
        if (!index) {
            return bad("bad deleted node '" + text + "'");
        }
        rule.deleteNodes.push_back(*index);
    }
    for (const std::string& text : retypes) {
        const auto colon = text.find(':');
        const auto index = colon == std::string::npos ? std::nullopt : parseIndex(std::string_view(text).substr(0, colon));
        if (!index) {
            return bad("bad retype '" + text + "'");
        }
        rule.retypes.push_back({*index, text.substr(colon + 1)});
    }
    if (rule.matchNodes.size() > kMaxMatchNodes || rule.addNodes.size() > kMaxAddNodes) {
        return bad("a rule may match at most 4 nodes and add at most 8");
    }
    // Every matched node past the first must hang off a matched edge, so the
    // number of matches stays bounded by the graph's degree, not its size to the power of the pattern.
    for (std::size_t node = 1; node < rule.matchNodes.size(); ++node) {
        const bool linked = std::any_of(rule.matchEdges.begin(), rule.matchEdges.end(), [&](const EdgePattern& e) {
            return e.from == static_cast<int>(node) || e.to == static_cast<int>(node);
        });
        if (!linked) {
            return bad("every matched node after the first needs a match edge");
        }
    }
    const std::string problem = graph::validate(rule);
    return problem.empty() ? problem : bad(problem);
}

RuleSet* phaseRules(Ruleset& rules, const std::string& phase)
{
    if (phase == kPhaseCore) {
        return &rules.core;
    }
    if (phase == kPhaseLock) {
        return &rules.lock;
    }
    if (phase == kPhaseGrow) {
        return &rules.grow;
    }
    if (phase == kPhaseReveal) {
        return &rules.reveal;
    }
    if (phase == kPhaseResolve) {
        return &rules.resolve;
    }
    return nullptr;
}

Ruleset failed(std::string message)
{
    Ruleset rules;
    rules.error = std::move(message);
    return rules;
}

std::string runPhase(Graph& graph, const RuleSet& rules, std::uint64_t seed, const char* phase, std::uint32_t steps, bool toFixedPoint)
{
    RewriteOptions options;
    options.seed = Rng(seed).derive(phase).nextU64();
    options.maxIterations = steps;
    const RewriteResult result = rewrite(graph, rules, options);
    if (result.reason == StopReason::InvalidRule) {
        return std::string(phase) + ": " + result.error;
    }
    if (toFixedPoint ? result.reason != StopReason::FixedPoint : result.iterations != steps) {
        return std::string(phase) + " phase did not run to completion";
    }
    return {};
}

bool isKnownType(const std::string& type)
{
    static const std::set<std::string> known = {kEntry, kClimax, kChallenge, kJunction, kGate, kKey, kReward, kReveal, kRest};
    return known.contains(type);
}

using Adjacency = std::map<NodeId, std::vector<const Edge*>>;

Adjacency outgoing(const Graph& graph)
{
    Adjacency adjacency;
    for (const auto& [id, edge] : graph.edges()) {
        adjacency[edge.from].push_back(&edge);
    }
    return adjacency;
}

// Nodes reachable from the entry along path and shortcut edges. A gate can
// only be entered once it is open; with `collectKeys` every key reached opens
// its gates, repeated until nothing changes.
std::set<NodeId> reach(const Graph& graph, const Adjacency& adjacency, NodeId entry, bool collectKeys)
{
    std::set<NodeId> open;
    std::set<NodeId> seen;
    for (bool changed = true; changed;) {
        changed = false;
        seen.clear();
        std::deque<NodeId> queue{entry};
        seen.insert(entry);
        while (!queue.empty()) {
            const NodeId at = queue.front();
            queue.pop_front();
            const auto out = adjacency.find(at);
            if (out == adjacency.end()) {
                continue;
            }
            const bool isKey = collectKeys && graph.node(at)->type == kKey;
            for (const Edge* edge : out->second) {
                if (isKey && edge->type == kUnlocks && open.insert(edge->to).second) {
                    changed = true;
                }
                if ((edge->type != kPath && edge->type != kShortcut) || seen.contains(edge->to)) {
                    continue;
                }
                if (graph.node(edge->to)->type == kGate && !open.contains(edge->to)) {
                    continue;
                }
                seen.insert(edge->to);
                queue.push_back(edge->to);
            }
        }
    }
    return seen;
}

std::map<NodeId, std::uint32_t> pathDepth(const Adjacency& adjacency, NodeId entry)
{
    std::map<NodeId, std::uint32_t> depth{{entry, 0}};
    std::deque<NodeId> queue{entry};
    while (!queue.empty()) {
        const NodeId at = queue.front();
        queue.pop_front();
        const auto out = adjacency.find(at);
        if (out == adjacency.end()) {
            continue;
        }
        for (const Edge* edge : out->second) {
            if (edge->type == kPath && !depth.contains(edge->to)) {
                depth[edge->to] = depth[at] + 1;
                queue.push_back(edge->to);
            }
        }
    }
    return depth;
}

bool pathReaches(const Adjacency& adjacency, NodeId from, NodeId target)
{
    return pathDepth(adjacency, from).contains(target);
}

}

Ruleset parseRuleset(std::string_view json)
{
    const Json document = Json::parse(json.begin(), json.end(), nullptr, false);
    if (document.is_discarded() || !document.is_array()) {
        return failed("mission rules must be a JSON array of records");
    }
    if (document.size() > kMaxRules) {
        return failed("too many mission rules");
    }
    Ruleset rules;
    std::set<std::string> ids;
    for (const Json& record : document) {
        std::string phase;
        Rule rule;
        if (const std::string problem = parseRule(record, phase, rule); !problem.empty()) {
            return failed(problem);
        }
        if (!ids.insert(rule.name).second) {
            return failed("duplicate rule id '" + rule.name + "'");
        }
        RuleSet* target = phaseRules(rules, phase);
        if (target == nullptr) {
            return failed(rule.name + ": unknown phase '" + phase + "'");
        }
        target->rules.push_back(std::move(rule));
    }
    for (const RuleSet* phase : {&rules.core, &rules.lock, &rules.grow, &rules.reveal, &rules.resolve}) {
        if (phase->rules.empty()) {
            return failed("every phase needs at least one rule");
        }
    }
    return rules;
}

Ruleset loadRuleset(const std::filesystem::path& file)
{
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(file, bytes, kMaxFileBytes)) {
        return failed("cannot read " + file.string());
    }
    return parseRuleset(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

Result generate(const Ruleset& rules, const Params& params)
{
    Result result;
    if (!rules.error.empty()) {
        result.error = rules.error;
        return result;
    }
    Graph& graph = result.graph;
    const NodeId entry = graph.addNode(kEntry);
    const NodeId first = graph.addNode(kSegment);
    const NodeId climax = graph.addNode(kClimax);
    graph.addEdge(entry, first, kPath);
    graph.addEdge(first, climax, kPath);

    const std::uint32_t locks = std::min(params.locks, kMaxLocks);
    const std::uint32_t growSteps = std::min(params.growSteps, kMaxGrowSteps);
    std::string problem = runPhase(graph, rules.core, params.seed, kPhaseCore, 1, false);
    if (problem.empty() && locks > 0) {
        problem = runPhase(graph, rules.lock, params.seed, kPhaseLock, locks, false);
    }
    if (problem.empty() && growSteps > 0) {
        problem = runPhase(graph, rules.grow, params.seed, kPhaseGrow, growSteps, false);
    }
    if (problem.empty()) {
        problem = runPhase(graph, rules.reveal, params.seed, kPhaseReveal, 1, false);
    }
    if (problem.empty()) {
        // A resolve rule must consume a placeholder, so one step per node is always enough.
        const auto budget = static_cast<std::uint32_t>(graph.nodes().size()) + 1;
        problem = runPhase(graph, rules.resolve, params.seed, kPhaseResolve, budget, true);
    }
    result.error = std::move(problem);
    return result;
}

std::vector<std::string> validateMission(const Graph& graph)
{
    std::vector<std::string> errors;
    NodeId entry = 0;
    std::size_t entries = 0;
    std::size_t climaxes = 0;
    for (const auto& [id, node] : graph.nodes()) {
        entries += node.type == kEntry ? 1 : 0;
        climaxes += node.type == kClimax ? 1 : 0;
        if (node.type == kEntry) {
            entry = id;
        }
        if (!isKnownType(node.type)) {
            errors.push_back("node " + std::to_string(id) + " has unresolved or unknown type '" + node.type + "'");
        }
    }
    if (entries != 1 || climaxes != 1) {
        errors.push_back("a mission needs exactly one entry and one climax");
    }
    if (!errors.empty()) {
        return errors;
    }

    for (const auto& [id, node] : graph.nodes()) {
        if (node.type != kGate) {
            continue;
        }
        const bool keyed = std::any_of(graph.edges().begin(), graph.edges().end(), [&](const auto& pair) {
            const Edge& e = pair.second;
            return e.type == kUnlocks && e.to == id && graph.node(e.from)->type == kKey;
        });
        if (!keyed) {
            errors.push_back("gate " + std::to_string(id) + " has no key");
        }
    }

    const Adjacency adjacency = outgoing(graph);
    const std::set<NodeId> open = reach(graph, adjacency, entry, true);
    for (const auto& [id, node] : graph.nodes()) {
        if (!open.contains(id)) {
            errors.push_back("node " + std::to_string(id) + " (" + node.type + ") is unreachable even with every key");
        }
    }
    const std::set<NodeId> shut = reach(graph, adjacency, entry, false);
    for (const auto& [id, node] : graph.nodes()) {
        if ((node.type == kReward || node.type == kClimax) && shut.contains(id)) {
            errors.push_back(node.type + " " + std::to_string(id) + " is reachable without passing a gate");
        }
    }

    const auto depth = pathDepth(adjacency, entry);
    for (const auto& [id, edge] : graph.edges()) {
        if (edge.type != kShortcut) {
            continue;
        }
        const auto from = depth.find(edge.from);
        const auto to = depth.find(edge.to);
        if (from == depth.end() || to == depth.end() || to->second >= from->second || !pathReaches(adjacency, edge.to, edge.from)) {
            errors.push_back("shortcut " + std::to_string(id) + " does not lead back to an ancestor");
        }
    }
    return errors;
}

}
