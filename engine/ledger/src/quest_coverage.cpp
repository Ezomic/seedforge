#include "ichor/ledger/quest_coverage.h"

#include "ichor/core/rng.h"
#include "ichor/graph/rewrite.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace ichor::questcoverage {

namespace {

using graph::Graph;
using graph::NodeId;

struct Phase {
    const char* name;
    const graph::RuleSet graph::quest::Ruleset::*rules;
};

constexpr Phase kPhases[] = {
    {graph::quest::kPhaseTension, &graph::quest::Ruleset::tension},
    {graph::quest::kPhaseStructure, &graph::quest::Ruleset::structure},
    {graph::quest::kPhaseDetail, &graph::quest::Ruleset::detail},
};

struct EffectRule {
    std::string_view beat;
    VisibleMetric metric;
    std::int64_t delta;
    std::string_view target;
};

constexpr EffectRule kEffectRules[] = {
    {"trade", VisibleMetric::SupplyStatus, 2, "settlement"},
    {"deliver", VisibleMetric::SupplyStatus, 1, "settlement"},
    {"heist", VisibleMetric::SupplyStatus, -1, "settlement"},
    {"escort", VisibleMetric::Route, 1, "route"},
    {"evacuate", VisibleMetric::Route, -1, "route"},
    {"defend", VisibleMetric::Territory, 1, "territory"},
    {"confront", VisibleMetric::FactionStrength, -1, "faction"},
    {"sabotage", VisibleMetric::FactionStrength, -1, "faction"},
    {"persuade", VisibleMetric::FactionStrength, 1, "faction"},
};

struct TensionEffect {
    std::string_view tension;
    VisibleMetric metric;
    std::int64_t delta;
    std::string_view target;
};

constexpr TensionEffect kTensionEffects[] = {
    {"contested_cell", VisibleMetric::Territory, 8, "territory"},
    {"broken_dependency", VisibleMetric::SupplyStatus, 8, "settlement"},
    {"hostility_pair", VisibleMetric::FactionStrength, 8, "faction"},
    {"pressure_front", VisibleMetric::Territory, -8, "territory"},
    {"custodian_secret", VisibleMetric::FactionStrength, 8, "faction"},
    {"player_consequence", VisibleMetric::Route, 8, "route"},
};

constexpr std::string_view kPartyBeats[] = {"parley", "persuade", "bribe", "warn", "sabotage", "heist", "confront", "trade"};

bool facesParty(std::string_view beat)
{
    return std::find(std::begin(kPartyBeats), std::end(kPartyBeats), beat) != std::end(kPartyBeats);
}

bool sameGraph(const Graph& a, const Graph& b)
{
    if (a.nodes().size() != b.nodes().size() || a.edges().size() != b.edges().size()) {
        return false;
    }
    auto an = a.nodes().begin();
    for (auto bn = b.nodes().begin(); bn != b.nodes().end(); ++an, ++bn) {
        if (an->first != bn->first || an->second.type != bn->second.type) {
            return false;
        }
    }
    auto ae = a.edges().begin();
    for (auto be = b.edges().begin(); be != b.edges().end(); ++ae, ++be) {
        const graph::Edge& x = ae->second;
        const graph::Edge& y = be->second;
        if (ae->first != be->first || x.from != y.from || x.to != y.to || x.type != y.type) {
            return false;
        }
    }
    return true;
}

// Mirrors graph::quest::expand phase by phase so the rule applied at every
// iteration is visible; the caller compares the result with expand() itself.
bool replay(const graph::quest::Ruleset& rules, std::uint64_t seed, const std::string& tension, Graph& g,
            std::vector<std::vector<std::uint32_t>>& applied)
{
    g.addNode(tension);
    applied.clear();
    for (const Phase& phase : kPhases) {
        graph::RewriteOptions options;
        options.seed = Rng(seed).derive(phase.name).nextU64();
        options.maxIterations = 512;
        const graph::RewriteResult result = graph::rewrite(g, rules.*phase.rules, options);
        if (result.reason != graph::StopReason::FixedPoint) {
            return false;
        }
        applied.push_back(result.applied);
    }
    return true;
}

void note(CoverageReport& report, std::string message)
{
    if (report.failureSamples.size() < kMaxFailureSamples) {
        report.failureSamples.push_back(std::move(message));
    }
}

std::string describe(std::uint64_t seed, const std::string& tension, const std::string& what)
{
    return "seed " + std::to_string(seed) + " " + tension + ": " + what;
}

// Per-mille as "x.y" using integers only.
std::string permille(std::uint64_t part, std::uint64_t whole)
{
    const std::uint64_t value = whole == 0 ? 0 : part * 1000 / whole;
    return std::to_string(value / 10) + "." + std::to_string(value % 10);
}

std::string pad(std::string text, std::size_t width, bool right = false)
{
    if (text.size() >= width) {
        return text;
    }
    const std::string fill(width - text.size(), ' ');
    return right ? fill + text : text + fill;
}

} // namespace

