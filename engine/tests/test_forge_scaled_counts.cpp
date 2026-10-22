#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>

#include <worldforge/forge_world.h>
#include <worldforge/world_validation.h>

#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace {

std::string failedGates(const worldforge::ValidationReport& report)
{
    std::string out;
    for (const auto& g : report.gates) {
        if (g.status == worldforge::GateStatus::Fail) out += std::format(" {}={} (min {})", g.id, g.value, g.min);
    }
    return out;
}

}

TEST_CASE("forge-cli defaults keep the counts and ranges at or below size 256", "[forge_cli][scaling]")
{
    const worldforge::ForgeConfig library;
    for (const int size : { 64, 128, 256 }) {
        const auto config = forgecli::defaultForgeConfig(size);
        CHECK(config.settlements.maxSettlements == library.settlements.maxSettlements);
        CHECK(config.pois.maxPois == library.pois.maxPois);
        CHECK(config.pois.viewRangeCells == library.pois.viewRangeCells);
        CHECK(config.landmarks.viewRangeCells == library.landmarks.viewRangeCells);
    }
}

TEST_CASE("forge-cli defaults scale counts and sight ranges with area above size 256", "[forge_cli][scaling]")
{
    const auto at512 = forgecli::defaultForgeConfig(512);
    const auto at1024 = forgecli::defaultForgeConfig(1024);
    CHECK(at512.settlements.maxSettlements == 80);
    CHECK(at512.pois.maxPois == 64);
    CHECK(at512.pois.viewRangeCells == 128);
    CHECK(at512.landmarks.viewRangeCells == 192);
    CHECK(at1024.settlements.maxSettlements == worldforge::kSettlementMaxSites);
    CHECK(at1024.pois.maxPois == worldforge::kPoiMaxPois);
    CHECK(at1024.pois.viewRangeCells == worldforge::kPoiMaxViewRangeCells);
    CHECK(at1024.landmarks.viewRangeCells == worldforge::kLandmarkMaxRangeCells);
    CHECK(forgecli::defaultForgeConfig(worldforge::kTectonicsMaxSize).settlements.maxSettlements <= worldforge::kSettlementMaxSites);
}

namespace {

using Blockers = std::map<std::uint64_t, std::string>;

Blockers firstAttemptBlockers(int size)
{
    const worldforge::GateThresholds thresholds = forgecli::sanityThresholds();
    Blockers out;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, size, forgecli::defaultForgeConfig(size));
        const std::string cause = world.complete() ? failedGates(worldforge::validateWorld(world, thresholds)) : "did not forge: " + world.error();
        if (!cause.empty()) out[seed] = cause;
    }
    return out;
}

std::string gateIds(const Blockers& b)
{
    std::string out;
    for (const auto& [seed, cause] : b) out += std::format(" seed {}:{}", seed, cause);
    return out;
}

}

// Hidden because 16 full forges are too slow for the sanitizer job; run with: ichor_tests_world "[.scaling_sanity]"
// The expected blockers are the honest ones: a seed listed here fails its gate for a world-shape reason, not a config one.
TEST_CASE("seeds 1 to 8 at 256 fail only the land share gate of the thin seeds on the first attempt", "[.scaling_sanity]")
{
    const Blockers blockers = firstAttemptBlockers(256);
    INFO(gateIds(blockers));
    CHECK(blockers.size() == 2);
    for (const std::uint64_t seed : { 4ull, 5ull }) {
        REQUIRE(blockers.contains(seed));
        CHECK(blockers.at(seed).starts_with(" land_share="));
        CHECK(blockers.at(seed).find("landmark_coverage") == std::string::npos);
    }
}

TEST_CASE("seeds 1 to 8 at 1024 reach every sanity gate on the first attempt except thin seed 4", "[.scaling_sanity]")
{
    const Blockers blockers = firstAttemptBlockers(1024);
    INFO(gateIds(blockers));
    REQUIRE(blockers.size() == 1);
    REQUIRE(blockers.contains(4));
    CHECK(blockers.at(4).starts_with(" settlement_density="));
}
