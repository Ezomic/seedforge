#include "test_temp.h"
#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>
#include <forgecli/overlay.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace {

worldforge::ForgedWorld boundWorld(std::uint64_t seed, int size)
{
    worldforge::ForgeConfig config = forgecli::defaultForgeConfig(size);
    std::string error;
    forgecli::ContentPools pools = forgecli::scanContentPools(ICHOR_REPO_DATA, error);
    REQUIRE(error.empty());
    config.itemIds = std::move(pools.itemIds);
    config.npcArchetypes = std::move(pools.npcArchetypes);
    config.dungeonArchetypes = std::move(pools.dungeonArchetypes);
    return worldforge::forgeWorld(seed, size, config, worldforge::ForgePhase::Content);
}

}

TEST_CASE("the viewer overlay carries exactly what the content manifest binds", "[forgecli][viewer][overlay]")
{
    for (const std::uint64_t seed : {1ull, 7ull, 42ull}) {
        INFO("seed " << seed);
        const worldforge::ForgedWorld world = boundWorld(seed, 128);
        REQUIRE(world.complete());
        const worldforge::ContentManifest& manifest = *world.manifest();
        const auto overlay = nlohmann::json::parse(forgecli::overlayJson(world));
        CHECK(overlay["size"] == 128);

        std::set<std::string> ids;
        std::map<std::string, nlohmann::json> byId;
        for (const auto& site : overlay["sites"]) {
            CHECK(ids.insert(site["id"].get<std::string>()).second);
            byId[site["id"].get<std::string>()] = site;
        }
        CHECK(ids == std::set<std::string>(manifest.poiIds.begin(), manifest.poiIds.end()));

        std::size_t items = 0, cast = 0, dungeons = 0;
        for (const auto& [id, site] : byId) {
            items += site["items"].size();
            cast += site["cast"].size();
            dungeons += site.contains("dungeon") ? 1 : 0;
        }
        CHECK(items == manifest.items.size());
        CHECK(cast == manifest.cast.size());
        CHECK(dungeons == manifest.dungeons.size());
        CHECK(!manifest.items.empty());
        CHECK(!manifest.cast.empty());
        CHECK(!manifest.dungeons.empty());
        for (const auto& item : manifest.items) {
            const auto& list = byId.at(item.poiId)["items"];
            CHECK(std::find(list.begin(), list.end(), item.itemId) != list.end());
        }
        for (const auto& member : manifest.cast) {
            bool found = false;
            for (const auto& c : byId.at(member.homePoiId)["cast"]) found = found || (c["id"] == member.npcId && c["archetype"] == member.archetype);
            CHECK(found);
            CHECK(byId.at(member.homePoiId)["kind"] == "settlement");
        }
        for (const auto& d : manifest.dungeons) {
            const auto& site = byId.at(d.poiId);
            CHECK(site["kind"] == "dungeon");
            CHECK(site["dungeon"]["archetype"] == d.archetype);
            CHECK(site["dungeon"]["seed"] == std::to_string(d.seed));
        }
    }
}

TEST_CASE("overlay sites sit on the settlement and POI cells", "[forgecli][viewer][overlay]")
{
    const worldforge::ForgedWorld world = boundWorld(3, 128);
    const auto overlay = nlohmann::json::parse(forgecli::overlayJson(world));
    for (const auto& site : overlay["sites"]) {
        const std::string id = site["id"];
        const std::size_t n = std::stoul(id.substr(id.find('_') + 1));
        if (id.rfind("settlement_", 0) == 0) {
            CHECK(site["x"] == world.settlements()->sites[n].x);
            CHECK(site["y"] == world.settlements()->sites[n].y);
            CHECK(site["kind"] == "settlement");
        } else {
            CHECK(site["x"] == world.pois()->pois[n].x);
            CHECK(site["y"] == world.pois()->pois[n].y);
            CHECK(site["kind"] != "settlement");
        }
    }
}

TEST_CASE("a world that stopped before binding has no overlay", "[forgecli][viewer][overlay]")
{
    const auto world = worldforge::forgeWorld(1, 64, forgecli::defaultForgeConfig(64), worldforge::ForgePhase::Tectonics);
    CHECK(forgecli::overlayJson(world).empty());
}

TEST_CASE("runForge writes the overlay next to the layers and names it in the manifest", "[forgecli][viewer][overlay]")
{
    const auto dir = ichor::test::outputPath(ICHOR_TEST_OUTPUT, "forge_overlay") / "full";
    std::filesystem::remove_all(dir);
    forgecli::RunOptions options;
    options.seedBegin = options.seedEnd = 2;
    options.size = 64;
    options.outDir = dir.string();
    const auto result = forgecli::runForge(options);
    REQUIRE(result.error.empty());
    CHECK(result.seeds[0].overlay == "seed_2/overlay.json");
    std::ifstream file(dir / "seed_2" / "overlay.json", std::ios::binary);
    REQUIRE(file);
    CHECK(nlohmann::json::parse(file)["sites"].is_array());
    CHECK(nlohmann::json::parse(result.manifest)["runs"][0]["overlay"] == "seed_2/overlay.json");

    options.phases = {"tectonics"};
    options.outDir = (dir.parent_path() / "tect").string();
    const auto tect = forgecli::runForge(options);
    REQUIRE(tect.error.empty());
    CHECK(tect.seeds[0].overlay.empty());
    CHECK_FALSE(std::filesystem::exists(dir.parent_path() / "tect" / "seed_2" / "overlay.json"));
    CHECK_FALSE(nlohmann::json::parse(tect.manifest)["runs"][0].contains("overlay"));
}

TEST_CASE("without dungeon archetypes the overlay has ruins and landmarks but no dungeons", "[forgecli][viewer][overlay]")
{
    const auto world = worldforge::forgeWorld(1, 128, forgecli::defaultForgeConfig(128), worldforge::ForgePhase::Content);
    REQUIRE(world.complete());
    const auto overlay = nlohmann::json::parse(forgecli::overlayJson(world));
    std::set<std::string> kinds;
    for (const auto& site : overlay["sites"]) {
        kinds.insert(site["kind"].get<std::string>());
        CHECK_FALSE(site.contains("dungeon"));
    }
    CHECK(kinds.count("settlement") == 1);
    CHECK(kinds.count("ruin") == 1);
    CHECK(kinds.count("dungeon") == 0);
    CHECK(overlay["seed"] == "1");
}
