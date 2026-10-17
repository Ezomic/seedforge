#include <worldforge/erosion_budget.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using namespace worldforge;

TEST_CASE("erosion cost counts cell visits like the erosion cap does", "[worldforge][erosion_budget]")
{
    ErosionParams p;
    p.iterations = 10;
    p.thermalPasses = 2;
    CHECK(erosionCellPasses(100, 50, p) == 5000 * 10 * 3);
    p.iterations = 0;
    CHECK(erosionCellPasses(100, 50, p) == 0);
    p.iterations = 4;
    CHECK(erosionCellPasses(0, 50, p) == 0);
    CHECK(erosionCellPasses(-4, 50, p) == 0);
}

TEST_CASE("a budget that fits leaves resolution and params untouched", "[worldforge][erosion_budget]")
{
    const ErosionParams p;
    const std::int64_t cost = erosionCellPasses(256, 256, p);
    const auto plan = planErosionBudget(256, 256, p, cost);
    REQUIRE(plan.has_value());
    CHECK(plan->downscaleShift == 0);
    CHECK(plan->workWidth == 256);
    CHECK(plan->workHeight == 256);
    CHECK(plan->params.iterations == p.iterations);
    CHECK(plan->cellPasses == cost);
    CHECK(plan->fits);
}

TEST_CASE("a tight budget halves the resolution before touching iterations", "[worldforge][erosion_budget]")
{
    const ErosionParams p;
    const std::int64_t full = erosionCellPasses(4096, 4096, p);
    const auto plan = planErosionBudget(4096, 4096, p, full / 4);
    REQUIRE(plan.has_value());
    CHECK(plan->downscaleShift == 1);
    CHECK(plan->workWidth == 2048);
    CHECK(plan->workHeight == 2048);
    CHECK(plan->params.iterations == p.iterations);
    CHECK(plan->cellPasses == full / 4);
    CHECK(plan->fits);
}

TEST_CASE("one pass over budget forces a further halving", "[worldforge][erosion_budget]")
{
    const ErosionParams p;
    const std::int64_t half = erosionCellPasses(2048, 2048, p);
    const auto plan = planErosionBudget(4096, 4096, p, half - 1);
    REQUIRE(plan.has_value());
    CHECK(plan->downscaleShift == 2);
    CHECK(plan->workWidth == 1024);
}

TEST_CASE("the resolution floor stops halving and iterations absorb the rest", "[worldforge][erosion_budget]")
{
    const ErosionParams p;
    ErosionParams three = p;
    three.iterations = 3;
    const std::int64_t budget = erosionCellPasses(1024, 1024, three);
    const auto plan = planErosionBudget(4096, 4096, p, budget, 1024);
    REQUIRE(plan.has_value());
    CHECK(plan->workWidth == 1024);
    CHECK(plan->params.iterations == 3);
    CHECK(plan->cellPasses <= budget);
    CHECK(plan->fits);
}

TEST_CASE("a budget below one iteration at the floor reports it does not fit", "[worldforge][erosion_budget]")
{
    const auto plan = planErosionBudget(512, 512, ErosionParams{}, 10, 256);
    REQUIRE(plan.has_value());
    CHECK_FALSE(plan->fits);
    CHECK(plan->workWidth == 256);
    CHECK(plan->params.iterations == 1);
}

TEST_CASE("plans are monotone in the budget", "[worldforge][erosion_budget]")
{
    const ErosionParams p;
    std::int64_t lastCost = -1;
    int lastShift = 100;
    for (std::int64_t budget = 1'000; budget <= 2'000'000'000; budget *= 3) {
        const auto plan = planErosionBudget(2048, 2048, p, budget, 64);
        REQUIRE(plan.has_value());
        CHECK(plan->downscaleShift <= lastShift);
        if (plan->fits) {
            CHECK(plan->cellPasses <= budget);
            CHECK(plan->cellPasses >= lastCost);
            lastCost = plan->cellPasses;
        }
        lastShift = plan->downscaleShift;
    }
    CHECK(lastCost > 0);
}

