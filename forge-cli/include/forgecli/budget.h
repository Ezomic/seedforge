#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <worldforge/erosion_map.h>

// Deterministic time and memory budget for the full forge (SEED-424). Nothing here measures a
// clock or an allocator: cost is a model, counted in cell passes (one visit of one cell by one
// stage) and bytes per cell, summed over the registered phases for a given square size. The same
// inputs give the same numbers on every machine, so CI can fail on a regression without flaking.
//
// The per-cell factors are estimates read off the stage code (loop counts, vector element sizes),
// not profiler output; see forge-cli/README.md "Forge budget" for how they were derived and how to
// change them. The erosion stage is the exception: its cost is erosionCellPasses() from
// erosion_budget.h, so changing ErosionParams or the erosion defaults moves the total.
namespace forgecli {

// The largest size every registered phase accepts (worldforge::kTectonicsMaxSize). The 4096
// target of doc 08 runs erosion at a downscaled work size, see forgeErosionBudgetCellPasses().
constexpr int kForgeTargetSize = 1024;

struct Stage {
    std::string_view name;
    std::int64_t passesPerCell = 0;         // ignored for the erosion stage, which uses erosionCellPasses
    std::int64_t retainedBytesPerCell = 0;  // result kept alive until the phase ends
    std::int64_t transientBytesPerCell = 0; // scratch alive only while the stage runs
    std::int64_t maxAllocBytesPerCell = 0;  // largest single vector the stage allocates
};

struct PhaseModel {
    std::string_view phase;
    std::vector<std::string_view> stages; // run in order; a phase reruns its prerequisites
    std::int64_t outputBytesPerCell = 0;  // layer pixels produced, also the output passes per cell
    std::int64_t extraPassesPerCell = 0;  // phase-local work outside the shared stages
    std::int64_t extraRetainedBytesPerCell = 0;
    std::int64_t extraTransientBytesPerCell = 0;
    std::int64_t extraMaxAllocBytesPerCell = 0;
};

struct CostModel {
    std::vector<Stage> stages;
    std::vector<PhaseModel> phases;
    worldforge::ErosionParams erosion; // params the erosion stage is costed with
};

struct Cost {
    std::int64_t cellPasses = 0;
    std::int64_t peakBytes = 0;     // retained + largest transient + output, at the worst moment
    std::int64_t maxAllocBytes = 0; // largest single allocation
};

struct ForgeBudget {
    std::int64_t cellPasses = 0;
    std::int64_t peakBytes = 0;
    std::int64_t maxAllocBytes = 0;
};

// The committed budget for the full forge at kForgeTargetSize.
constexpr ForgeBudget kForgeBudget = {
    300'000'000,        // cell passes, about 8% above the model at the target size
    96ll * 1024 * 1024, // peak bytes of the whole shared run
    24ll * 1024 * 1024, // largest single allocation
};

const CostModel& defaultCostModel();

// nullopt when the phase is not in the model or size is outside [kTectonicsMinSize, kTectonicsMaxSize].
std::optional<Cost> phaseCost(const CostModel& model, std::string_view phase, int size);

// Every phase in phaseRegistry() off one shared forge, as the CLI does by default: each stage is paid
// once however many phases list it, each phase adds its layer work, and the maps of every stage stay
// alive together, so memory is the sum of what they retain plus the largest scratch and layer set.
// nullopt when a registered phase has no model entry or the size is out of range.
std::optional<Cost> forgeCost(const CostModel& model, int size);

bool withinBudget(const Cost& cost, const ForgeBudget& budget);

// Cell passes the erosion stage gets at kForgeTargetSize; feed it to worldforge::planErosionBudget
// for sizes above the tectonics limit.
std::int64_t forgeErosionBudgetCellPasses();

}
