#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 04: every quest must move a visible number, or it was a chore. A quest
// declares what its completion changes; this check says whether any of that is
// observable. Pure and integer-only, so it is identical on every platform.
// It is a standalone check: the quest validation pass calls it, nothing here
// depends on that pass.

enum class VisibleMetric : std::uint8_t {
    Territory = 0,       // cells gained (positive) or lost (negative) by the target faction
    SupplyStatus = 1,    // stock level change at the target settlement
    FactionStrength = 2, // strength change of the target faction
    Route = 3,           // a route opening (positive) or closing (negative)
};

struct QuestEffect {
    VisibleMetric metric = VisibleMetric::Territory;
    std::string target; // lowercase letters, digits and the characters - _ . :
    std::int64_t delta = 0;
};

enum class [[nodiscard]] VisibleNumberVerdict : std::uint8_t {
    Moves = 0,     // at least one visible number changes
    NoEffects,     // the quest declares nothing: a chore
    AllCancel,     // effects are declared but every number nets to zero: a chore
    InvalidEffect, // a bad metric, target or delta
    TooManyEffects,
    UnknownTarget, // a target is not in the supplied set of known ids
};

constexpr std::size_t kMaxQuestEffects = 64;
constexpr std::size_t kMaxEffectTargetBytes = 128;
constexpr std::int64_t kMaxEffectDelta = 1000000000;

struct VisibleNumberReport {
    VisibleNumberVerdict verdict = VisibleNumberVerdict::NoEffects;
    // How many distinct (metric, target) numbers end up changed. 0 unless Moves.
    std::size_t movedNumbers = 0;
    // Index of the first offending effect for InvalidEffect and UnknownTarget.
    std::size_t badIndex = 0;
};

// knownTargets, when non-null, must list every id the quest may touch (factions,
// settlements, routes); a target outside it fails with UnknownTarget. Null skips that check.
// Effects on the same metric and target are summed before judging, so a quest that
// gives a faction a cell and takes the same cell away moved nothing.
VisibleNumberReport checkVisibleNumber(const std::vector<QuestEffect>& effects, const std::set<std::string, std::less<>>* knownTargets = nullptr);

inline bool movesVisibleNumber(const std::vector<QuestEffect>& effects, const std::set<std::string, std::less<>>* knownTargets = nullptr)
{
    return checkVisibleNumber(effects, knownTargets).verdict == VisibleNumberVerdict::Moves;
}

}
