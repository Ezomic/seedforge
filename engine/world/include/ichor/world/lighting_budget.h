#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Doc 09 lighting: generated interiors cannot bake light, so how many dynamic lights
// are live at once is the main perf risk. This picks which candidates to light.

constexpr std::size_t kMaxLightCandidates = 4096;
constexpr std::int32_t kMaxLightCoordMm = 50'000'000; // positions clamp to +-50 km
constexpr std::uint32_t kMaxHysteresis256 = 256;

struct LightCandidate {
    std::uint32_t id = 0;
    std::int32_t roomId = 0;
    std::int32_t xMm = 0;
    std::int32_t yMm = 0;
    std::int32_t zMm = 0;
    std::uint32_t cost = 1; // abstract cost units, must be at least 1
    bool guide = false;     // lights the way to the objective, never starved by clutter
};

struct LightingBudget {
    std::uint32_t maxCost = 0;
    std::uint32_t maxLights = 0;
    // A light already live keeps its slot against a challenger unless the challenger's
    // squared distance is smaller by this fraction (in 1/256ths, clamped to 256).
    std::uint32_t hysteresis256 = 64;
};

struct LightingViewer {
    std::int32_t roomId = 0;
    std::int32_t xMm = 0;
    std::int32_t yMm = 0;
    std::int32_t zMm = 0;
};

struct LightingSelection {
    std::vector<std::uint32_t> active; // ascending id
    std::uint32_t spent = 0;
};

// Priority: guide lights, then lights in the viewer's room, then everything else; within
// a tier nearer wins, ties go to a light already live and then to the smaller id. A
// candidate that does not fit the remaining cost or count is skipped, smaller ones may
// still fit. `previouslyActive` is the last selection's `active`. Invalid candidates
// (zero cost, duplicate id, beyond kMaxLightCandidates) are skipped and described in
// `problems`. Deterministic, integer only, bad input is reported, not thrown.
LightingSelection selectLights(const std::vector<LightCandidate>& candidates, const LightingViewer& viewer, const LightingBudget& budget, const std::vector<std::uint32_t>& previouslyActive, std::vector<std::string>& problems);

// Keeps the last selection so callers just step it once per update.
class LightingBudgeter {
public:
    explicit LightingBudgeter(LightingBudget budget);

    const LightingSelection& update(const std::vector<LightCandidate>& candidates, const LightingViewer& viewer, std::vector<std::string>& problems);
    const LightingSelection& current() const { return current_; }

private:
    LightingBudget budget_;
    LightingSelection current_;
};

}