std::vector<QuestEffect> effectsOf(const Graph& g, const std::string& tension)
{
    std::vector<QuestEffect> effects;
    for (const TensionEffect& rule : kTensionEffects) {
        if (tension == rule.tension) {
            effects.push_back({rule.metric, std::string(rule.target), rule.delta});
        }
    }
    for (const auto& [id, node] : g.nodes()) {
        for (const EffectRule& rule : kEffectRules) {
            if (node.type == rule.beat) {
                effects.push_back({rule.metric, std::string(rule.target), rule.delta});
            }
        }
    }
    return effects;
}

graph::questrefusal::Quest refusalViewOf(const Graph& g)
{
    using namespace graph::questrefusal;
    Quest quest;
    NodeId root = 0;
    for (const auto& [id, node] : g.nodes()) {
        if (node.type == graph::quest::kSituation) {
            root = id;
            break;
        }
    }
    if (root == 0) {
        return quest;
    }
    std::vector<NodeId> order;
    std::set<NodeId> seen{root};
    std::deque<NodeId> queue{root};
    while (!queue.empty()) {
        const NodeId at = queue.front();
        queue.pop_front();
        order.push_back(at);
        for (const auto& [edgeId, edge] : g.edges()) {
            const graph::Node* to = g.node(edge.to);
            if (edge.from == at && to != nullptr && to->type != graph::quest::kWalkAway && seen.insert(edge.to).second) {
                queue.push_back(edge.to);
            }
        }
    }
    quest.start = root;
    for (std::size_t i = 0; i < order.size(); ++i) {
        Step step;
        step.id = order[i];
        if (facesParty(g.node(order[i])->type)) {
            step.otherSide = root;
        }
        step.options.push_back({Choice::Accept, i + 1 < order.size() ? order[i + 1] : 0, ""});
        quest.steps.push_back(std::move(step));
    }
    return quest;
}

