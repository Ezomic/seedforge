#include "ichor/graph/minor_site.h"

#include "ichor/core/rng.h"

#include <array>
#include <map>
#include <set>

namespace ichor::graph::site {

namespace {

constexpr std::array<std::string_view, kArchetypeCount> kArchetypes = {
    "cave", "camp", "cellar", "wreck", "watchtower"
};

constexpr std::uint32_t kMaxTargetSeconds = 520;
constexpr std::uint32_t kMinTargetSeconds = 330;
constexpr std::uint32_t kMaxSideBranches = 2;

struct Cost {
    std::string_view type;
    std::uint32_t seconds;
};
constexpr std::array<Cost, 7> kCosts = { {
    { kEntrance, 20 },
    { kPassage, 30 },
    { kRoom, 45 },
    { kEncounter, 75 },
    { kLandmark, 25 },
    { kStash, 20 },
    { kReward, 40 },
} };

// Eight draws per archetype; the mix is what tells a cave from a camp.
constexpr std::array<std::array<const char*, 8>, kArchetypeCount> kMainMix = { {
    { kPassage, kPassage, kPassage, kRoom, kRoom, kEncounter, kEncounter, kLandmark },
    { kLandmark, kLandmark, kRoom, kRoom, kRoom, kEncounter, kEncounter, kPassage },
    { kRoom, kRoom, kRoom, kRoom, kPassage, kPassage, kEncounter, kLandmark },
    { kEncounter, kEncounter, kEncounter, kRoom, kRoom, kPassage, kLandmark, kLandmark },
    { kPassage, kPassage, kPassage, kPassage, kRoom, kEncounter, kEncounter, kLandmark },
} };

const char* pickMainType(Rng& rng, std::uint32_t archetype)
{
    return kMainMix[archetype][rng.range(0, 7)];
}

}

std::uint32_t nodeSeconds(std::string_view type)
{
    for (const Cost& c : kCosts) {
        if (c.type == type) {
            return c.seconds;
        }
    }
    return 0;
}

std::string_view archetypeName(std::uint32_t index)
{
    return index < kArchetypeCount ? kArchetypes[index] : std::string_view {};
}

std::uint32_t clearSeconds(const Graph& graph)
{
    std::uint64_t total = 0;
    for (const auto& [id, node] : graph.nodes()) {
        total += nodeSeconds(node.type);
    }
    return total > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(total);
}

Result generate(const Params& params)
{
    Result result;
    Rng rng = Rng(params.seed).derive("minor_site");

    std::uint32_t archetype = params.archetype;
    if (archetype >= kArchetypeCount) {
        archetype = rng.range(0, kArchetypeCount - 1);
    }
    result.archetype = std::string(kArchetypes[archetype]);

    const std::uint32_t target = rng.range(kMinTargetSeconds, kMaxTargetSeconds);
    const std::uint32_t rewardSeconds = nodeSeconds(kReward);

    Graph& g = result.graph;
    std::uint32_t total = nodeSeconds(kEntrance);
    std::vector<NodeId> chain { g.addNode(kEntrance) };

    // The loop only runs while total + reward < target <= kMaxTargetSeconds, so
    // adding the costliest node (75s) and the reward stays under kMaxSeconds.
    while (total + rewardSeconds < target) {
        const char* type = pickMainType(rng, archetype);
        const NodeId id = g.addNode(type);
        g.addEdge(chain.back(), id, kPath);
        chain.push_back(id);
        total += nodeSeconds(type);
    }
    const NodeId reward = g.addNode(kReward);
    g.addEdge(chain.back(), reward, kPath);
    chain.push_back(reward);
    total += rewardSeconds;

    const std::uint32_t branches = rng.range(0, kMaxSideBranches);
    for (std::uint32_t i = 0; i < branches && chain.size() > 2; ++i) {
        const char* type = rng.range(0, 1) == 0 ? kStash : kLandmark;
        if (total + nodeSeconds(type) > kMaxSeconds) {
            break;
        }
        const NodeId from = chain[rng.range(1, static_cast<std::uint32_t>(chain.size()) - 2)];
        const NodeId id = g.addNode(type);
        g.addEdge(from, id, kPath);
        total += nodeSeconds(type);
    }

    if (chain.size() >= 5 && rng.range(0, 1) == 0) {
        g.addEdge(reward, chain.front(), kShortcut);
    }

    result.seconds = total;
    return result;
}

std::vector<std::string> validateSite(const Graph& graph)
{
    std::vector<std::string> errors;
    NodeId entrance = 0;
    NodeId reward = 0;
    std::uint32_t entrances = 0;
    std::uint32_t rewards = 0;

    for (const auto& [id, node] : graph.nodes()) {
        if (nodeSeconds(node.type) == 0) {
            errors.push_back("unknown node type '" + node.type + "'");
        } else if (node.type == kEntrance) {
            entrance = id;
            ++entrances;
        } else if (node.type == kReward) {
            reward = id;
            ++rewards;
        }
    }
    if (entrances != 1) {
        errors.push_back("expected one entrance, found " + std::to_string(entrances));
    }
    if (rewards != 1) {
        errors.push_back("expected one reward, found " + std::to_string(rewards));
    }

    std::map<NodeId, std::vector<NodeId>> forward;
    for (const auto& [id, edge] : graph.edges()) {
        if (edge.type != kPath && edge.type != kShortcut) {
            errors.push_back("unknown edge type '" + edge.type + "'");
        } else if (edge.type == kPath) {
            forward[edge.from].push_back(edge.to);
        }
    }

    if (entrances == 1) {
        std::set<NodeId> seen { entrance };
        std::vector<NodeId> stack { entrance };
        while (!stack.empty()) {
            const NodeId at = stack.back();
            stack.pop_back();
            const auto it = forward.find(at);
            if (it == forward.end()) {
                continue;
            }
            for (NodeId next : it->second) {
                if (seen.insert(next).second) {
                    stack.push_back(next);
                }
            }
        }
        if (seen.size() != graph.nodes().size()) {
            errors.push_back("a node is not reachable from the entrance");
        }
        if (rewards == 1 && seen.count(reward) == 0) {
            errors.push_back("the reward is not reachable from the entrance");
        }
    }

    const std::uint32_t seconds = clearSeconds(graph);
    if (seconds < kMinSeconds || seconds > kMaxSeconds) {
        errors.push_back("clear time " + std::to_string(seconds) + "s is outside the five to ten minute window");
    }
    return errors;
}

}
