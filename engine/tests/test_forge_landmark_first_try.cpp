#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>

#include <worldforge/forge_world.h>
#include <worldforge/world_validation.h>

#include <cstdint>
#include <cstdio>
#include <format>
#include <string>

namespace {

worldforge::ForgedWorld forgeToPois(std::uint64_t seed, int size, const worldforge::ForgeConfig& config)
{
    return worldforge::forgeWorld(seed, size, config, worldforge::ForgePhase::Pois);
}

}

TEST_CASE("landmark coverage passes on the first attempt at 128 and 256 for seeds 1 to 8", "[forge_cli][landmark]")
{
    const int minimum = forgecli::sanityThresholds().minLandmarkCoveragePermille;
    REQUIRE(minimum == 550);
    for (const int size : { 128, 256 }) {
        const worldforge::ForgeConfig config = forgecli::defaultForgeConfig(size);
        for (std::uint64_t seed = 1; seed <= 8; ++seed) {
            const worldforge::ForgedWorld world = forgeToPois(seed, size, config);
            INFO(std::format("size {} seed {}: {}", size, seed, world.error()));
            REQUIRE(world.landmarkCoverage().has_value());
            CHECK(world.landmarkCoverage()->coveragePermille >= minimum);
        }
    }
}

TEST_CASE("the coverage reserve is what lifts the weakest seeds over the gate", "[forge_cli][landmark]")
{
    const int minimum = forgecli::sanityThresholds().minLandmarkCoveragePermille;
    worldforge::ForgeConfig withReserve = forgecli::defaultForgeConfig(256);
    worldforge::ForgeConfig withoutReserve = withReserve;
    withoutReserve.pois.coverageReserve = 0;
    REQUIRE(withReserve.pois.coverageReserve > 0);
    int found = 0;
    for (std::uint64_t seed = 1; seed <= 64 && found < 3; ++seed) {
        const auto before = forgeToPois(seed, 256, withoutReserve).landmarkCoverage();
        REQUIRE(before.has_value());
        if (before->coveragePermille >= minimum) continue;
        ++found;
        const auto after = forgeToPois(seed, 256, withReserve).landmarkCoverage();
        REQUIRE(after.has_value());
        INFO("seed " << seed);
        CHECK(after->coveragePermille >= minimum);
    }
    CHECK(found == 3);
}

// Hidden: prints the per-seed table for the ticket. Run with: ichor_tests_world "[.landmark_measure]"
TEST_CASE("landmark coverage per seed and size", "[.landmark_measure]")
{
    for (const int size : { 64, 128, 256, 1024 }) {
        for (std::uint64_t seed = 1; seed <= 16; ++seed) {
            const worldforge::ForgedWorld world = forgeToPois(seed, size, forgecli::defaultForgeConfig(size));
            if (!world.landmarkCoverage()) {
                std::printf("size %d seed %2llu no coverage: %s\n", size, static_cast<unsigned long long>(seed), world.error().c_str());
                continue;
            }
            const auto& c = *world.landmarkCoverage();
            std::printf("size %d seed %2llu pois %zu samples %d near %d visible %d permille %d\n", size, static_cast<unsigned long long>(seed), world.pois()->pois.size(),
                c.sampleCount, c.nearCount, c.visibleCount, c.coveragePermille);
        }
    }
    std::fflush(stdout);
}
