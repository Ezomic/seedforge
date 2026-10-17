#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor::graph::spine {

enum class SlotKind : std::uint8_t { Faction, Npc };

// One role in a hand-authored spine beat ("the rival power", "the informant").
// `needsTag` limits the candidates to those carrying the tag, empty for any.
// `memberOf` names a Faction slot; an Npc slot with it must be bound to a
// person who belongs to the faction bound there. `hostileTo` names a Faction
// slot; a Faction slot with it must be bound to a faction hostile to that one.
struct Slot {
    std::string id;
    SlotKind kind = SlotKind::Faction;
    std::string needsTag;
    std::string memberOf;
    std::string hostileTo;
};

// The spine as plain data: the roles every seed's main quest needs.
struct Spine {
    std::vector<Slot> slots;
};

struct Faction {
    std::string id;
    std::vector<std::string> tags;
    std::vector<std::string> hostileTo;
};

struct Npc {
    std::string id;
    std::string faction; // empty when unaffiliated
    std::vector<std::string> tags;
};

// What this seed produced.
struct Cast {
    std::vector<Faction> factions;
    std::vector<Npc> npcs;
};

struct Binding {
    std::string slot;
    std::string entity;
};

enum class Code {
    Empty,
    TooLarge,
    BadSlotId,
    DuplicateSlot,
    BadReference,
    ReferenceCycle,
    BadCastId,
    DuplicateCast,
    NoCandidate,
    Unsatisfiable,
    OverBudget,
};

struct Finding {
    Code code = Code::Empty;
    std::string slot; // the slot concerned, empty when none
    std::string message;
};

struct Result {
    bool ok = false;
    std::vector<Binding> bindings; // in spine slot order; empty unless ok
    std::vector<Finding> findings; // why binding failed
};

inline constexpr std::size_t kMaxSlots = 64;
inline constexpr std::size_t kMaxCastEntities = 4096;
inline constexpr std::size_t kMaxTags = 32;
inline constexpr std::size_t kMaxIdLength = 64;
inline constexpr std::size_t kSearchBudget = 200000;

// Binds every slot to a distinct entity of its kind so each constraint holds.
// Persistent entities only: nothing is invented, so a spine the cast cannot
// serve fails with findings instead of a made-up person. Among valid
// bindings the choice depends on `seed` alone (candidates are ordered by id,
// then shuffled), so the same seed and cast always give the same cast list.
// Never throws; hostile input (oversized, duplicate ids, dangling or cyclic
// references) is rejected with findings, and the search is capped.
Result bind(const Spine& spine, const Cast& cast, std::uint64_t seed);

} // namespace ichor::graph::spine
