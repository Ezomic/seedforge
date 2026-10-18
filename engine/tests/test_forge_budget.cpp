#include <catch2/catch_test_macros.hpp>

#include <forgecli/budget.h>
#include <forgecli/forge.h>

#include <worldforge/erosion_budget.h>
#include <worldforge/tectonics_map.h>

#include <chrono>
#include <cstdint>

using namespace forgecli;

TEST_CASE("every registered phase has a cost entry", "[forge_budget]")
{
    const CostModel& model = defaultCostModel();
    for (const Phase& p : phaseRegistry()) {
        INFO("phase " << p.name << " has no entry in defaultCostModel()");
        CHECK(phaseCost(model, p.name, kForgeTargetSize).has_value());
    }
    CHECK_FALSE(phaseCost(model, "no_such_phase", kForgeTargetSize).has_value());
}

TEST_CASE("the full forge at the target size stays inside the committed budget", "[forge_budget]")
{
    const auto cost = forgeCost(defaultCostModel(), kForgeTargetSize);
    REQUIRE(cost.has_value());
    CHECK(cost->cellPasses <= kForgeBudget.cellPasses);
    CHECK(cost->peakBytes <= kForgeBudget.peakBytes);
    CHECK(cost->maxAllocBytes <= kForgeBudget.maxAllocBytes);
    CHECK(withinBudget(*cost, kForgeBudget));
}

TEST_CASE("the budget is tight enough to notice growth", "[forge_budget]")
{
    const auto cost = forgeCost(defaultCostModel(), kForgeTargetSize);
    REQUIRE(cost.has_value());
    CHECK(cost->cellPasses * 10 >= kForgeBudget.cellPasses * 6);
    CHECK(cost->peakBytes * 10 >= kForgeBudget.peakBytes * 6);
    CHECK(cost->maxAllocBytes * 10 >= kForgeBudget.maxAllocBytes * 6);
}

TEST_CASE("doubling the erosion iterations goes over budget", "[forge_budget]")
{
    CostModel model = defaultCostModel();
    model.erosion.iterations *= 2;
    const auto cost = forgeCost(model, kForgeTargetSize);
    REQUIRE(cost.has_value());
    CHECK(cost->cellPasses > kForgeBudget.cellPasses);
    CHECK_FALSE(withinBudget(*cost, kForgeBudget));
}

TEST_CASE("a phase whose per-cell cost doubles goes over budget", "[forge_budget]")
{
    for (std::size_t i = 0; i < defaultCostModel().stages.size(); ++i) {
        CostModel model = defaultCostModel();
        Stage& s = model.stages[i];
        if (s.name == "erosion") continue;
        s.passesPerCell *= 8;
        const auto cost = forgeCost(model, kForgeTargetSize);
        REQUIRE(cost.has_value());
        INFO("stage " << s.name << " at 8x passes");
        CHECK(cost->cellPasses > forgeCost(defaultCostModel(), kForgeTargetSize)->cellPasses);
    }
    CostModel model = defaultCostModel();
    for (Stage& s : model.stages) s.passesPerCell *= 4;
    CHECK_FALSE(withinBudget(*forgeCost(model, kForgeTargetSize), kForgeBudget));
}

TEST_CASE("a bigger scratch buffer trips the memory and allocation bounds", "[forge_budget]")
{
    CostModel model = defaultCostModel();
    for (Stage& s : model.stages) {
        s.transientBytesPerCell *= 2;
        s.maxAllocBytesPerCell *= 2;
    }
    for (PhaseModel& p : model.phases) {
        p.extraTransientBytesPerCell *= 2;
        p.extraMaxAllocBytesPerCell *= 2;
    }
    const auto cost = forgeCost(model, kForgeTargetSize);
    REQUIRE(cost.has_value());
    CHECK(cost->peakBytes > kForgeBudget.peakBytes);
    CHECK(cost->maxAllocBytes > kForgeBudget.maxAllocBytes);
}

TEST_CASE("a phase missing a stage in the model is rejected, not costed as free", "[forge_budget]")
{
    CostModel model = defaultCostModel();
    model.stages.pop_back();
    CHECK_FALSE(forgeCost(model, kForgeTargetSize).has_value());
    CostModel empty;
    CHECK_FALSE(forgeCost(empty, kForgeTargetSize).has_value());
}

TEST_CASE("cost scales with the cell count and rejects sizes outside the phase limits", "[forge_budget]")
{
    const CostModel& model = defaultCostModel();
    const auto small = forgeCost(model, 256);
    const auto big = forgeCost(model, 512);
    REQUIRE(small.has_value());
    REQUIRE(big.has_value());
    CHECK(big->cellPasses == small->cellPasses * 4);
    CHECK(big->peakBytes == small->peakBytes * 4);
    CHECK(big->maxAllocBytes == small->maxAllocBytes * 4);

    CHECK_FALSE(forgeCost(model, worldforge::kTectonicsMinSize - 1).has_value());
    CHECK_FALSE(forgeCost(model, worldforge::kTectonicsMaxSize + 1).has_value());
    CHECK_FALSE(forgeCost(model, 0).has_value());
    CHECK_FALSE(forgeCost(model, -5).has_value());
    CHECK_FALSE(forgeCost(model, 1 << 30).has_value());
}

TEST_CASE("the target size is the largest size every phase accepts", "[forge_budget]")
{
    CHECK(kForgeTargetSize == worldforge::kTectonicsMaxSize);
}

TEST_CASE("the output bytes in the model match the layers the phases write", "[forge_budget]")
{
    constexpr int size = 64;
    for (const PhaseModel& pm : defaultCostModel().phases) {
        const Phase* phase = findPhase(pm.phase);
        REQUIRE(phase != nullptr);
        const PhaseResult result = phase->run(3, size);
        REQUIRE(result.error.empty());
        std::int64_t bytes = 0;
        for (const Layer& l : result.layers) bytes += static_cast<std::int64_t>(l.pixels.size());
        INFO("phase " << pm.phase);
        CHECK(bytes == pm.outputBytesPerCell * size * size);
    }
}

TEST_CASE("the erosion budget for sizes above the tectonics limit downscales to fit", "[forge_budget]")
{
    const std::int64_t budget = forgeErosionBudgetCellPasses();
    CHECK(budget == worldforge::erosionCellPasses(kForgeTargetSize, kForgeTargetSize, worldforge::ErosionParams {}));
    const auto plan = worldforge::planErosionBudget(4096, 4096, worldforge::ErosionParams {}, budget);
    REQUIRE(plan.has_value());
    CHECK(plan->fits);
    CHECK(plan->downscaleShift == 2);
    CHECK(plan->workWidth == 1024);
    CHECK(plan->cellPasses <= budget);
}

// Coarse guard only: the model above is the real budget. The full forge at 256 runs in well under
// a second in Release and a few seconds under ASan/UBSan; 120 s is more than an order of magnitude
// above that, so a loaded CI runner cannot trip it but an accidental quadratic or unbounded loop does.
TEST_CASE("the full forge at 256 finishes inside a generous wall-clock limit", "[forge_budget][wallclock]")
{
    RunOptions options;
    options.seedBegin = 1;
    options.seedEnd = 1;
    options.size = 256;
    const auto start = std::chrono::steady_clock::now();
    const RunResult result = runForge(options);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(result.error.empty());
    REQUIRE(result.seeds.size() == 1);
    CHECK(result.seeds[0].error.empty());
    CHECK(elapsed < std::chrono::seconds(120));
}
