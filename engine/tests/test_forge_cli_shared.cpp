#include "test_temp.h"
#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>

#include <worldforge/forge_world.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace {

std::filesystem::path scratch(const char* name)
{
    const auto dir = ichor::test::outputPath(ICHOR_TEST_OUTPUT, "forge_cli_shared") / name;
    std::filesystem::remove_all(dir);
    return dir;
}

forgecli::RunOptions oneSeed(std::uint64_t seed, int size = 64)
{
    forgecli::RunOptions o;
    o.seedBegin = o.seedEnd = seed;
    o.size = size;
    return o;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

}

TEST_CASE("the four original phases keep their layer hashes", "[forge_cli][forge_shared]")
{
    // Recorded from the stand-alone per-phase code that forgeWorld replaced (seed 1, size 64).
    const std::map<std::string, std::string> golden = {
        {"plates", "4c69858509baee99"}, {"boundary", "b9e886218c2b7fea"}, {"height", "051d5cdacf4b7b9a"},
        {"region_tier", "4e0bc1c607a035a7"}, {"flow", "7396e0179c0450fa"}, {"rivers", "2785ba395bf1262d"},
        {"lakes", "82d72a57feb055e9"}, {"height_carved", "4fe8ca5e066ce89a"}, {"biome", "4c33c3f6fb2539e2"},
        {"biome_blend", "e47f18e495bb85ba"},
    };
    const auto result = forgecli::runForge(oneSeed(1));
    REQUIRE(result.error.empty());
    REQUIRE(result.seeds[0].error.empty());
    std::size_t matched = 0;
    for (const auto& layer : result.seeds[0].layers) {
        const auto it = golden.find(layer.name);
        if (it == golden.end()) continue;
        INFO(layer.name);
        CHECK(layer.hash == it->second);
        ++matched;
    }
    CHECK(matched == golden.size());
}

TEST_CASE("a shared batch run gives each phase the layers it gives alone", "[forge_cli][forge_shared]")
{
    const auto shared = forgecli::runForge(oneSeed(3));
    REQUIRE(shared.error.empty());
    REQUIRE(shared.seeds[0].error.empty());
    std::size_t at = 0;
    for (const auto& phase : forgecli::phaseRegistry()) {
        const auto alone = phase.run(3, 64);
        REQUIRE(alone.error.empty());
        for (const auto& layer : alone.layers) {
            REQUIRE(at < shared.seeds[0].layers.size());
            INFO(phase.name << " " << layer.name);
            CHECK(shared.seeds[0].layers[at].phase == phase.name);
            CHECK(shared.seeds[0].layers[at].hash == forgecli::layerHash(layer));
            ++at;
        }
    }
    CHECK(at == shared.seeds[0].layers.size());
}

TEST_CASE("a phase subset is forged only as far as it needs", "[forge_cli][forge_shared]")
{
    const auto* tectonics = forgecli::findPhase("tectonics");
    const auto* hydrology = forgecli::findPhase("hydrology");
    const auto* pois = forgecli::findPhase("pois");
    REQUIRE((tectonics && hydrology && pois));
    CHECK(forgecli::highestNeed({}) == worldforge::ForgePhase::Tectonics);
    CHECK(forgecli::highestNeed({tectonics, hydrology}) == worldforge::ForgePhase::Hydrology);
    CHECK(forgecli::highestNeed({pois, tectonics, hydrology}) == worldforge::ForgePhase::Pois);

    const auto world = worldforge::forgeWorld(3, 64, forgecli::defaultForgeConfig(64), worldforge::ForgePhase::Tectonics);
    const auto cut = forgecli::phaseLayers(*hydrology, world);
    CHECK(cut.layers.empty());
    CHECK_FALSE(cut.error.empty());
}

TEST_CASE("every phase draws something that is not flat", "[forge_cli][forge_shared]")
{
    const auto world = worldforge::forgeWorld(11, 128, forgecli::defaultForgeConfig(128));
    REQUIRE(world.complete());
    for (const auto& phase : forgecli::phaseRegistry()) {
        const auto r = forgecli::phaseLayers(phase, world);
        INFO(phase.name);
        REQUIRE(r.error.empty());
        REQUIRE_FALSE(r.layers.empty());
        for (const auto& layer : r.layers) {
            INFO(layer.name);
            const std::set<std::uint8_t> distinct(layer.pixels.begin(), layer.pixels.end());
            CHECK(distinct.size() >= 2);
        }
    }
}

