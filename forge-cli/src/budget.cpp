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
            // oreKind 1 + oreGrade 1 + arable 1 kept; moisture byte 1 + dist 2 + queue 4 scratch.
            { "resources", 6, 3, 8, 4 },
            // suitability 1 kept; freshWater 1 + ocean 1 + ore 1 + biome lookup 1 + dist 2 + queue 4 + score 1 scratch.
            { "settlements", 10, 1, 11, 4 },
            // heightM 2 + road 1 kept; biome cost 1 + router g 8 + parent 4 + open and closed stamps 8 scratch.
            { "roads", 12, 3, 21, 8 },
            // owner 1 + rival 1 kept; taken 1 + slope 1 + best 4 + bestOwner 1 + settled 4 + heap about 8 scratch.
            { "politics", 16, 2, 19, 8 },
            // The POI list is small; the viewpoint seen mask and the candidate grid (capped) are the scratch.
            { "pois", 4, 0, 2, 1 },
        },
        {
            { "tectonics", { "tectonics" }, 5, 0, 0, 0, 0 },
            // One pass to fill the per-cell inputs and one to assign; inputs are two doubles per cell.
            { "region_tiers", { "tectonics" }, 1, 2, 4, 20, 16 },
            { "erosion", { "tectonics", kErosionStage }, 1, 0, 0, 0, 0 },
            { "hydrology", { "tectonics", kErosionStage, "hydrology" }, 4, 0, 0, 0, 0 },
            { "climate", { "tectonics", kErosionStage, "climate" }, 2, 0, 0, 0, 0 },
            { "biomes", { "tectonics", kErosionStage, "hydrology", "climate", "biomes" }, 4, 0, 0, 0, 0 },
            { "resources", { "tectonics", kErosionStage, "climate", "resources" }, 3, 0, 0, 0, 0 },
            { "settlements", { "tectonics", kErosionStage, "hydrology", "climate", "biomes", "resources", "settlements" }, 2, 0, 0, 0, 0 },
            { "roads", { "tectonics", kErosionStage, "hydrology", "climate", "biomes", "resources", "settlements", "roads" }, 2, 0, 0, 0, 0 },
            { "politics", { "tectonics", kErosionStage, "hydrology", "climate", "biomes", "resources", "settlements", "politics" }, 4, 0, 0, 0, 0 },
            { "pois", { "tectonics", kErosionStage, "hydrology", "climate", "biomes", "resources", "settlements", "roads", "pois" }, 1, 0, 0, 0, 0 },
            // The content layer is a sparse overlay: one fill pass, no scratch beyond the manifest.
            { "binding", { "tectonics", kErosionStage, "hydrology", "climate", "biomes", "resources", "settlements", "roads", "pois" }, 1, 1, 0, 0, 0 },
        },
        worldforge::ErosionParams {},
    };
    return model;
}

namespace {

const PhaseModel* findPhaseModel(const CostModel& model, std::string_view phase)
{
    for (const PhaseModel& p : model.phases) {
        if (p.phase == phase) return &p;
    }
    return nullptr;
}

// Cost of running these phases off one shared forge: each stage any of them lists is paid once, each
// phase adds its own layer work, and every stage's result stays alive until the last phase ends.
std::optional<Cost> sharedCost(const CostModel& model, const std::vector<const PhaseModel*>& phases, int size)
{
    if (size < worldforge::kTectonicsMinSize || size > worldforge::kTectonicsMaxSize) return std::nullopt;

    const std::int64_t cells = static_cast<std::int64_t>(size) * size;
    std::vector<const Stage*> stages;
    std::int64_t passes = 0;
    std::int64_t retained = 0;
    std::int64_t transient = 0;
    std::int64_t maxAlloc = 0;
    std::int64_t maxOutput = 0;
    for (const PhaseModel* pm : phases) {
        passes += (pm->extraPassesPerCell + pm->outputBytesPerCell) * cells;
        retained += pm->extraRetainedBytesPerCell;
        transient = std::max(transient, pm->extraTransientBytesPerCell);
        maxAlloc = std::max(maxAlloc, pm->extraMaxAllocBytesPerCell);
        maxOutput = std::max(maxOutput, pm->outputBytesPerCell);
        for (std::string_view name : pm->stages) {
            const Stage* s = findStage(model, name);
            if (s == nullptr) return std::nullopt;
            if (std::find(stages.begin(), stages.end(), s) != stages.end()) continue;
            stages.push_back(s);
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
    }

    Cost cost;
    cost.cellPasses = passes;
    cost.peakBytes = (retained + transient + maxOutput) * cells;
    cost.maxAllocBytes = maxAlloc * cells;
    return cost;
}

}

std::optional<Cost> phaseCost(const CostModel& model, std::string_view phase, int size)
{
    const PhaseModel* pm = findPhaseModel(model, phase);
    if (pm == nullptr) return std::nullopt;
    return sharedCost(model, {pm}, size);
}

std::optional<Cost> forgeCost(const CostModel& model, int size)
{
    std::vector<const PhaseModel*> phases;
    for (const Phase& p : phaseRegistry()) {
        const PhaseModel* pm = findPhaseModel(model, p.name);
        if (pm == nullptr) return std::nullopt;
        phases.push_back(pm);
    }
    return sharedCost(model, phases, size);
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
