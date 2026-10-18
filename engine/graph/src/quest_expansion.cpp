#include "ichor/graph/quest_expansion.h"

#include "ichor/core/file_io.h"
#include "ichor/core/rng.h"
#include "ichor/graph/mission.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <iterator>
#include <set>

namespace ichor::graph::quest {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxRules = 256;
constexpr std::uintmax_t kMaxFileBytes = 1u << 20;
constexpr std::uint32_t kMaxExpansionSteps = 512;
constexpr int kMaxJsonDepth = 32;

Ruleset failed(std::string message)
{
    Ruleset rules;
    rules.error = std::move(message);
    return rules;
}

RuleSet* phaseRules(Ruleset& rules, const std::string& phase)
{
    if (phase == kPhaseTension) {
        return &rules.tension;
    }
    if (phase == kPhaseStructure) {
        return &rules.structure;
    }
    if (phase == kPhaseDetail) {
        return &rules.detail;
    }
    return nullptr;
}

bool isTension(std::string_view kind)
{
    return std::any_of(std::begin(kTensionKinds), std::end(kTensionKinds), [&](const char* k) { return kind == k; });
}

const std::set<std::string, std::less<>>& concreteTypes()
{
    static const std::set<std::string, std::less<>> types = {
        kSituation, kWalkAway, "trade", "deliver", "scout", "fetch", "parley", "persuade", "sabotage", "bribe", "warn",
        "escort", "defend", "witness", "confront", "evacuate", "investigate", "heist",
    };
    return types;
}

std::string runPhase(Graph& graph, const RuleSet& rules, std::uint64_t seed, const char* phase)
{
    RewriteOptions options;
    options.seed = Rng(seed).derive(phase).nextU64();
    options.maxIterations = kMaxExpansionSteps;
    const RewriteResult result = rewrite(graph, rules, options);
    if (result.reason == StopReason::InvalidRule) {
        return std::string(phase) + ": " + result.error;
    }
    if (result.reason != StopReason::FixedPoint) {
        return std::string(phase) + " phase did not settle";
    }
    return {};
}

// Records are re-serialised below, and the serialiser recurses, so a hostile nesting depth is refused up front.
Json parseShallow(std::string_view json)
{
    bool tooDeep = false;
    Json document = Json::parse(json.begin(), json.end(), [&tooDeep](int depth, Json::parse_event_t, Json&) {
        if (depth > kMaxJsonDepth) tooDeep = true;
        return !tooDeep;
    }, false);
    return tooDeep ? Json(Json::value_t::discarded) : document;
}

}

Ruleset parseRuleset(std::string_view json)
{
    const Json document = parseShallow(json);
    if (document.is_discarded() || !document.is_array()) {
        return failed("quest rules must be a JSON array of records");
    }
    if (document.size() > kMaxRules) {
        return failed("too many quest rules");
    }
    Ruleset rules;
    std::set<std::string> ids;
    for (const Json& record : document) {
        std::string phase;
        Rule rule;
        if (const std::string problem = mission::parseRuleRecord(record.dump(), phase, rule); !problem.empty()) {
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
    for (const RuleSet* phase : {&rules.tension, &rules.structure, &rules.detail}) {
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

bool isConcrete(std::string_view type)
{
    return concreteTypes().contains(type);
}

Result expand(const Ruleset& rules, const Params& params)
{
    Result result;
    if (!rules.error.empty()) {
        result.error = rules.error;
        return result;
    }
    if (!isTension(params.tension)) {
        result.error = "unknown tension '" + params.tension + "'";
        return result;
    }
    result.graph.addNode(params.tension);
    std::string problem = runPhase(result.graph, rules.tension, params.seed, kPhaseTension);
    if (problem.empty()) {
        problem = runPhase(result.graph, rules.structure, params.seed, kPhaseStructure);
    }
    if (problem.empty()) {
        problem = runPhase(result.graph, rules.detail, params.seed, kPhaseDetail);
    }
    result.error = std::move(problem);
    return result;
}

std::vector<std::string> validateQuest(const Graph& graph)
{
    std::vector<std::string> errors;
    NodeId root = 0;
    std::size_t roots = 0;
    for (const auto& [id, node] : graph.nodes()) {
        if (node.type == kSituation) {
            ++roots;
            root = id;
        } else if (!isConcrete(node.type)) {
            errors.push_back("placeholder '" + node.type + "' left in the graph");
        }
    }
    if (roots != 1) {
        errors.push_back("expected one situation, found " + std::to_string(roots));
        return errors;
    }

    std::set<NodeId> seen{root};
    std::deque<NodeId> queue{root};
    bool refusable = false;
    bool hasRequirement = false;
    while (!queue.empty()) {
        const NodeId at = queue.front();
        queue.pop_front();
        for (const auto& [id, edge] : graph.edges()) {
            if (edge.from != at) {
                continue;
            }
            if (at == root && edge.type == kNeeds) {
                hasRequirement = true;
            }
            const Node* to = graph.node(edge.to);
            if (at == root && edge.type == kRefuse && to != nullptr && to->type == kWalkAway) {
                refusable = true;
            }
            if (seen.insert(edge.to).second) {
                queue.push_back(edge.to);
            }
        }
    }
    if (seen.size() != graph.nodes().size()) {
        errors.push_back("some nodes are unreachable from the situation");
    }
    if (!hasRequirement) {
        errors.push_back("the situation has no requirement");
    }
    if (!refusable) {
        errors.push_back("the player has no way to refuse");
    }
    return errors;
}

}
