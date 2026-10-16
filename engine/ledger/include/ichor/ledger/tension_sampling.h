#pragma once

#include "ichor/factions/hostility.h"
#include "ichor/factions/territory.h"
#include "ichor/ledger/ledger_query.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Pass 1 of Tier 1 quest generation (doc 04): pull tensions from live world
// state. Pure and integer-scored: the same inputs and seed always give the same
// list on every platform. The quest grammar that consumes it does not exist yet.

enum class TensionKind : std::uint8_t {
    Consequence = 0, // a ledger fact: its subject wants redress from the player
    Hostility = 1,   // a hostile faction pair: the lower id acts against the other
    Contested = 2,   // cells claimed by two factions: the stronger claimant wants them from the other
};

// Who wants what from whom.
struct Tension {
    TensionKind kind = TensionKind::Consequence;
    std::string wanter;
    std::string holder;
    std::string stake; // "fact:<id>", "hostile", or "cell:<lowest contested cell index>"
    std::uint32_t score = 0; // 1 to kMaxTensionScore
};

constexpr std::uint32_t kMaxTensionScore = 1u << 30;
constexpr std::size_t kMaxTensions = 64;
constexpr std::size_t kMaxTensionCandidates = 4096;
constexpr std::size_t kMaxTensionIdBytes = 128;
constexpr double kMaxSourceWeight = 1000.0;
constexpr std::string_view kPlayerHolder = "player";

struct TensionInputs {
    // Null or empty sources contribute nothing. Not owned.
    const ConsequenceLedger* ledger = nullptr;
    TensionQuery ledgerQuery; // which kinds count; limit is ignored
    const std::vector<HostilityEntry>* hostility = nullptr;
    const TerritoryResult* territory = nullptr;
};

struct TensionConfig {
    std::uint64_t seed = 0;
    // Clamped to kMaxTensions; 0 returns nothing.
    std::size_t cap = 8;
    // Narrative pressure per source, multiplied into its scores. NaN, infinite
    // or negative counts as 0 (the source is silent); above kMaxSourceWeight is clamped.
    double consequenceWeight = 1.0;
    double hostilityWeight = 1.0;
    double contestedWeight = 1.0;
    // A pair scores as hostile from this value up (default: the Hostile relation).
    std::int32_t hostilityThreshold = kHostileAtLeast;
};

// Every tension the inputs support, ordered by score descending, then kind, wanter, holder, stake.
// Stops at kMaxTensionCandidates. Skips entries with empty, over-long or equal ids and malformed
// territory instead of failing.
std::vector<Tension> collectTensions(const TensionInputs& inputs, const TensionConfig& config);

// Draws up to `cap` of the candidates without replacement, each draw weighted by score, using
// the seed; returns them ordered as collectTensions orders. A dominant tension is almost always
// kept, a faint one rarely.
std::vector<Tension> sampleTensions(const TensionInputs& inputs, const TensionConfig& config);

}
