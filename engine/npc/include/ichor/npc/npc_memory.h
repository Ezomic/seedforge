#pragma once

#include "ichor/save/chronicle.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ichor {

// What an NPC remembers is derived from the chronicle on demand (doc 10, doc
// 14). Nothing here is saved: a loaded chronicle gives the same memory.

struct MemorySubject {
    std::uint32_t npc = 0;
    std::uint32_t place = 0; // where the NPC is, 0 for nowhere in particular
};

// Decides whether the subject knows of an entry.
using WitnessRule = bool (*)(const MemorySubject&, const ChronicleEntry&);

// The default rule: the entry names the NPC, or happened at the NPC's place
// (place 0 never matches).
bool witnessedByTagOrPlace(const MemorySubject& subject, const ChronicleEntry& entry);

struct RememberedEntry {
    std::size_t index = 0;        // position in the chronicle's entries
    std::uint64_t tick = 0;
    std::uint32_t salience = 0;   // 1 to kMaxSalience
};

constexpr std::uint32_t kMaxSalience = 255 * 1000;
constexpr std::uint64_t kMemoryHorizonTicks = 360000; // salience halves at this age
constexpr std::uint64_t kMaxMemoryAgeTicks = std::uint64_t{1} << 40; // older counts as this old
constexpr std::size_t kMaxRemembered = 16;

// weight * 1000 * horizon / (horizon + age), in integers. It never rises as
// the age grows. An entry from after `now` (clock skew) counts as age 0.
std::uint32_t memorySalience(std::uint8_t weight, std::uint64_t tick, std::uint64_t now);

// The strongest kMaxRemembered entries the subject witnessed, ordered by
// salience descending, then tick descending, then index ascending. Entries
// whose salience is 0 (weight 0) are forgotten.
std::vector<RememberedEntry> memoryOf(const MemorySubject& subject, const std::vector<ChronicleEntry>& entries, std::uint64_t now,
                                      WitnessRule rule = witnessedByTagOrPlace);

}
