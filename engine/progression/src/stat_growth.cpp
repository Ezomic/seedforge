#include "ichor/progression/stat_growth.h"

#include <algorithm>

namespace ichor {

namespace {

constexpr std::int64_t kMaxValue = 1'000'000'000;

}

std::int64_t statProgressPermille(std::int64_t xp)
{
    const std::int64_t clamped = std::clamp<std::int64_t>(xp, 0, kStatMaxXp);
    return clamped * 1000 / (clamped + kStatHalfXp);
}

std::int64_t statValueAtXp(const SpineStat& stat, std::int64_t xp)
{
    if (stat.start < 0 || stat.start > kMaxValue || stat.end < stat.start || stat.end > kMaxValue) {
        return stat.start;
    }
    return stat.start + (stat.end - stat.start) * statProgressPermille(xp) / 1000;
}

std::vector<SpineStat> statsAtXp(const std::vector<SpineStat>& stats, std::int64_t xp)
{
    std::vector<SpineStat> out = stats;
    for (SpineStat& stat : out) {
        stat.end = statValueAtXp(stat, xp);
    }
    return out;
}

}
