#pragma once

#include "ichor/ledger/consequence_ledger.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Weighted queries over the ledger, the input to Tier 1 quest generation (doc
// 04). Pure: the same ledger and query always give the same answer.

struct KindWeight {
    std::string kind;
    std::uint32_t weight = 0; // clamped to kMaxKindWeight; 0 never scores
};

struct TensionQuery {
    // Only facts of a listed kind score. A kind listed twice uses its highest weight.
    std::vector<KindWeight> kinds;
    // Only facts about this subject score. Empty means any subject.
    std::string subject;
    std::uint64_t now = 0;
    // A fact this many ticks old scores half of a fresh one. Clamped to 1..kMaxHorizonTicks.
    std::uint64_t horizonTicks = 360000;
    // Clamped to kMaxQueryLimit; 0 returns nothing.
    std::size_t limit = 16;
};

struct ScoredFact {
    std::size_t index = 0; // position in ConsequenceLedger::facts()
    std::uint64_t tick = 0;
    std::uint32_t score = 0; // 1 to kMaxKindWeight * 1000
};

constexpr std::uint32_t kMaxKindWeight = 65535;
constexpr std::uint64_t kMaxHorizonTicks = std::uint64_t{1} << 32;
constexpr std::uint64_t kMaxFactAgeTicks = std::uint64_t{1} << 40; // older counts as this old
constexpr std::size_t kMaxQueryLimit = 256;

// weight * 1000 * horizon / (horizon + age), in integers. It never rises as the
// age grows. A fact from after `now` (clock skew) counts as age 0.
std::uint32_t factScore(std::uint32_t weight, std::uint64_t tick, std::uint64_t now, std::uint64_t horizonTicks);

// The highest scoring facts, ordered by score descending, then tick descending,
// then index ascending. Facts scoring 0 are left out.
std::vector<ScoredFact> queryTension(const ConsequenceLedger& ledger, const TensionQuery& query);

}
