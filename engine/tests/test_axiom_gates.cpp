#include "ichor/data/world_config.h"
#include "ichor/world/forge.h"
#include "ichor/world/validation.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint32_t kSide = 17;

ForgeRequest request()
{
    ForgeRequest r;
    r.desc.seed = 5;
    r.desc.sizeMetres = 256;
    r.desc.metresPerSample = 16;
    r.desc.cellMetres = 128;
    r.desc.heightRange = 400.0f;
    r.desc.waterHeight = 20.0f;
    r.rivers.riverThreshold = std::numeric_limits<std::uint32_t>::max();
    return r;
}

std::uint16_t sampleOf(float metres)
{
    return static_cast<std::uint16_t>(metres / 400.0f * 65535.0f);
}

template <typename Height>
ForgedWorld worldOf(const ForgeRequest& r, Height metresAt)
{
    std::vector<std::uint16_t> samples;
    for (std::uint32_t z = 0; z < kSide; ++z) {
        for (std::uint32_t x = 0; x < kSide; ++x) {
            samples.push_back(sampleOf(metresAt(x, z)));
        }
    }
    ForgedWorld world;
    world.heightfield = Heightfield(r.desc, samples);
    world.hydrology = computeHydrology(world.heightfield);
    return world;
}

// High ground on the left, a sea 10 m deep on the right.
ForgedWorld coastWorld(const ForgeRequest& r)
{
    return worldOf(r, [](std::uint32_t x, std::uint32_t) { return x < 8 ? 300.0f : 10.0f; });
}

// A tilted plane that stays above the water everywhere: land from 100 m to 300 m, no sea, no lake, no river.
ForgedWorld dryWorld(const ForgeRequest& r)
{
    return worldOf(r, [](std::uint32_t x, std::uint32_t) { return 100.0f + 12.5f * static_cast<float>(x); });
}

// A sea only 2 m deep, shallower than a dive.
ForgedWorld shallowWorld(const ForgeRequest& r)
{
    return worldOf(r, [](std::uint32_t x, std::uint32_t) { return x < 8 ? 300.0f : 18.0f; });
}

std::shared_ptr<const WorldConfig> configWith(const std::string& block, const std::string& key, AxiomValue value)
{
    auto config = std::make_shared<WorldConfig>();
    config->merged.directives.blocks[block][key] = std::move(value);
    return config;
}

std::shared_ptr<const WorldConfig> flood()
{
    auto config = std::make_shared<WorldConfig>();
    auto& blocks = config->merged.directives.blocks;
    blocks["settlement"]["min_elevation_m"] = 210.0;
    blocks["traversal"]["unlock"] = std::vector<std::string>{"boat", "swim_deep"};
    blocks["biomes"]["inject"] = std::vector<std::string>{"salt_blight", "tidal_ruin"};
    blocks["poi"]["weights"] = std::map<std::string, double, std::less<>>{{"drowned_polis", 3.0}, {"pharos", 2.0}};
    return config;
}

std::vector<std::string> failed(const ForgedWorld& world, const ForgeRequest& r)
{
    std::vector<std::string> names;
    for (const GateFailure& failure : runGates(axiomGates(), world, r)) {
        names.push_back(failure.gate);
    }
    return names;
}

bool flags(const std::vector<std::string>& names, const std::string& gate)
{
    return std::find(names.begin(), names.end(), gate) != names.end();
}

}

TEST_CASE("the axiom gates pass every world when there is no WorldConfig or it has no axioms", "[axiom_gates]")
{
    ForgeRequest r = request();
    CHECK(failed(dryWorld(r), r).empty());
    r.axioms = std::make_shared<WorldConfig>();
    CHECK(failed(dryWorld(r), r).empty());
    CHECK(failed(shallowWorld(r), r).empty());
}

TEST_CASE("the default gates answer the same with no WorldConfig, an empty one, and on a forged world", "[axiom_gates]")
{
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        ForgeRequest r = request();
        r.desc.sizeMetres = 1024;
        r.desc.cellMetres = 256;
        r.desc.seed = seed;
        r.carveRivers = true;
        r.rivers.riverThreshold = 40;
        const ForgedWorld world = forgeWorld(r);
        const auto bare = runGates(defaultGates(), world, r);
        r.axioms = std::make_shared<WorldConfig>();
        const auto empty = runGates(defaultGates(), world, r);
        REQUIRE(bare.size() == empty.size());
        CHECK(bare.empty());
        CHECK(defaultGates().size() == 4 + axiomGates().size());
    }
}

TEST_CASE("the Flood leaves land to settle on, dive water, springs' water and a coast on a coast world", "[axiom_gates]")
{
    ForgeRequest r = request();
    r.axioms = flood();
    CHECK(failed(coastWorld(r), r).empty());
}