CoverageReport run(const graph::quest::Ruleset& rules, const Options& options)
{
    CoverageReport report;
    report.seeds = std::clamp<std::uint32_t>(options.seeds, 1, kMaxSeeds);
    if (!rules.error.empty()) {
        report.expandFailures = 1;
        note(report, "ruleset: " + rules.error);
        return report;
    }
    for (const Phase& phase : kPhases) {
        for (const graph::Rule& rule : (rules.*phase.rules).rules) {
            report.rules.push_back({rule.name, phase.name, 0, 0});
        }
    }
    std::size_t base[std::size(kPhases)] = {};
    for (std::size_t p = 1; p < std::size(kPhases); ++p) {
        base[p] = base[p - 1] + (rules.*kPhases[p - 1].rules).rules.size();
    }

    for (std::uint32_t n = 0; n < report.seeds; ++n) {
        const std::uint64_t seed = options.baseSeed + n;
        for (const char* kind : graph::quest::kTensionKinds) {
            const std::string tension = kind;
            ++report.quests;

            graph::quest::Params params;
            params.seed = seed;
            params.tension = tension;
            const graph::quest::Result expanded = graph::quest::expand(rules, params);
            if (!expanded.error.empty()) {
                ++report.expandFailures;
                note(report, describe(seed, tension, "expand failed: " + expanded.error));
                continue;
            }

            Graph replayed;
            std::vector<std::vector<std::uint32_t>> applied;
            if (!replay(rules, seed, tension, replayed, applied) || !sameGraph(replayed, expanded.graph)) {
                ++report.replayMismatches;
                note(report, describe(seed, tension, "per-rule replay differs from expand"));
            } else {
                std::set<std::size_t> touched;
                for (std::size_t p = 0; p < applied.size(); ++p) {
                    for (const std::uint32_t index : applied[p]) {
                        const std::size_t slot = base[p] + index;
                        ++report.rules[slot].fires;
                        touched.insert(slot);
                    }
                }
                for (const std::size_t slot : touched) {
                    ++report.rules[slot].quests;
                }
            }

            if (const auto problems = graph::quest::validateQuest(expanded.graph); !problems.empty()) {
                ++report.structureFailures;
                note(report, describe(seed, tension, "structure: " + problems.front()));
            }

            const graph::questrefusal::Result refusal = graph::questrefusal::ensureRefusals(refusalViewOf(expanded.graph));
            if (!refusal.findings.empty()) {
                ++report.refusalFailures;
                if (refusal.findings.front().code == graph::questrefusal::Code::NoOtherSide) {
                    ++report.refusalNoOtherSide;
                }
                note(report, describe(seed, tension, "refusal: " + refusal.findings.front().message));
            }

            switch (checkVisibleNumber(effectsOf(expanded.graph, tension)).verdict) {
            case VisibleNumberVerdict::Moves:
                ++report.visibleMoves;
                break;
            case VisibleNumberVerdict::AllCancel:
                ++report.visibleAllCancel;
                note(report, describe(seed, tension, "visible number: every effect cancels"));
                break;
            default:
                ++report.visibleNoEffects;
                note(report, describe(seed, tension, "visible number: quest moves nothing"));
                break;
            }
        }
    }

    if (report.replayMismatches == 0 && report.expandFailures == 0) {
        for (const RuleCoverage& rule : report.rules) {
            if (rule.fires == 0) {
                report.deadRules.push_back(rule.id);
            }
        }
    }
    return report;
}

std::string formatTable(const CoverageReport& report)
{
    std::size_t idWidth = 4;
    for (const RuleCoverage& rule : report.rules) {
        idWidth = std::max(idWidth, rule.id.size());
    }
    std::string out;
    out += pad("rule", idWidth) + "  " + pad("phase", 9) + pad("fires", 8, true) + pad("quests", 8, true) + pad("share%", 8, true) + "\n";
    for (const RuleCoverage& rule : report.rules) {
        out += pad(rule.id, idWidth) + "  " + pad(rule.phase, 9) + pad(std::to_string(rule.fires), 8, true) +
               pad(std::to_string(rule.quests), 8, true) + pad(permille(rule.quests, report.quests), 8, true) +
               (rule.fires == 0 ? "  DEAD" : "") + "\n";
    }
    out += "seeds " + std::to_string(report.seeds) + ", quests " + std::to_string(report.quests) + ", dead rules " +
           std::to_string(report.deadRules.size()) + "\n";
    out += "expand failures " + std::to_string(report.expandFailures) + ", replay mismatches " + std::to_string(report.replayMismatches) +
           ", structure failures " + std::to_string(report.structureFailures) + ", refusal failures " +
           std::to_string(report.refusalFailures) + "\n";
    out += "visible number: moves " + std::to_string(report.visibleMoves) + ", no effects " + std::to_string(report.visibleNoEffects) +
           ", all cancel " + std::to_string(report.visibleAllCancel) + "\n";
    for (const std::string& sample : report.failureSamples) {
        out += "  " + sample + "\n";
    }
    return out;
}

} // namespace ichor::questcoverage
