#include "ichor/world/lighting_budget.h"

#include <algorithm>
#include <set>

namespace ichor {

namespace {

static_assert(3ull * (2 * kMaxLightCoordMm) * (2 * kMaxLightCoordMm) * (256 + kMaxHysteresis256) < ~0ull, "ranking key must fit in uint64");

struct Ranked {
    std::size_t index = 0;
    int tier = 2;
    std::uint64_t key = 0;
    bool wasActive = false;
    std::uint32_t id = 0;
};

std::int64_t clampedDelta(std::int32_t a, std::int32_t b)
{
    const auto ca = std::clamp(a, -kMaxLightCoordMm, kMaxLightCoordMm);
    const auto cb = std::clamp(b, -kMaxLightCoordMm, kMaxLightCoordMm);
    return static_cast<std::int64_t>(ca) - static_cast<std::int64_t>(cb);
}

std::uint64_t distanceSquared(const LightCandidate& light, const LightingViewer& viewer)
{
    const std::int64_t dx = clampedDelta(light.xMm, viewer.xMm);
    const std::int64_t dy = clampedDelta(light.yMm, viewer.yMm);
    const std::int64_t dz = clampedDelta(light.zMm, viewer.zMm);
    return static_cast<std::uint64_t>(dx * dx + dy * dy + dz * dz);
}

bool before(const Ranked& a, const Ranked& b)
{
    if (a.tier != b.tier) {
        return a.tier < b.tier;
    }
    if (a.key != b.key) {
        return a.key < b.key;
    }
    if (a.wasActive != b.wasActive) {
        return a.wasActive;
    }
    return a.id < b.id;
}

}

LightingSelection selectLights(const std::vector<LightCandidate>& candidates, const LightingViewer& viewer, const LightingBudget& budget, const std::vector<std::uint32_t>& previouslyActive, std::vector<std::string>& problems)
{
    LightingSelection result;
    const std::uint64_t margin = std::min(budget.hysteresis256, kMaxHysteresis256);

    std::set<std::uint32_t> previous;
    for (std::size_t i = 0; i < previouslyActive.size() && i < kMaxLightCandidates; ++i) {
        previous.insert(previouslyActive[i]);
    }

    if (candidates.size() > kMaxLightCandidates) {
        problems.push_back("candidate list truncated to " + std::to_string(kMaxLightCandidates));
    }

    std::set<std::uint32_t> seen;
    std::vector<Ranked> ranked;
    const std::size_t count = std::min(candidates.size(), kMaxLightCandidates);
    for (std::size_t i = 0; i < count; ++i) {
        const LightCandidate& light = candidates[i];
        if (light.cost == 0) {
            problems.push_back("light " + std::to_string(light.id) + ": zero cost");
            continue;
        }
        if (!seen.insert(light.id).second) {
            problems.push_back("light " + std::to_string(light.id) + ": duplicate id");
            continue;
        }
        Ranked entry;
        entry.index = i;
        entry.id = light.id;
        entry.wasActive = previous.count(light.id) != 0;
        entry.tier = light.guide ? 0 : (light.roomId == viewer.roomId ? 1 : 2);
        const std::uint64_t scale = 256 + (entry.wasActive ? 0 : margin);
        entry.key = distanceSquared(light, viewer) * scale;
        ranked.push_back(entry);
    }
    std::sort(ranked.begin(), ranked.end(), before);

    std::uint64_t spent = 0;
    for (const Ranked& entry : ranked) {
        if (result.active.size() >= budget.maxLights) {
            break;
        }
        const std::uint32_t cost = candidates[entry.index].cost;
        if (spent + cost > budget.maxCost) {
            continue;
        }
        spent += cost;
        result.active.push_back(entry.id);
    }
    std::sort(result.active.begin(), result.active.end());
    result.spent = static_cast<std::uint32_t>(spent);
    return result;
}

LightingBudgeter::LightingBudgeter(LightingBudget budget)
    : budget_(budget)
{
}

const LightingSelection& LightingBudgeter::update(const std::vector<LightCandidate>& candidates, const LightingViewer& viewer, std::vector<std::string>& problems)
{
    current_ = selectLights(candidates, viewer, budget_, current_.active, problems);
    return current_;
}

}
