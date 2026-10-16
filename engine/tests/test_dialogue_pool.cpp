#include "ichor/npc/dialogue_select.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

using Tags = std::vector<std::pair<std::string, std::string>>;

const std::filesystem::path kData = std::filesystem::path(ICHOR_GAME_DATA_DIR);

// The axes the shipped pool selects on. A tag the pool uses that is not here is a typo.
const std::map<std::string, std::vector<std::string>> kAxes = {
    {"topic", {"greeting", "work", "rumour", "farewell"}},
    {"schedule", {"home", "work", "social", "sleep"}},
    {"relation", {"family", "rival", "creditor", "lover", "grudge"}},
    {"town", {"short_of_grain", "rising_sea", "hulls_lost", "blockaded", "dive_deaths"}},
    {"memory", {"helped_me", "harmed_me", "helped_kin", "harmed_kin", "witnessed", "in_debt"}},
};

DialoguePool shippedPool()
{
    std::vector<std::string> problems;
    auto pool = DialoguePool::load(kData / "dialogue" / "lines.json", problems);
    for (const auto& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(pool.has_value());
    return *pool;
}

std::vector<std::string> shippedTraits()
{
    std::ifstream file(kData / "npcs" / "traits.json", std::ios::binary);
    const auto json = nlohmann::json::parse(file, nullptr, false);
    REQUIRE(json.is_array());
    std::vector<std::string> ids;
    for (const auto& item : json) {
        ids.push_back(item.at("id").get<std::string>());
    }
    return ids;
}

DialogueContext contextOf(Tags tags)
{
    DialogueContext c;
    c.npcId = 1;
    c.tags = std::move(tags);
    return c;
}

// Lines that qualify for exactly these tags, ignoring the ones that need other tags.
std::size_t qualifying(const DialoguePool& pool, const Tags& tags)
{
    const DialogueContext context = contextOf(tags);
    return static_cast<std::size_t>(std::count_if(pool.lines().begin(), pool.lines().end(), [&](const DialogueLine& line) {
        return line.weight > 0 && std::all_of(line.conditions.begin(), line.conditions.end(), [&](const DialogueCondition& c) { return conditionHolds(c, context); });
    }));
}

std::size_t mentioning(const DialoguePool& pool, const std::string& key, const std::string& value)
{
    return static_cast<std::size_t>(std::count_if(pool.lines().begin(), pool.lines().end(), [&](const DialogueLine& line) {
        return std::any_of(line.conditions.begin(), line.conditions.end(), [&](const DialogueCondition& c) { return c.key == key && c.value == value; });
    }));
}

}

TEST_CASE("the shipped dialogue pool loads through the real loader", "[dialogue][data]")
{
    const auto pool = shippedPool();
    CHECK(pool.lines().size() >= 150);
    for (const auto& line : pool.lines()) {
        INFO(line.id);
        CHECK(line.weight >= 1);
        CHECK(line.text.find("--") == std::string::npos);
    }
}

TEST_CASE("every condition in the pool uses a known axis and value", "[dialogue][data]")
{
    const auto pool = shippedPool();
    const auto traits = shippedTraits();
    for (const auto& line : pool.lines()) {
        for (const auto& condition : line.conditions) {
            INFO(line.id << ": " << condition.key << "=" << condition.value);
            REQUIRE_FALSE(condition.negated);
            REQUIRE_FALSE(condition.anyValue);
            if (condition.key == "trait") {
                CHECK(std::find(traits.begin(), traits.end(), condition.value) != traits.end());
            } else {
                const auto axis = kAxes.find(condition.key);
                REQUIRE(axis != kAxes.end());
                CHECK(std::find(axis->second.begin(), axis->second.end(), condition.value) != axis->second.end());
            }
        }
    }
}

TEST_CASE("every value of every axis is asked for by at least three lines", "[dialogue][data]")
{
    const auto pool = shippedPool();
    for (const auto& [key, values] : kAxes) {
        for (const auto& value : values) {
            INFO(key << "=" << value);
            CHECK(mentioning(pool, key, value) >= 3);
        }
    }
    for (const auto& trait : shippedTraits()) {
        INFO("trait=" << trait);
        CHECK(mentioning(pool, "trait", trait) >= 4);
    }
}

TEST_CASE("every trait has a line for every topic", "[dialogue][data]")
{
    const auto pool = shippedPool();
    for (const auto& trait : shippedTraits()) {
        for (const auto& topic : kAxes.at("topic")) {
            INFO("trait=" << trait << " topic=" << topic);
            CHECK(qualifying(pool, {{"trait", trait}, {"topic", topic}}) >= 1);
        }
    }
}

TEST_CASE("every line is reachable under its own conditions", "[dialogue][data]")
{
    const auto pool = shippedPool();
    for (const auto& line : pool.lines()) {
        Tags tags;
        for (const auto& condition : line.conditions) {
            tags.emplace_back(condition.key, condition.value);
        }
        const DialogueContext context = contextOf(tags);
        bool reached = false;
        for (std::uint64_t counter = 0; counter < 400 && !reached; ++counter) {
            DialogueHistory history;
            history.counter = counter;
            const DialogueLine* picked = selectLine(pool, context, history, 11);
            REQUIRE(picked != nullptr);
            reached = picked->id == line.id;
        }
        INFO(line.id);
        CHECK(reached);
    }
}

TEST_CASE("an NPC with no tags still has something to say", "[dialogue][data]")
{
    const auto pool = shippedPool();
    CHECK(qualifying(pool, {}) >= 6);
    CHECK(selectLine(pool, contextOf({}), DialogueHistory{}, 3) != nullptr);
}

TEST_CASE("the combination, not the line, is what varies", "[dialogue][data]")
{
    const auto pool = shippedPool();
    const auto firstPick = [&](Tags tags) {
        const DialogueLine* picked = selectLine(pool, contextOf(std::move(tags)), DialogueHistory{}, 5);
        REQUIRE(picked != nullptr);
        return picked->id;
    };
    const Tags base = {{"trait", "bitter"}, {"topic", "rumour"}};
    Tags starving = base;
    starving.emplace_back("town", "short_of_grain");
    CHECK(qualifying(pool, starving) > qualifying(pool, base));
    CHECK(qualifying(pool, base) > qualifying(pool, {}));
    CHECK(firstPick(base) != firstPick({{"trait", "cheerful"}, {"topic", "rumour"}}));
}
