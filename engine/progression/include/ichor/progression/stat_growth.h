#pragma once

#include "ichor/progression/power_growth.h"

#include <cstdint>
#include <vector>

namespace ichor {

// Turns accumulated discovery XP into spine stat values (docs/07-progression.md).
// Progress follows xp / (xp + half) in permille: it rises fast early, slows later and
// never reaches the end value, so the stat table's `end` is a hard ceiling and the 2x
// power cap holds at any XP. Integer math only; identical on every platform.
constexpr std::int64_t kStatHalfXp = 5000;
constexpr std::int64_t kStatMaxXp = 1'000'000'000;

// Share of the start-to-end span earned at `xp`, in permille (0 to 999). Negative XP counts as zero; XP above the maximum saturates.
std::int64_t statProgressPermille(std::int64_t xp);

// Value of one stat at `xp`: start plus the earned share of (end - start), rounded down.
// Returns start for a stat that is out of range or whose end is below its start.
std::int64_t statValueAtXp(const SpineStat& stat, std::int64_t xp);

// The table with every `end` replaced by the value reached at `xp`, so the power
// functions in power_growth.h can score it. Ids, starts and weights are kept.
std::vector<SpineStat> statsAtXp(const std::vector<SpineStat>& stats, std::int64_t xp);

}
