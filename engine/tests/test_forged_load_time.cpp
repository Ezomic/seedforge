#include "forged_world_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>

using namespace ichor;

// SEED-750: how long loading the cached forged world (SEED-507) takes before the first frame. The entry is the forged
// world file plus the phase maps beside it, read and validated with their own checksums, the same bytes the app decodes.
// The forge itself is not timed (the shared fixture cache forges a missing entry first); only the load is, as the mean
// of kLoads reads of a warm file. Budgets live in docs/forge-performance.md ("Loading the cached world").
//
// To remeasure after an intended change to the cache format:
//   engine/build/windows-ci/tests/ichor_tests_world.exe "[.forged_load_report]"

namespace {

// GCC defines the macros; clang only answers __has_feature, and the Linux sanitizer job is clang.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

constexpr int kLoads = 5;

struct Budget {
    int grid;
    double meanMs;
};

constexpr Budget kBudgets[] = {{256, 100.0}, {1024, 750.0}};

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

struct Measured {
    bool ok = false;
    double meanMs = 0.0;
    std::string error;
};

Measured measureLoad(int grid)
{
    Measured out;
    const std::filesystem::path directory = test::forgedFixtureDirectory();
    if (directory.empty()) {
        out.error = "the fixture cache is off";
        return out;
    }
    const WorldDesc desc = descFor(1);
    const test::ForgedFixture forged = test::forgedFixture(desc, grid);
    if (!forged.ok()) {
        out.error = forged.error;
        return out;
    }
    const ForgedWorldKey key = forgedWorldKey(desc, grid);
    if (!test::fixture_detail::readEntry(directory, key)) {
        out.error = "the cache entry is missing after the forge";
        return out;
    }
    double totalMs = 0.0;
    for (int i = 0; i < kLoads; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const std::optional<ForgeJobResult> loaded = test::fixture_detail::readEntry(directory, key);
        const auto stop = std::chrono::steady_clock::now();
        if (!loaded || !loaded->world) {
            out.error = "the cache entry did not load";
            return out;
        }
        totalMs += std::chrono::duration<double, std::milli>(stop - start).count();
    }
    out.ok = true;
    out.meanMs = totalMs / kLoads;
    return out;
}

bool withinBudget(double meanMs, double budgetMs)
{
    return meanMs <= budgetMs;
}

}

TEST_CASE("forged load time report", "[.forged_load_report]")
{
    for (const Budget& budget : kBudgets) {
        const Measured m = measureLoad(budget.grid);
        std::printf("grid %d load mean %.2f ms (%s)\n", budget.grid, m.meanMs, m.ok ? "ok" : m.error.c_str());
        std::fflush(stdout);
    }
}

TEST_CASE("loading the cached forged world at 256 and 1024 stays within its budget", "[forged_load_time]")
{
    // A sanitizer slows the load several times over, and a cold cache would forge the 1024 world under it (569 s on main), so
    // the sanitizer jobs skip the measurement; the Windows job enforces the budget.
    if (kSanitized) {
        SKIP("load times are not measured under a sanitizer");
    }
    for (const Budget& budget : kBudgets) {
        INFO("grid " << budget.grid);
        const Measured m = measureLoad(budget.grid);
        INFO(m.error);
        REQUIRE(m.ok);
        INFO("mean load " << m.meanMs << " ms over a budget of " << budget.meanMs << " ms");
        CHECK(withinBudget(m.meanMs, budget.meanMs));
    }
}

TEST_CASE("the load budget goes red when loading is made much slower", "[forged_load_time]")
{
    for (const Budget& budget : kBudgets) {
        INFO("grid " << budget.grid);
        CHECK(withinBudget(budget.meanMs, budget.meanMs));
        CHECK_FALSE(withinBudget(budget.meanMs * 4.0, budget.meanMs));
    }
}
