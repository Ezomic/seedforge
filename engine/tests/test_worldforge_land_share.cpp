#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>

#include <worldforge/forge_world.h>
#include <worldforge/tectonics_map.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace worldforge;

namespace {

int landPermille(const std::vector<std::int16_t>& heightM)
{
    std::int64_t land = 0;
    for (const std::int16_t h : heightM) land += h > 0 ? 1 : 0;
    return static_cast<int>(land * 1000 / static_cast<std::int64_t>(heightM.size()));
}

std::vector<int> tectonicLandShares(int size, std::uint64_t firstSeed, std::uint64_t lastSeed)
{
    std::vector<int> shares;
    for (std::uint64_t seed = firstSeed; seed <= lastSeed; ++seed) {
        const auto map = generateTectonics(seed, TectonicsParams{ size, size, 12 });
        REQUIRE(map.has_value());
        shares.push_back(landPermille(map->heightM));
    }
    return shares;
}

}

TEST_CASE("tectonics: seeds 1 to 64 land between 20 and 70 percent for nearly every seed at 128, 256 and 1024", "[worldforge][tectonics][land_share]")
{
    for (const int size : { 128, 256, 1024 }) {
        const std::vector<int> shares = tectonicLandShares(size, 1, 64);
        const auto inBand = std::count_if(shares.begin(), shares.end(), [](int s) { return s >= 200 && s <= 700; });
        INFO("size " << size);
        CHECK(inBand >= 58);
        CHECK(*std::min_element(shares.begin(), shares.end()) >= 150);
        CHECK(*std::max_element(shares.begin(), shares.end()) <= 750);
    }
}

TEST_CASE("tectonics: land share still differs a lot from seed to seed", "[worldforge][tectonics][land_share]")
{
    for (const int size : { 128, 256 }) {
        std::vector<int> shares = tectonicLandShares(size, 1, 64);
        std::sort(shares.begin(), shares.end());
        INFO("size " << size);
        CHECK(shares.back() - shares.front() >= 250);
        CHECK(shares[48] - shares[16] >= 100);
    }
}

TEST_CASE("tectonics: plate 0 is continental, plate 1 is oceanic, and both kinds exist for any plate count", "[worldforge][tectonics][land_share]")
{
    for (std::uint64_t seed = 1; seed <= 24; ++seed) {
        for (const int plates : { kTectonicsMinPlates, 3, 7, 12, kTectonicsMaxPlates }) {
            const auto map = generateTectonics(seed, TectonicsParams{ 64, 64, plates });
            REQUIRE(map.has_value());
            CHECK(map->plates[0].type == PlateType::Continental);
            CHECK(map->plates[1].type == PlateType::Oceanic);
        }
    }
}

// Hidden: prints the land share table for the ticket. Run with: ichor_tests_world "[.land_measure]"
TEST_CASE("land share per seed after tectonics, hydrology and roads", "[.land_measure]")
{
    const char* only = std::getenv("ICHOR_LAND_SIZE");
    const char* from = std::getenv("ICHOR_LAND_FROM_SEED");
    for (const int size : { 128, 256, 1024 }) {
        if (only != nullptr && std::atoi(only) != size) continue;
        const ForgeConfig config = forgecli::defaultForgeConfig(size);
        for (std::uint64_t seed = from != nullptr ? static_cast<std::uint64_t>(std::atoi(from)) : 1; seed <= 64; ++seed) {
            const ForgedWorld world = forgeWorld(seed, size, config, ForgePhase::Roads);
            int tectonic = -1;
            int hydrology = -1;
            int roads = -1;
            if (world.tectonics()) tectonic = landPermille(world.tectonics()->heightM);
            if (world.hydrology()) hydrology = landPermille(world.hydrology()->heightM);
            if (world.roads()) roads = landPermille(world.roads()->heightM);
            std::printf("LAND size %d seed %2llu tectonic %d hydrology %d roads %d\n", size, static_cast<unsigned long long>(seed), tectonic, hydrology, roads);
            std::fflush(stdout);
        }
    }
}
