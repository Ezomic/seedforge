#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ichor {

struct VantageCell {
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const VantageCell&) const = default;
};

// Orders by row then column, the canonical order of every revealed set.
inline bool vantageCellLess(const VantageCell& a, const VantageCell& b)
{
    return a.y != b.y ? a.y < b.y : a.x < b.x;
}

constexpr float kVantageMaxRadiusCells = 64.0f;
constexpr std::size_t kVantageDefaultSampleBudget = 1000000;
constexpr float kVantageMaxAbsHeight = 1.0e6f;

struct VantagePoint {
    VantageCell cell;
    float eyeHeight = 0.0f;   // absolute elevation of the eye, same units as the height field
    float radiusCells = 0.0f; // clamped to kVantageMaxRadiusCells
};

enum class VantageStatus : std::uint8_t {
    Ok,
    InvalidInput, // NaN or infinite eye height or radius, negative radius, or no height function
    Truncated,    // the sample budget ran out; the cells found so far are kept
};

struct VantageReveal {
    VantageStatus status = VantageStatus::Ok;
    std::vector<VantageCell> cells; // sorted, unique
    std::size_t samples = 0;        // height samples taken, never above the budget
};

// Height of the terrain at a cell. A non-finite answer means unknown: that cell is never revealed
// and never blocks the view. Finite answers are clamped to +-kVantageMaxAbsHeight.
using VantageHeightFn = std::function<float(std::int32_t x, std::int32_t y)>;

// Pure and deterministic. A cell within the radius is revealed when the line from the eye to the
// terrain at that cell clears every cell between them (Bresenham, ties favour the viewer). The vantage
// cell itself is always revealed. Cost is bounded by the radius cap and by sampleBudget.
// The only maths used is + - * / and sqrt, all correctly rounded, so no libm is involved.
VantageReveal computeVantageReveal(const VantagePoint& vantage, const VantageHeightFn& height,
    std::size_t sampleBudget = kVantageDefaultSampleBudget);

// Merges a revealed set into a map's cell set (kept sorted and unique) and returns how many cells were
// new. Repeat reveals add nothing.
std::size_t mergeRevealedCells(std::vector<VantageCell>& mapCells, const std::vector<VantageCell>& revealed);

}
