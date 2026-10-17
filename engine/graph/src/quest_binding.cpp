#include "ichor/graph/quest_binding.h"

#include "ichor/core/rng.h"
#include "ichor/graph/quest_expansion.h"

#include <functional>
#include <set>
#include <string>
#include <utility>

namespace ichor::graph::questbind {

namespace {

struct KindEntry {
    const char* type;
    EntityKind kind;
};

constexpr KindEntry kKinds[] = {
    {"situation", EntityKind::Site},
    {"trade", EntityKind::Faction},
    {"deliver", EntityKind::Road},
    {"scout", EntityKind::Road},
    {"fetch", EntityKind::Site},
    {"parley", EntityKind::Npc},
    {"persuade", EntityKind::Npc},
    {"sabotage", EntityKind::Site},
    {"bribe", EntityKind::Npc},
    {"warn", EntityKind::Npc},
    {"escort", EntityKind::Road},
    {"defend", EntityKind::Site},
    {"witness", EntityKind::Npc},
    {"confront", EntityKind::Faction},
    {"evacuate", EntityKind::Site},
    {"investigate", EntityKind::Site},
    {"heist", EntityKind::Site},
};

Result failed(std::string message)
{
    Result r;
    r.error = std::move(message);
    return r;
}

std::string checkWorld(const World& world)
{
    if (world.entities.size() > kMaxEntities) {
        return "too many entities";
    }
    std::set<std::string_view> ids;
    for (const Entity& e : world.entities) {
        if (e.id.empty() || e.id.size() > kMaxIdLength || e.region.size() > kMaxIdLength) {
            return "entity id is empty or an id or region is too long";
        }
        if (!ids.insert(e.id).second) {
            return "duplicate entity id '" + e.id + "'";
        }
    }
    return {};
}

struct Pick {
    const Entity* entity = nullptr;
    bool sameRegion = false;
    bool unused = false;
    std::uint32_t appearances = 0;
    std::uint64_t draw = 0;
};

// True when `a` is the better candidate. Ties fall to id so the order is total.
bool better(const Pick& a, const Pick& b)
{
    if (a.sameRegion != b.sameRegion) {
        return a.sameRegion;
    }
    if (a.unused != b.unused) {
        return a.unused;
    }
    if (a.appearances != b.appearances) {
        return a.appearances > b.appearances;
    }
    if (a.draw != b.draw) {
        return a.draw < b.draw;
    }
    return a.entity->id < b.entity->id;
}

}

bool kindFor(std::string_view nodeType, EntityKind& kind)
{
    for (const KindEntry& entry : kKinds) {
        if (nodeType == entry.type) {
            kind = entry.kind;
            return true;
        }
    }
    return false;
}

Result bind(const Graph& graph, const World& world, std::uint64_t seed)
{
    if (graph.nodes().empty() || graph.nodes().size() > kMaxNodes) {
        return failed("nothing to bind or too many nodes");
    }
    if (std::string problem = checkWorld(world); !problem.empty()) {
        return failed(std::move(problem));
    }

    Result result;
    std::set<std::string, std::less<>> used;
    std::string anchorRegion;
    bool anchored = false;
    const Rng rng(seed);

    for (const auto& [id, node] : graph.nodes()) {
        Binding binding;
        binding.node = id;
        binding.type = node.type;
        EntityKind kind{};
        if (!kindFor(node.type, kind)) {
            if (node.type != quest::kWalkAway) {
                return failed("node " + std::to_string(id) + " has unbindable type '" + node.type + "'");
            }
            result.bindings.push_back(std::move(binding));
            continue;
        }
        Pick best;
        for (const Entity& e : world.entities) {
            if (e.kind != kind) {
                continue;
            }
            Pick p;
            p.entity = &e;
            p.sameRegion = anchored && e.region == anchorRegion;
            p.unused = !used.contains(e.id);
            p.appearances = e.appearances;
            p.draw = rng.derive("bind/" + std::to_string(id) + "/" + e.id).nextU64();
            if (best.entity == nullptr || better(p, best)) {
                best = p;
            }
        }
        if (best.entity == nullptr) {
            return failed("no entity of the kind needed by node " + std::to_string(id) + " ('" + node.type + "')");
        }
        if (!anchored) {
            anchored = true;
            anchorRegion = best.entity->region;
        }
        used.insert(best.entity->id);
        binding.entity = best.entity->id;
        result.bindings.push_back(std::move(binding));
    }
    return result;
}

}
