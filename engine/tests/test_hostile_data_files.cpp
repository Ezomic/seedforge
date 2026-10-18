#include "hostile_text.h"

#include "ichor/data/data_set.h"
#include "ichor/graph/mission.h"
#include "ichor/graph/quest_expansion.h"
#include "ichor/npc/dialogue_select.h"

#include <forgecli/forge.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

// File-based loaders (SEED-463): the mutated text is written to a scratch file in a unique directory and
// the loader is pointed at it, so a hostile file is exercised through the same path the game uses.

namespace {

namespace fs = std::filesystem;

struct ScratchDir {
    fs::path path;
    explicit ScratchDir(const std::string& tag) : path(hostile::uniqueDir(tag)) {}
    ~ScratchDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void spit(const fs::path& path, std::string_view text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void expectClean(const hostile::TextReport& report)
{
    INFO(hostile::summary(report));
    CHECK(report.cases > 100);
    CHECK(report.accepted > 0);
    CHECK(report.rejected > 0);
    CHECK(report.ok());
}

hostile::TextOptions lighter()
{
    hostile::TextOptions options;
    options.randomMutations = 500;
    return options;
}

}

TEST_CASE("hostile: data table file through DataSet::loadDirectory", "[hostile][data_set]")
{
    ScratchDir dir("data_set");
    const std::string valid = slurp(fs::path(ICHOR_REPO_DATA) / "catalogue" / "consumables.json");
    REQUIRE_FALSE(valid.empty());
    const fs::path file = dir.path / "catalogue" / "items.json";
    expectClean(hostile::mutateText(valid, [&](std::string_view text) {
        spit(file, text);
        DataSet set;
        return set.loadDirectory(dir.path);
    }, lighter()));
}

TEST_CASE("hostile: dialogue file through DialoguePool::load", "[hostile][dialogue]")
{
    ScratchDir dir("dialogue");
    const std::string valid = slurp(fs::path(ICHOR_REPO_DATA) / "dialogue" / "lines.json");
    REQUIRE_FALSE(valid.empty());
    const fs::path file = dir.path / "lines.json";
    hostile::TextOptions options;
    options.randomMutations = 200;
    options.maxNumberSites = 12;
    expectClean(hostile::mutateText(valid, [&](std::string_view text) {
        spit(file, text);
        std::vector<std::string> problems;
        return DialoguePool::load(file, problems).has_value();
    }, options));
}

TEST_CASE("hostile: mission ruleset file through loadRuleset", "[hostile][mission]")
{
    ScratchDir dir("mission_rules");
    const std::string valid = slurp(fs::path(ICHOR_REPO_DATA) / "dungeon_missions" / "mission_rules.json");
    REQUIRE_FALSE(valid.empty());
    const fs::path file = dir.path / "rules.json";
    expectClean(hostile::mutateText(valid, [&](std::string_view text) {
        spit(file, text);
        return graph::mission::loadRuleset(file).error.empty();
    }, lighter()));
}

TEST_CASE("hostile: quest ruleset file through loadRuleset", "[hostile][quest]")
{
    ScratchDir dir("quest_rules");
    const std::string valid = slurp(fs::path(ICHOR_REPO_DATA) / "quests" / "expansion_rules.json");
    REQUIRE_FALSE(valid.empty());
    const fs::path file = dir.path / "rules.json";
    expectClean(hostile::mutateText(valid, [&](std::string_view text) {
        spit(file, text);
        return graph::quest::loadRuleset(file).error.empty();
    }, lighter()));
}

TEST_CASE("hostile: forge-cli data directory through scanContentPools", "[hostile][forge_cli]")
{
    ScratchDir dir("forge_pools");
    const std::string valid = slurp(fs::path(ICHOR_REPO_DATA) / "catalogue" / "materials.json");
    REQUIRE_FALSE(valid.empty());
    const fs::path file = dir.path / "catalogue" / "items.json";
    expectClean(hostile::mutateText(valid, [&](std::string_view text) {
        spit(file, text);
        std::string error;
        const forgecli::ContentPools pools = forgecli::scanContentPools(dir.path.string(), error);
        return error.empty() && !pools.itemIds.empty();
    }, lighter()));
}

TEST_CASE("hostile: a dialogue file of a hundred thousand bad lines reports a bounded number of problems", "[hostile][dialogue]")
{
    ScratchDir dir("dialogue_problems");
    std::string text = "[";
    for (int i = 0; i < 100000; ++i) text += "1,";
    text += "1]";
    const fs::path file = dir.path / "lines.json";
    spit(file, text);
    std::vector<std::string> problems;
    CHECK_FALSE(DialoguePool::load(file, problems).has_value());
    CHECK_FALSE(problems.empty());
    CHECK(problems.size() <= 200);
}