TEST_CASE("settlement-elevation-land goes red when no dry land reaches the minimum elevation", "[axiom_gates]")
{
    ForgeRequest r = request();
    r.axioms = configWith("settlement", "min_elevation_m", 210.0);
    CHECK_FALSE(flags(failed(coastWorld(r), r), "settlement-elevation-land"));

    r.axioms = configWith("settlement", "min_elevation_m", 350.0);
    CHECK(flags(failed(coastWorld(r), r), "settlement-elevation-land"));
}

TEST_CASE("dive-water-reachable goes red with no water, or only shallows", "[axiom_gates]")
{
    ForgeRequest r = request();
    r.axioms = configWith("traversal", "unlock", std::vector<std::string>{"swim_deep"});
    CHECK_FALSE(flags(failed(coastWorld(r), r), "dive-water-reachable"));
    CHECK(flags(failed(dryWorld(r), r), "dive-water-reachable"));
    CHECK(flags(failed(shallowWorld(r), r), "dive-water-reachable"));

    r.axioms = configWith("traversal", "unlock", std::vector<std::string>{"boat"});
    CHECK_FALSE(flags(failed(dryWorld(r), r), "dive-water-reachable"));
}

TEST_CASE("dive-water-reachable wants the deep water joined to a shore", "[axiom_gates]")
{
    ForgeRequest r = request();
    r.axioms = configWith("traversal", "unlock", std::vector<std::string>{"swim_deep"});
    const ForgedWorld island = worldOf(r, [](std::uint32_t x, std::uint32_t z) {
        const bool pit = x >= 6 && x <= 10 && z >= 6 && z <= 10;
        return pit ? 0.0f : 300.0f;
    });
    CHECK_FALSE(flags(failed(island, r), "dive-water-reachable"));

    const ForgedWorld cut = worldOf(r, [](std::uint32_t x, std::uint32_t z) {
        const bool pit = x >= 6 && x <= 10 && z >= 6 && z <= 10;
        return pit ? 0.0f : (x < 8 ? 300.0f : 18.0f);
    });
    CHECK_FALSE(flags(failed(cut, r), "dive-water-reachable"));
}

TEST_CASE("spring-needs-water goes red on a world with no river, lake or sea", "[axiom_gates]")
{
    ForgeRequest r = request();
    r.axioms = configWith("poi", "weights", std::map<std::string, double, std::less<>>{{"sacred_spring", 2.0}, {"sanctuary", 3.0}});
    CHECK_FALSE(flags(failed(coastWorld(r), r), "spring-needs-water"));
    CHECK(flags(failed(dryWorld(r), r), "spring-needs-water"));

    r.axioms = configWith("poi", "weights", std::map<std::string, double, std::less<>>{{"sanctuary", 3.0}});
    CHECK_FALSE(flags(failed(dryWorld(r), r), "spring-needs-water"));
}

TEST_CASE("coast-needs-shore goes red when the axioms want a coast and the land never meets water", "[axiom_gates]")
{
    ForgeRequest r = request();
    r.axioms = configWith("biomes", "inject", std::vector<std::string>{"tidal_ruin"});
    CHECK_FALSE(flags(failed(coastWorld(r), r), "coast-needs-shore"));
    CHECK(flags(failed(dryWorld(r), r), "coast-needs-shore"));

    r.axioms = configWith("poi", "weights", std::map<std::string, double, std::less<>>{{"pharos", 2.0}});
    CHECK(flags(failed(dryWorld(r), r), "coast-needs-shore"));

    r.axioms = configWith("biomes", "inject", std::vector<std::string>{"sacred_grove"});
    CHECK_FALSE(flags(failed(dryWorld(r), r), "coast-needs-shore"));
}

TEST_CASE("the Flood on a dry world is rejected by every gate that depends on it and forgeValidated reports it", "[axiom_gates]")
{
    ForgeRequest r = request();
    r.axioms = flood();
    const auto names = failed(dryWorld(r), r);
    CHECK(flags(names, "dive-water-reachable"));
    CHECK(flags(names, "coast-needs-shore"));
    CHECK_FALSE(flags(names, "settlement-elevation-land"));

    ForgeRequest big = request();
    big.desc.sizeMetres = 1024;
    big.desc.cellMetres = 256;
    big.desc.waterHeight = 0.0f;
    big.axioms = flood();
    const ValidatedWorld validated = forgeValidated(big, axiomGates(), 2);
    CHECK_FALSE(validated.passed);
    REQUIRE_FALSE(validated.rejected.empty());
    CHECK_FALSE(validated.rejected.front().failures.empty());
}
