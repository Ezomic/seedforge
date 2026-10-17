#pragma once

#include <worldforge/erosion_map.h>

#include <algorithm>
#include <cstdint>
#include <optional>

// Performance budget for phase 2 (doc 08, Performance): a deterministic cost model that picks the
// erosion working resolution and iteration count for a requested grid. Cost is counted in cell
// passes (cells * iterations * (1 + thermalPasses)), the same unit as kErosionMaxCellPasses, so no
// wall clock is involved and the same inputs give the same plan on every machine.
//
// Strategy, following doc 08: keep the iteration count and halve the grid (downscaleShift) until
// the cost fits, never going below minWorkSide on the shorter side; if the floor is reached and
// the cost still exceeds the budget, lower the iteration count (at least 1). The caller erodes at
// workWidth x workHeight and upsamples by 2^downscaleShift before phase 3. Calibrating the budget
// against real forge time is a benchmarking job and is not done here.
namespace worldforge {

constexpr int kErosionBudgetMaxSide = 16384;
constexpr int kErosionBudgetDefaultMinSide = 256;

struct ErosionPlan {
    int workWidth = 0;
    int workHeight = 0;
    int downscaleShift = 0; // requested side = work side * 2^shift, rounded up
    ErosionParams params;   // params to erode with at the work resolution
    std::int64_t cellPasses = 0;
    bool fits = false; // false when even one iteration at the floor exceeds the budget
};

inline std::int64_t erosionCellPasses(int width, int height, const ErosionParams& params)
{
    if (width <= 0 || height <= 0 || params.iterations <= 0 || params.thermalPasses < 0) return 0;
    return static_cast<std::int64_t>(width) * height * params.iterations * (1 + static_cast<std::int64_t>(params.thermalPasses));
}

// nullopt when a dimension is outside [1, kErosionBudgetMaxSide], budgetCellPasses <= 0,
// minWorkSide < 1, or params are out of range. The budget is clamped to kErosionMaxCellPasses.
inline std::optional<ErosionPlan> planErosionBudget(int width, int height, const ErosionParams& params, std::int64_t budgetCellPasses,
    int minWorkSide = kErosionBudgetDefaultMinSide)
{
    if (width < 1 || height < 1 || width > kErosionBudgetMaxSide || height > kErosionBudgetMaxSide) return std::nullopt;
    if (budgetCellPasses <= 0 || minWorkSide < 1 || !detail::erosionParamsValid(params)) return std::nullopt;
    const std::int64_t budget = std::min(budgetCellPasses, kErosionMaxCellPasses);

    ErosionPlan plan;
    plan.params = params;
    plan.workWidth = width;
    plan.workHeight = height;
    while (erosionCellPasses(plan.workWidth, plan.workHeight, plan.params) > budget) {
        const int nextW = (plan.workWidth + 1) / 2;
        const int nextH = (plan.workHeight + 1) / 2;
        if (std::min(nextW, nextH) < minWorkSide || (nextW == plan.workWidth && nextH == plan.workHeight)) break;
        plan.workWidth = nextW;
        plan.workHeight = nextH;
        ++plan.downscaleShift;
    }

    ErosionParams single = params;
    single.iterations = 1;
    const std::int64_t perIteration = erosionCellPasses(plan.workWidth, plan.workHeight, single);
    if (params.iterations > 0 && perIteration * params.iterations > budget) {
        plan.params.iterations = static_cast<int>(std::max<std::int64_t>(1, std::min<std::int64_t>(params.iterations, budget / perIteration)));
    }
    plan.cellPasses = erosionCellPasses(plan.workWidth, plan.workHeight, plan.params);
    plan.fits = plan.cellPasses <= budget;
    return plan;
}

}
