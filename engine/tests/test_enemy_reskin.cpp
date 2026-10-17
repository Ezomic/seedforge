#include "ichor/world/enemy_reskin.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::vector<ReskinLayer> parseOk(const std::string& text)
{
    std::vector<ReskinLayer> layers;
    std::vector<std::string> problems;
    REQUIRE(parseReskinLayers("t.json", text, layers, problems));
    REQUIRE(problems.empty());
    return layers;
}

std::vector<std::string> parseBad(const std::string& text)
{
    std::vector<ReskinLayer> layers;
    std::vector<std::string> problems;
    CHECK_FALSE(parseReskinLayers("t.json", text, layers, problems));
    CHECK(layers.empty());
    CHECK_FALSE(problems.empty());
    return problems;
}

std::vector<ReskinLayer> repoLayers()
{
    std::ifstream file(std::filesystem::path(ICHOR_REPO_DATA) / "reskin" / "layers.json", std::ios::binary);
    return parseOk(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}

ReskinLayer layer(const std::string& id, ReskinKind kind, const std::string& key)
{
    ReskinLayer out;
    out.id = id;
    out.kind = kind;
    out.key = key;
    return out;
}

}

TEST_CASE("no matching layer leaves the skeleton unchanged")
{
    const ReskinParams params = resolveReskin({EnemyRole::Swarm, "nobody", "nowhere", 7}, repoLayers());
    CHECK(params == ReskinParams{});
}

TEST_CASE("faction and biome are both applied and differ from either alone")
{
    const auto layers = repoLayers();
    const ReskinParams both = resolveReskin({EnemyRole::Skirmisher, "archaists", "frozen_field", 1}, layers);
    const ReskinParams factionOnly = resolveReskin({EnemyRole::Skirmisher, "archaists", "", 1}, layers);
    const ReskinParams biomeOnly = resolveReskin({EnemyRole::Skirmisher, "", "frozen_field", 1}, layers);
    CHECK(both.scalePermille == 1155);
    CHECK(both.speedPermille == 950);
    CHECK(both.material == "bronze_plate");
    CHECK(both.tint == "#8C6A3F");
    CHECK(both.gear == std::vector<std::string>{"fur_mantle", "bronze_spear", "crested_helm"});
    CHECK(factionOnly != both);
    CHECK(biomeOnly != both);
    CHECK(biomeOnly.material == "frost_fur");
}

TEST_CASE("a faction outranks a biome for material, regardless of file order")
{
    std::vector<ReskinLayer> layers = {layer("a", ReskinKind::Faction, "f"), layer("b", ReskinKind::Biome, "b")};
    layers[0].material = "faction_mat";
    layers[1].material = "biome_mat";
    CHECK(resolveReskin({EnemyRole::Anchor, "f", "b", 0}, layers).material == "faction_mat");
    std::reverse(layers.begin(), layers.end());
    CHECK(resolveReskin({EnemyRole::Anchor, "f", "b", 0}, layers).material == "faction_mat");
}

TEST_CASE("role specific layers apply only to their role and after generic ones")
{
    const auto layers = repoLayers();
    const ReskinParams anchor = resolveReskin({EnemyRole::Anchor, "archaists", "", 0}, layers);
    const ReskinParams skirmisher = resolveReskin({EnemyRole::Skirmisher, "archaists", "", 0}, layers);
    CHECK(anchor.scalePermille == 1050 * 1150 / 1000);
    CHECK(anchor.speedPermille == 900);
    CHECK(anchor.gear.back() == "tower_shield");
    CHECK(skirmisher.scalePermille == 1050);
    CHECK(skirmisher.speedPermille == 1000);
}

TEST_CASE("resolution is independent of layer order")
{
    auto layers = repoLayers();
    const ReskinRequest request{EnemyRole::Anchor, "archaists", "frozen_field", 99};
    const ReskinParams forward = resolveReskin(request, layers);
    std::reverse(layers.begin(), layers.end());
    CHECK(resolveReskin(request, layers) == forward);
}

