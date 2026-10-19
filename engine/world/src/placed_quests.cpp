#include "ichor/world/placed_quests.h"

#include "ichor/core/rng.h"
#include "ichor/graph/quest_binding.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>

namespace ichor {

struct PlacedQuestBuilder {
    static void add(PlacedQuestSet& set, QuestInstance quest) { set.quests_.push_back(std::move(quest)); }
    static void refuse(PlacedQuestSet& set, RefusedQuest refused) { set.refused_.push_back(std::move(refused)); }
};

namespace {

using graph::questbind::EntityKind;

constexpr std::size_t kMaxSites = 1500;
constexpr std::size_t kMaxFactions = 500;
constexpr std::size_t kMaxLocalNpcs = 1500; // sites + factions + local NPCs + the road stay under the binder's cap

constexpr std::string_view kSite = "poi:";
constexpr std::string_view kNpc = "npc:";
constexpr std::string_view kFaction = "faction:";
constexpr std::string_view kRoad = "road:";

PlacedQuests failure(std::string message)
{
    PlacedQuests out;
    out.error = std::move(message);
    return out;
}

QuestEntityKind kindOf(EntityKind kind)
{
    switch (kind) {
    case EntityKind::Faction: return QuestEntityKind::Faction;
    case EntityKind::Npc: return QuestEntityKind::Npc;
    case EntityKind::Site: return QuestEntityKind::Site;
    case EntityKind::Road: return QuestEntityKind::Road;
    }
    return QuestEntityKind::Site;
}

std::string_view prefixOf(EntityKind kind)
{
    switch (kind) {
    case EntityKind::Faction: return kFaction;
    case EntityKind::Npc: return kNpc;
    case EntityKind::Site: return kSite;
    case EntityKind::Road: return kRoad;
    }
    return kSite;
}

// Entity ids carry a kind prefix so a POI, an NPC and an archetype that share a name cannot collide;
// the road id uses indices, since two 64 character ids would not fit the binder's id cap.
graph::questbind::World bindWorld(const worldforge::ContentManifest& m, const worldforge::QuestTension& t, std::string& roadId)
{
    graph::questbind::World world;
    const auto indexOf = [&m](const std::string& id) { return std::lower_bound(m.poiIds.begin(), m.poiIds.end(), id) - m.poiIds.begin(); };
    roadId = std::string(kRoad) + std::to_string(indexOf(t.poiA)) + ">" + std::to_string(indexOf(t.poiB));
    world.entities.push_back({roadId, EntityKind::Road, "", 0});

    std::size_t sites = 0;
    for (const std::string& id : m.poiIds) {
        const bool local = id == t.poiA || id == t.poiB;
        if (!local && sites >= kMaxSites) continue;
        ++sites;
        world.entities.push_back({std::string(kSite) + id, EntityKind::Site, "", id == t.poiA ? 2u : id == t.poiB ? 1u : 0u});
    }

    std::set<std::string> localArchetypes;
    std::vector<const worldforge::CastMember*> others;
    std::size_t localNpcs = 0;
    for (const worldforge::CastMember& c : m.cast) {
        if (c.homePoiId == t.poiA || c.homePoiId == t.poiB) {
            if (localNpcs >= kMaxLocalNpcs) continue;
            ++localNpcs;
            localArchetypes.insert(c.archetype);
            world.entities.push_back({std::string(kNpc) + c.npcId, EntityKind::Npc, "", 1});
        } else {
            others.push_back(&c);
        }
    }

    std::set<std::string> archetypes;
    for (const worldforge::CastMember& c : m.cast) archetypes.insert(c.archetype);
    std::size_t factions = 0;
    for (const std::string& a : archetypes) {
        const bool local = localArchetypes.contains(a);
        if (!local && factions >= kMaxFactions) continue;
        ++factions;
        world.entities.push_back({std::string(kFaction) + a, EntityKind::Faction, "", local ? 1u : 0u});
    }

    for (const worldforge::CastMember* c : others) {
        if (world.entities.size() >= graph::questbind::kMaxEntities) break;
        world.entities.push_back({std::string(kNpc) + c->npcId, EntityKind::Npc, "", 0});
    }
    return world;
}

std::string plainId(const std::string& internal, EntityKind kind, const worldforge::QuestTension& t)
{
    if (kind == EntityKind::Road) return t.poiA + ">" + t.poiB;
    return internal.substr(prefixOf(kind).size());
}

// Story order: breadth first from the situation along needs and then edges, ties by node id; any
// node the walk misses follows in id order.
std::vector<std::uint32_t> storyOrder(const graph::Graph& graph)
{
    std::vector<std::uint32_t> order;
    std::set<std::uint32_t> seen;
    const auto visit = [&](std::uint32_t root) {
        if (!seen.insert(root).second) return;
        std::vector<std::uint32_t> queue{root};
        for (std::size_t head = 0; head < queue.size(); ++head) {
            order.push_back(queue[head]);
            std::set<std::uint32_t> next;
            for (const auto& [eid, edge] : graph.edges()) {
                if (edge.from == queue[head] && edge.type != graph::quest::kRefuse && !seen.contains(edge.to)) next.insert(edge.to);
            }
            for (const std::uint32_t id : next) {
                seen.insert(id);
                queue.push_back(id);
            }
        }
    };
    for (const auto& [id, node] : graph.nodes()) {
        if (node.type == graph::quest::kSituation) visit(id);
    }
    for (const auto& [id, node] : graph.nodes()) visit(id);
    return order;
}

// Steps in story order, each accepting into the next; the other side is the opposite settlement.
graph::questrefusal::Quest stepsOf(const graph::Graph& graph, std::uint32_t otherSide)
{
    graph::questrefusal::Quest quest;
    std::vector<std::uint32_t> ids;
    for (const std::uint32_t id : storyOrder(graph)) {
        if (graph.node(id)->type != graph::quest::kWalkAway) ids.push_back(id);
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        graph::questrefusal::Step step;
        step.id = ids[i];
        step.otherSide = otherSide;
        step.options.push_back({graph::questrefusal::Choice::Accept, i + 1 < ids.size() ? ids[i + 1] : 0u, ""});
        quest.steps.push_back(std::move(step));
    }
    quest.start = ids.empty() ? 0 : ids.front();
    return quest;
}

std::string describe(const std::vector<graph::questrefusal::Finding>& findings)
{
    return findings.empty() ? std::string("refusal paths could not be repaired") : "refusal paths: " + findings.front().message;
}

}

const QuestInstance* PlacedQuestSet::find(const std::string& questId) const
{
    const auto it = std::lower_bound(quests_.begin(), quests_.end(), questId, [](const QuestInstance& q, const std::string& v) { return q.id < v; });
    return it != quests_.end() && it->id == questId ? &*it : nullptr;
}

std::vector<const QuestInstance*> PlacedQuestSet::touching(const std::string& poiId) const
{
    std::vector<const QuestInstance*> out;
    for (const QuestInstance& q : quests_) {
        const bool uses = q.poiA == poiId || q.poiB == poiId
            || std::any_of(q.beats.begin(), q.beats.end(), [&](const QuestBeat& b) { return b.kind == QuestEntityKind::Site && b.entity == poiId; });
        if (uses) out.push_back(&q);
    }
    return out;
}

PlacedQuests placeManifestQuests(const worldforge::ContentManifest& manifest, const graph::quest::Ruleset& rules)
{
    if (!rules.error.empty()) return failure("the quest rules did not load: " + rules.error);
    if (manifest.poiIds.size() > worldforge::kManifestMaxPois || manifest.cast.size() > worldforge::kManifestMaxEntries
        || manifest.tensions.size() > worldforge::kManifestMaxEntries || manifest.items.size() > worldforge::kManifestMaxEntries
        || manifest.dungeons.size() > worldforge::kManifestMaxPois) {
        return failure("the manifest is over its size caps");
    }
    const std::optional<worldforge::ContentManifest> checked = worldforge::decodeManifest(worldforge::encodeManifest(manifest));
    if (!checked.has_value() || !(*checked == manifest)) return failure("the manifest is malformed");

    const Rng rng(manifest.worldSeed);
    const auto indexOf = [&manifest](const std::string& id) {
        return static_cast<std::uint32_t>(std::lower_bound(manifest.poiIds.begin(), manifest.poiIds.end(), id) - manifest.poiIds.begin()) + 1u;
    };

    PlacedQuests out;
    for (std::size_t i = 0; i < manifest.tensions.size(); ++i) {
        const worldforge::QuestTension& t = manifest.tensions[i];
        const auto refuse = [&](std::string reason) { PlacedQuestBuilder::refuse(out.set, {i, t.poiA, t.poiB, std::move(reason)}); };

        const std::string key = t.poiA + "|" + t.poiB;
        const std::uint64_t kindDraw = rng.derive("quest/kind/" + key).nextU64();
        graph::quest::Params params;
        params.seed = rng.derive("quest/seed/" + key).nextU64();
        params.tension = graph::quest::kTensionKinds[kindDraw % std::size(graph::quest::kTensionKinds)];

        graph::quest::Result expanded = graph::quest::expand(rules, params);
        if (!expanded.error.empty()) {
            refuse("expansion failed: " + expanded.error);
            continue;
        }
        if (const auto problems = graph::quest::validateQuest(expanded.graph); !problems.empty()) {
            refuse("expanded quest is invalid: " + problems.front());
            continue;
        }

        std::string roadId;
        const graph::questbind::World world = bindWorld(manifest, t, roadId);
        const graph::questbind::Result bound = graph::questbind::bind(expanded.graph, world, params.seed);
        if (!bound.error.empty()) {
            refuse("binding failed: " + bound.error);
            continue;
        }

        QuestInstance quest;
        char number[16];
        std::snprintf(number, sizeof number, "quest_%05zu", i);
        quest.id = number;
        quest.tensionKind = params.tension;
        quest.poiA = t.poiA;
        quest.poiB = t.poiB;
        quest.intensity = t.intensity;
        quest.seed = params.seed;
        quest.graphDigest = expanded.graph.digest();
        for (const graph::questbind::Binding& b : bound.bindings) {
            QuestBeat beat;
            beat.node = b.node;
            beat.type = b.type;
            if (!b.entity.empty()) {
                EntityKind kind{};
                graph::questbind::kindFor(b.type, kind);
                beat.kind = kindOf(kind);
                beat.entity = plainId(b.entity, kind, t);
            }
            quest.beats.push_back(std::move(beat));
        }

        graph::questrefusal::Result repaired = graph::questrefusal::ensureRefusals(stepsOf(expanded.graph, indexOf(t.poiB)));
        if (!repaired.findings.empty()) {
            refuse(describe(repaired.findings));
            continue;
        }
        quest.refusals = std::move(repaired.quest);
        PlacedQuestBuilder::add(out.set, std::move(quest));
    }
    return out;
}

}
