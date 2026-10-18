#include <forgecli/budget.h>

#include <algorithm>

#include <forgecli/forge.h>
#include <worldforge/erosion_budget.h>
#include <worldforge/tectonics_map.h>

namespace forgecli {

namespace {

constexpr std::string_view kErosionStage = "erosion";

const Stage* findStage(const CostModel& model, std::string_view name)
{
    for (const Stage& s : model.stages) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

}

const CostModel& defaultCostModel()
{
    // Bytes per cell come from the element sizes of the vectors each stage allocates. Passes per
    // cell are loop counts: a flat scan is 1, a heap or sort step is counted as about 8.
    static const CostModel model = {
        {
            // plateId 1 + boundary 1 + heightM 2 kept; taken 1 + otherPlate 1 + dist 2 + source 4 + queue 4 scratch.
            { "tectonics", 16, 4, 12, 4 },
            // heightM 2 kept; hydraulicPass holds receiver 4, drop 4, keys 8, area 4, sediment 8, delta 4, z 4: about 40.
            { kErosionStage, 0, 2, 40, 8 },
            // filledM 2 + lakeDepthM 2 + heightM 2 + flowDir 1 + accumulation 4 + river 1 kept; parent 4 + order 4 + queued 1 + heap 8 scratch.
            { "hydrology", 10, 12, 17, 8 },
            // temperatureC10 2 + moisture 2 kept; humidity 4 scratch.
            { "climate", 6, 4, 4, 4 },
            // biome 1 + secondary 1 + blend 1 kept; oceanDistance 2 + queue 4 + coast 2 scratch.
            { "biomes", 8, 3, 8, 4 },
        },
        {
            { "tectonics", { "tectonics" }, 5, 0, 0, 0, 0 },
            // One pass to fill the per-cell inputs and one to assign; inputs are two doubles per cell.
            { "region_tiers", { "tectonics" }, 1, 2, 4, 20, 16 },
            { "hydrology", { "tectonics", kErosionStage, "hydrology" }, 4, 0, 0, 0, 0 },
            { "biomes", { "tectonics", kErosionStage, "hydrology", "climate", "biomes" }, 4, 0, 0, 0, 0 },
        },
        worldforge::ErosionParams {},
    };
    return model;
}

std::optional<Cost> phaseCost(const CostModel& model, std::string_view phase, int size)
{
    if (size < worldforge::kTectonicsMinSize || size > worldforge::kTectonicsMaxSize) return std::nullopt;
    const PhaseModel* pm = nullptr;
    for (const PhaseModel& p : model.phases) {
        if (p.phase == phase) {
            pm = &p;
            break;
        }
    }
    if (pm == nullptr) return std::nullopt;

    const std::int64_t cells = static_cast<std::int64_t>(size) * size;
    std::int64_t passes = (pm->extraPassesPerCell + pm->outputBytesPerCell) * cells;
    std::int64_t retained = pm->extraRetainedBytesPerCell;
    std::int64_t transient = pm->extraTransientBytesPerCell;
    std::int64_t maxAlloc = pm->extraMaxAllocBytesPerCell;
    for (std::string_view name : pm->stages) {
        const Stage* s = findStage(model, name);
        if (s == nullptr) return std::nullopt;
        std::int64_t stagePasses = s->passesPerCell * cells;
        if (name == kErosionStage) {
            stagePasses = worldforge::erosionCellPasses(size, size, model.erosion);
            if (stagePasses == 0) return std::nullopt;
        }
        passes += stagePasses;
        retained += s->retainedBytesPerCell;
        transient = std::max(transient, s->transientBytesPerCell);
        maxAlloc = std::max(maxAlloc, s->maxAllocBytesPerCell);
    }

    Cost cost;
    cost.cellPasses = passes;
    cost.peakBytes = (retained + transient + pm->outputBytesPerCell) * cells;
    cost.maxAllocBytes = maxAlloc * cells;
    return cost;
}

std::optional<Cost> forgeCost(const CostModel& model, int size)
{
    Cost total;
    for (const Phase& p : phaseRegistry()) {
        const auto c = phaseCost(model, p.name, size);
        if (!c) return std::nullopt;
        total.cellPasses += c->cellPasses;
        total.peakBytes = std::max(total.peakBytes, c->peakBytes);
        total.maxAllocBytes = std::max(total.maxAllocBytes, c->maxAllocBytes);
    }
    return total;
}

bool withinBudget(const Cost& cost, const ForgeBudget& budget)
{
    return cost.cellPasses <= budget.cellPasses && cost.peakBytes <= budget.peakBytes && cost.maxAllocBytes <= budget.maxAllocBytes;
}

std::int64_t forgeErosionBudgetCellPasses()
{
    return worldforge::erosionCellPasses(kForgeTargetSize, kForgeTargetSize, worldforge::ErosionParams {});
}

}