TEST_CASE("planning is deterministic and the plan honours the work cap", "[worldforge][erosion_budget]")
{
    const auto a = planErosionBudget(4096, 3000, ErosionParams{}, 50'000'000);
    const auto b = planErosionBudget(4096, 3000, ErosionParams{}, 50'000'000);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->workWidth == b->workWidth);
    CHECK(a->workHeight == b->workHeight);
    CHECK(a->params.iterations == b->params.iterations);
    CHECK(a->cellPasses <= kErosionMaxCellPasses);
}

TEST_CASE("hostile inputs are rejected without overflow", "[worldforge][erosion_budget]")
{
    const ErosionParams p;
    CHECK_FALSE(planErosionBudget(0, 64, p, 1000).has_value());
    CHECK_FALSE(planErosionBudget(64, -1, p, 1000).has_value());
    CHECK_FALSE(planErosionBudget(kErosionBudgetMaxSide + 1, 64, p, 1000).has_value());
    CHECK_FALSE(planErosionBudget(64, 64, p, 0).has_value());
    CHECK_FALSE(planErosionBudget(64, 64, p, -5).has_value());
    CHECK_FALSE(planErosionBudget(64, 64, p, 1000, 0).has_value());
    CHECK_FALSE(planErosionBudget(64, 64, p, 1000, kTectonicsMinSize - 1).has_value());
    ErosionParams bad;
    bad.iterations = kErosionMaxIterations + 1;
    CHECK_FALSE(planErosionBudget(64, 64, bad, 1000).has_value());

    ErosionParams heavy;
    heavy.iterations = kErosionMaxIterations;
    heavy.thermalPasses = kErosionMaxIterations;
    const auto huge = planErosionBudget(kErosionBudgetMaxSide, kErosionBudgetMaxSide, heavy, std::numeric_limits<std::int64_t>::max());
    REQUIRE(huge.has_value());
    CHECK(huge->cellPasses <= kErosionMaxCellPasses);
}

TEST_CASE("a planned run is accepted by erodeTectonics and stays within budget", "[worldforge][erosion_budget]")
{
    const std::int64_t budget = 64 * 64 * 12;
    const auto plan = planErosionBudget(64, 64, ErosionParams{}, budget, 16);
    REQUIRE(plan.has_value());
    CHECK(plan->fits);
    CHECK(plan->cellPasses <= budget);
    CHECK(plan->workWidth < 64);

    auto small = generateTectonics(7, { plan->workWidth, plan->workHeight, 6 });
    REQUIRE(small.has_value());
    CHECK(erodeTectonics(*small, plan->params).has_value());
}

TEST_CASE("odd and non-square grids round up and the shorter side meets the floor", "[worldforge][erosion_budget]")
{
    ErosionParams p;
    p.thermalPasses = 0;
    const std::int64_t budget = erosionCellPasses(51, 100, p);
    const auto plan = planErosionBudget(101, 200, p, budget, 16);
    REQUIRE(plan.has_value());
    CHECK(plan->downscaleShift == 1);
    CHECK(plan->workWidth == 51);
    CHECK(plan->workHeight == 100);
    CHECK(plan->fits);

    const auto narrow = planErosionBudget(16384, 20, p, 1000, 16);
    REQUIRE(narrow.has_value());
    CHECK(narrow->downscaleShift == 0);
    CHECK(narrow->workHeight == 20);
    CHECK_FALSE(narrow->fits);
}

TEST_CASE("an unfit plan may exceed the work cap and says so", "[worldforge][erosion_budget]")
{
    ErosionParams heavy;
    heavy.thermalPasses = kErosionMaxIterations;
    const auto plan = planErosionBudget(kErosionBudgetMaxSide, kErosionBudgetMaxSide, heavy, kErosionMaxCellPasses, kErosionBudgetMaxSide);
    REQUIRE(plan.has_value());
    CHECK_FALSE(plan->fits);
    CHECK(plan->cellPasses > kErosionMaxCellPasses);
}
