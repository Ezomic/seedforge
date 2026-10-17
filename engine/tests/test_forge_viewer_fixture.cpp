#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("the map viewer fixture matches what forge-cli produces today", "[forgecli][viewer]")
{
    const auto fixture = std::filesystem::path(ICHOR_ART_DIR).parent_path() / "forge-cli" / "viewer" / "fixture";
    std::ifstream file(fixture / "manifest.json", std::ios::binary);
    REQUIRE(file);
    const auto expected = nlohmann::json::parse(file);

    forgecli::RunOptions options;
    options.seedBegin = 1;
    options.seedEnd = 1;
    options.size = 64;
    const auto result = forgecli::runForge(options);
    REQUIRE(result.error.empty());
    const auto actual = nlohmann::json::parse(result.manifest);

    // Regenerate with: ichor_forge_cli --seed 1 --size 64 --out forge-cli/viewer/fixture
    REQUIRE(actual["runs"].size() == 1);
    REQUIRE(expected["runs"].size() == 1);
    const auto& want = expected["runs"][0]["layers"];
    const auto& got = actual["runs"][0]["layers"];
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        INFO(want[i]["name"].get<std::string>());
        CHECK(got[i]["name"] == want[i]["name"]);
        CHECK(got[i]["hash"] == want[i]["hash"]);
    }
}
