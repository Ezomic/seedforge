#pragma once

#include "ichor/data/slot_roll.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 15, tier 1: "this week's world" as a shared topic. A curated list of seeds, one of which is
// featured at a time and rotates by day index. No clock is read here: the caller supplies the day.
constexpr std::int64_t kFeaturedPeriodDays = 7;
constexpr std::size_t kMaxFeaturedSeeds = 64;
constexpr std::size_t kMaxFeaturedTitleBytes = 64;
// Above the five slots on purpose: a pin may name more than a roll yields, and then fails as a mismatch.
constexpr std::size_t kMaxFeaturedAxioms = 8;
constexpr std::size_t kMaxFeaturedAxiomIdBytes = 64;

struct FeaturedSeed {
    std::uint64_t seed = 0;
    std::string title;
    // The axiom ids the seed is meant to roll, as curated. Empty means "do not pin"; otherwise the
    // roll must produce exactly this set, so a changed pool or roll shows up as a failed validation.
    std::vector<std::string> expectedAxioms;

    bool operator==(const FeaturedSeed&) const = default;
};

// Day index 0 is whatever epoch the caller counts from. Negative days are fine.
std::int64_t featuredWeekOf(std::int64_t dayIndex);

// Nullopt for an empty list. Otherwise the entry featured on `dayIndex`; every day of one week
// gives the same entry and consecutive weeks walk the list in order, wrapping.
std::optional<std::size_t> featuredIndexForDay(std::size_t listSize, std::int64_t dayIndex);
const FeaturedSeed* featuredForDay(std::span<const FeaturedSeed> list, std::int64_t dayIndex);

enum class FeaturedProblem {
    None,
    BadTitle,        // empty, too long, or not printable ASCII
    BadAxiomId,      // a pinned id that is empty, too long, or not printable ASCII
    TooManyAxioms,
    NoSlotRoll,      // rollSlots failed on this pool; see `message`
    UnknownExpected, // a pinned id that is not in the pool
    RolledMismatch,  // the roll produced a different axiom set than the entry pins
    DuplicateSeed,   // list only: two entries share a seed
    TooManyEntries,  // list only
    Empty,           // list only
};

struct FeaturedCheck {
    FeaturedProblem problem = FeaturedProblem::None;
    // The entry the problem is in (list checks); 0 for a single-entry check.
    std::size_t index = 0;
    std::string message;
    bool ok() const { return problem == FeaturedProblem::None; }
};

// Rolls the seed over `pool` with `mayCoexist` (null allows every pair) and checks the entry's
// title and pinned axioms against the result.
FeaturedCheck validateFeaturedSeed(const FeaturedSeed& entry, std::span<const Axiom> pool, const AxiomCoexist& mayCoexist = {});

// Every entry through validateFeaturedSeed, plus list-wide rules. Returns the first problem.
FeaturedCheck validateFeaturedList(std::span<const FeaturedSeed> list, std::span<const Axiom> pool, const AxiomCoexist& mayCoexist = {});

// "ichorf1.<payload hex>.<check:4 hex>", lowercase. The check catches a mistyped or truncated
// string. Nullopt when the list breaks the shape limits (titles and ids printable ASCII within
// the caps, at most kMaxFeaturedSeeds entries).
std::optional<std::string> encodeFeaturedList(std::span<const FeaturedSeed> list);

// Nullopt for anything encodeFeaturedList could not have produced. Never throws, and its
// allocation is bounded by the size cap, whatever the text.
std::optional<std::vector<FeaturedSeed>> decodeFeaturedList(std::string_view text);

}