TEST_CASE("jitter varies individuals within its bound and is deterministic per seed")
{
    const auto layers = repoLayers();
    std::uint32_t low = 100000;
    std::uint32_t high = 0;
    for (std::uint64_t seed = 0; seed < 400; ++seed) {
        const ReskinRequest request{EnemyRole::Skirmisher, "xenoi", "", seed};
        const ReskinParams params = resolveReskin(request, layers);
        CHECK(params == resolveReskin(request, layers));
        CHECK(params.scalePermille >= 920);
        CHECK(params.scalePermille <= 1080);
        low = std::min(low, params.scalePermille);
        high = std::max(high, params.scalePermille);
    }
    CHECK(high - low > 100);
    CHECK(resolveReskin({EnemyRole::Skirmisher, "archaists", "", 5}, layers).scalePermille == resolveReskin({EnemyRole::Skirmisher, "archaists", "", 6}, layers).scalePermille);
}

TEST_CASE("stacked multipliers clamp and gear caps without repeats")
{
    std::vector<ReskinLayer> layers;
    for (int i = 0; i < 6; ++i) {
        ReskinLayer l = layer("x" + std::to_string(i), ReskinKind::Biome, "b");
        l.scalePermille = kMaxReskinPermille;
        l.speedPermille = kMinReskinPermille;
        l.scaleJitterPermille = kMaxReskinJitterPermille;
        l.gear = {"shared", "g" + std::to_string(i), "h" + std::to_string(i)};
        layers.push_back(l);
    }
    const ReskinParams params = resolveReskin({EnemyRole::Swarm, "", "b", std::numeric_limits<std::uint64_t>::max()}, layers);
    CHECK(params.scalePermille <= kMaxReskinPermille);
    CHECK(params.scalePermille >= kMinReskinPermille);
    CHECK(params.speedPermille == kMinReskinPermille);
    CHECK(params.gear.size() == kMaxReskinGear);
    CHECK(std::count(params.gear.begin(), params.gear.end(), "shared") == 1);
}

TEST_CASE("an empty key never matches an empty request field")
{
    const std::vector<ReskinLayer> layers = {layer("e", ReskinKind::Faction, "")};
    CHECK(resolveReskin({EnemyRole::Swarm, "", "", 0}, layers) == ReskinParams{});
}

TEST_CASE("the shipped layers parse and every faction and biome gives a distinct look")
{
    const auto layers = repoLayers();
    CHECK(layers.size() >= 8);
    std::vector<std::string> looks;
    for (const ReskinLayer& l : layers) {
        if (!l.anyRole) {
            continue;
        }
        const ReskinRequest request{EnemyRole::Skirmisher, l.kind == ReskinKind::Faction ? l.key : "", l.kind == ReskinKind::Biome ? l.key : "", 3};
        const ReskinParams params = resolveReskin(request, layers);
        CHECK_FALSE(params.material.empty());
        looks.push_back(params.material + params.tint);
    }
    std::sort(looks.begin(), looks.end());
    CHECK(std::adjacent_find(looks.begin(), looks.end()) == looks.end());
}

TEST_CASE("parser rejects hostile input")
{
    parseBad("");
    parseBad("{}");
    parseBad("[1]");
    parseBad("[{}]");
    parseBad(R"([{"id":"a","kind":"x","key":"k"}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","tint":"red"}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","tint":"#12345"}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","role":"dragon"}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","extra":1}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","scale_permille":0}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","scale_permille":99999999999999}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","scale_permille":-5}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","scale_permille":1.5}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","scale_jitter_permille":501}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","gear":"sword"}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","gear":["a","b","c","d","e","f","g","h","i"]}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k","gear":[1]}])");
    parseBad(R"([{"id":"a","kind":"biome","key":"k"},{"id":"a","kind":"faction","key":"k"}])");
    parseBad(R"([{"id":")" + std::string(65, 'x') + R"(","kind":"biome","key":"k"}])");
    parseBad(std::string(kMaxReskinBytes + 1, ' '));
    parseBad(std::string(5000, '['));
    parseBad(std::string(400000, '['));
    parseBad("[[[[{}]]]]");
}

TEST_CASE("parser accepts a valid layer and reports the failing path")
{
    const auto layers = parseOk(R"([{"id":"a","kind":"faction","key":"k","role":"ambusher","tint":"#aBc012","scale_permille":1200}])");
    REQUIRE(layers.size() == 1);
    CHECK_FALSE(layers[0].anyRole);
    CHECK(layers[0].role == EnemyRole::Ambusher);
    const auto problems = parseBad(R"([{"id":"a","kind":"biome","key":"k"},{"id":"b","kind":"biome","key":"k","tint":"x"}])");
    CHECK(problems[0].find("t.json: [1].tint") == 0);
}
