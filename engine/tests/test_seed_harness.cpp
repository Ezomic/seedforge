#include "ichor/world/seed_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

using namespace ichor;

namespace {

HarnessConfig smallConfig(std::uint32_t count)
{
    HarnessConfig config;
    config.request.desc.sizeMetres = 1024;
    config.request.desc.metresPerSample = 16;
    config.request.desc.cellMetres = 256;
    config.request.desc.waterHeight = 20.0f;
    config.request.carveRivers = true;
    config.request.rivers.riverThreshold = 40;
    config.firstSeed = 1;
    config.count = count;
    config.gates = defaultGates();
    return config;
}

Gate failsOnOddSeeds()
{
    return {"odd", [](const ForgedWorld& world, const ForgeRequest&) -> std::optional<std::string> {
                if (world.heightfield.desc().seed % 2 == 1) return std::string("seed \"is\" odd");
                return std::nullopt;
            }};
}

Gate alwaysFails()
{
    return {"always", [](const ForgedWorld&, const ForgeRequest&) -> std::optional<std::string> { return std::string("never"); }};
}

}

TEST_CASE("the harness passes a few real seeds and the counts add up", "[seed_harness]")
{
    const HarnessReport report = runHarness(smallConfig(4));
    CHECK(report.seeds.size() == 4);
    CHECK(report.passedFirstAttempt + report.passedAfterReroll + report.failedAllAttempts == 4);
    CHECK(report.failedAllAttempts == 0);
    CHECK(report.gates.size() == defaultGates().size());
}

TEST_CASE("the report is deterministic without timing", "[seed_harness]")
{
    const HarnessReport a = runHarness(smallConfig(3));
    const HarnessReport b = runHarness(smallConfig(3));
    CHECK(harnessText(a, false) == harnessText(b, false));
    CHECK(harnessJson(a, false) == harnessJson(b, false));
    CHECK(harnessJson(a, false).find("micros") == std::string::npos);
    CHECK(harnessJson(a, true).find("\"totalMicros\"") != std::string::npos);
}

TEST_CASE("a gate that rejects some worlds is counted per cause, and a reroll can rescue the seed", "[seed_harness]")
{
    HarnessConfig config = smallConfig(6);
    config.gates = {failsOnOddSeeds()};
    const HarnessReport report = runHarness(config);
    CHECK(report.gates.at(0).firstAttemptFailures == 3);
    CHECK(report.gates.at(0).blockedSeeds + report.passedAfterReroll == 3);
    CHECK(report.passedFirstAttempt == 3);
    CHECK(report.gates.at(0).attemptFailures >= 3);
    REQUIRE(report.gates.at(0).sampleReasons.size() == 1);
    CHECK(harnessJson(report, false).find("seed \\\"is\\\" odd") != std::string::npos);
}

TEST_CASE("a gate that always fails blocks every seed and the failing seeds are listed", "[seed_harness]")
{
    HarnessConfig config = smallConfig(3);
    config.gates = {alwaysFails(), failsOnOddSeeds()};
    config.maxRerolls = 2;
    const HarnessReport report = runHarness(config);
    CHECK(report.failedAllAttempts == 3);
    CHECK(report.gates.at(0).blockedSeeds == 3);
    CHECK(report.gates.at(0).attemptFailures == 9);
    CHECK(report.gates.at(1).blockedSeeds < 3);
    CHECK(harnessText(report, false).find("failing seeds: 1 2 3") != std::string::npos);
    for (const SeedOutcome& seed : report.seeds) {
        CHECK_FALSE(seed.passed);
        CHECK(seed.attempt == 2);
    }
}

TEST_CASE("a harness that cannot run says so", "[seed_harness]")
{
    CHECK_THROWS_AS(runHarness(smallConfig(0)), std::invalid_argument);
    CHECK_THROWS_AS(runHarness(smallConfig(kMaxHarnessSeeds + 1)), std::invalid_argument);
    HarnessConfig bad = smallConfig(1);
    bad.request.desc.sizeMetres = 0;
    CHECK_THROWS_AS(runHarness(bad), std::invalid_argument);
}

TEST_CASE("an enormous reroll count is clamped and a seed near the top of the range wraps", "[seed_harness]")
{
    HarnessConfig config = smallConfig(2);
    config.firstSeed = UINT64_MAX;
    config.maxRerolls = UINT32_MAX;
    config.gates = {alwaysFails()};
    config.request.desc.sizeMetres = 256;
    const HarnessReport report = runHarness(config);
    CHECK(report.maxRerolls == 64);
    CHECK(report.seeds.at(1).seed == 0);
}