TEST_CASE("a late phase failing keeps the layers of the phases before it", "[forge_cli][forge_shared]")
{
    auto config = forgecli::defaultForgeConfig(64);
    config.settlements.minScore = 255;
    const auto world = worldforge::forgeWorld(1, 64, config);
    REQUIRE_FALSE(world.ok());
    const auto* biomes = forgecli::findPhase("biomes");
    const auto* roads = forgecli::findPhase("roads");
    REQUIRE((biomes && roads));
    CHECK(forgecli::phaseLayers(*biomes, world).error.empty());
    const auto failed = forgecli::phaseLayers(*roads, world);
    CHECK(failed.layers.empty());
    CHECK(*world.failedPhase() == worldforge::ForgePhase::Roads);
    CHECK(failed.error.find("settlement") != std::string::npos);
}

TEST_CASE("the content pools come from the data directory", "[forge_cli][forge_shared]")
{
    const auto dir = scratch("pools");
    writeText(dir / "catalogue" / "gear.json", R"([{"id": "sword"}, {"id": "axe", "name": "Axe"}])");
    writeText(dir / "notable_items" / "gear.json", R"([{"id": "crown"}])");
    writeText(dir / "npcs" / "traits.json", R"([{"id": "pious"}])");
    writeText(dir / "dungeon_skeletons" / "a.json", R"([{"id": "crypt", "rooms": ["1:entry@0,0,0"]}])");
    writeText(dir / "quests" / "ignored.json", "not json at all");
    std::string error;
    const auto pools = forgecli::scanContentPools(dir.string(), error);
    CHECK(error.empty());
    CHECK(pools.itemIds == std::vector<std::string>{"axe", "crown", "sword"});
    CHECK(pools.npcArchetypes == std::vector<std::string>{"pious"});
    CHECK(pools.dungeonArchetypes == std::vector<std::string>{"crypt"});

    std::filesystem::remove_all(dir / "npcs");
    CHECK(forgecli::scanContentPools(dir.string(), error).npcArchetypes.empty());
    CHECK(error.empty());
}

TEST_CASE("hostile data directories are refused with a cause", "[forge_cli][forge_shared]")
{
    std::string error;
    CHECK(forgecli::scanContentPools((scratch("absent") / "nowhere").string(), error).itemIds.empty());
    CHECK_FALSE(error.empty());

    const auto dir = scratch("hostile");
    const auto refused = [&](const std::string& text) {
        std::filesystem::remove_all(dir);
        writeText(dir / "catalogue" / "bad.json", text);
        std::string e;
        const auto pools = forgecli::scanContentPools(dir.string(), e);
        return !e.empty() && pools.itemIds.empty();
    };
    CHECK(refused("{"));
    CHECK(refused(""));
    CHECK(refused(R"({"id": "x"})"));
    CHECK(refused("[1, 2]"));
    CHECK(refused(R"([{"name": "no id"}])"));
    CHECK(refused(R"([{"id": 7}])"));
    CHECK(refused(R"([{"id": "twice"}, {"id": "twice"}])"));
    CHECK(refused(std::string(100000, '[')));
}

TEST_CASE("the repository data binds content to the forged sites", "[forge_cli][forge_shared]")
{
    std::string error;
    const auto pools = forgecli::scanContentPools(ICHOR_REPO_DATA, error);
    REQUIRE(error.empty());
    CHECK_FALSE(pools.itemIds.empty());
    CHECK_FALSE(pools.dungeonArchetypes.empty());

    auto bare = oneSeed(5, 128);
    bare.phases = {"binding"};
    auto bound = bare;
    bound.dataDir = ICHOR_REPO_DATA;
    const auto without = forgecli::runForge(bare);
    const auto with = forgecli::runForge(bound);
    REQUIRE(with.error.empty());
    REQUIRE(with.seeds[0].error.empty());
    REQUIRE(with.seeds[0].layers.size() == 1);
    CHECK(with.seeds[0].layers[0].hash != without.seeds[0].layers[0].hash);
    CHECK(forgecli::runForge(bound).manifest == with.manifest);
}

TEST_CASE("a missing data directory is an error before any seed runs", "[forge_cli][forge_shared]")
{
    auto options = oneSeed(1, 32);
    options.dataDir = (scratch("nodata") / "gone").string();
    const auto result = forgecli::runForge(options);
    CHECK_FALSE(result.error.empty());
    CHECK(result.seeds.empty());
}
