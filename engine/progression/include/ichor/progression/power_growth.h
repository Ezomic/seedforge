#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The design constraint in docs/07-progression.md: total power grows about 2x over
// a full campaign, so an ordinary bandit can still kill a careless player in act
// three. Ratios are in permille (1000 = no growth) so the check is integer only.
constexpr std::int64_t kPowerCapPermille = 2000;
constexpr std::int64_t kSpineMinGrowthPermille = 1500;
constexpr std::int64_t kSpineMaxGrowthPermille = 2000;

// One of the four spine stats (health, stamina, carry, attunement): the value at
// the start of a campaign, the value at its end, and how much of "total power"
// the stat accounts for.
struct SpineStat {
    std::string id;
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::int64_t weightPermille = 0;
};

// Parses and validates a JSON array of {"id", "start", "end", "weight_permille"}
// records. Every problem is appended to `problems` as "<label>: <path>: <what is
// wrong>". Requires exactly the four spine ids, positive integer start values,
// end >= start, each stat growing 50 to 100 percent, weights summing to 1000, and
// total growth within the cap. Returns true, and fills `out` sorted by id, only
// when there are no problems. Never throws on bad input.
bool parsePowerGrowth(std::string_view label, std::string_view text, std::vector<SpineStat>& out, std::vector<std::string>& problems);

// Growth of one stat in permille, rounded down: end * 1000 / start. Zero when a value is out of range.
std::int64_t statGrowthPermille(const SpineStat& stat);

// Weighted mean of the stat growths in permille, rounded down. Zero for an empty
// table or a table whose weights do not sum to 1000.
std::int64_t totalGrowthPermille(const std::vector<SpineStat>& stats);

// Appends one problem per violated limit: a stat outside the 50 to 100 percent
// band, a weight outside 0 to 1000, weights not summing to 1000, or a total above
// the cap. The cap is the rule the design states; the per-stat band is stricter
// today, so the total check backs it up if the band is ever loosened. Safe for any values.
void checkPowerGrowthCap(std::string_view label, const std::vector<SpineStat>& stats, std::vector<std::string>& problems);

}
