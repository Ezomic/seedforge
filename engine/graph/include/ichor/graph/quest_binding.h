#pragma once

#include "ichor/graph/graph.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::graph::questbind {

// Pass 3 of Tier 1 quest generation (doc 04): resolve every node of an expanded
// quest to a persistent faction, NPC, site or road. Pure and integer-only.
// The world is a plain snapshot so the binder does not depend on the NPC,
// faction or world modules.

enum class EntityKind : std::uint8_t { Faction, Npc, Site, Road };

struct Entity {
    std::string id;
    EntityKind kind = EntityKind::Npc;
    std::string region; // empty when the entity has no home region
    std::uint32_t appearances = 0; // quests it already features in
};

struct World {
    std::vector<Entity> entities;
};

struct Binding {
    NodeId node = 0;
    std::string type; // the node's beat type
    std::string entity; // empty for nodes that need no entity (walk_away)
};

struct Result {
    std::vector<Binding> bindings; // in node id order
    std::string error; // empty on success
};

inline constexpr std::size_t kMaxEntities = 4096;
inline constexpr std::size_t kMaxIdLength = 128;
inline constexpr std::size_t kMaxNodes = 1024;

// Which entity kind a node type binds to. False for types that bind nothing
// (walk_away) or are not known.
bool kindFor(std::string_view nodeType, EntityKind& kind);

// The first node anchors the quest's region. Each node prefers an entity of
// its kind in the anchor's region, then one not yet used in this quest, then
// more appearances (persistent people over fresh ones), then a seeded draw,
// then id. An entity is reused inside one quest only when no unused entity of
// the kind remains. Fails (error set) on an empty graph, a node type that is
// neither bindable nor walk_away, a kind with no candidate, or a hostile world
// (empty or duplicate id, id too long, too many entities).
Result bind(const Graph& graph, const World& world, std::uint64_t seed);

}
