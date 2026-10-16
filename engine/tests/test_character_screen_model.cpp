#include "ichor/ui/character_screen_model.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace ichor;

namespace {

const CharacterScreenSection* find(const std::vector<CharacterScreenSection>& view, CharacterSectionKind kind)
{
    for (const auto& section : view) {
        if (section.kind == kind) {
            return &section;
        }
    }
    return nullptr;
}

}

TEST_CASE("an empty input has no sections", "[character_screen]")
{
    CHECK(buildCharacterScreen({}).empty());
}

TEST_CASE("stats follow spine order with growth over base", "[character_screen]")
{
    CharacterScreenInput in;
    in.stats = {{CharacterStat::Attunement, 30, 20}, {CharacterStat::Health, 150, 100}, {CharacterStat::Carry, 40, 40}};
    const auto view = buildCharacterScreen(in);
    const auto* stats = find(view, CharacterSectionKind::Stats);
    REQUIRE(stats != nullptr);
    REQUIRE(stats->lines.size() == 3);
    CHECK(stats->lines[0].text == "Health");
    CHECK(stats->lines[0].value == 150);
    CHECK(stats->lines[0].growthPercent == 50);
    CHECK(stats->lines[1].text == "Carry");
    CHECK(stats->lines[1].growthPercent == 0);
    CHECK(stats->lines[2].text == "Attunement");
    CHECK(stats->lines[2].growthPercent == 50);
}

TEST_CASE("hostile stat values are dropped or lose growth", "[character_screen]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CharacterScreenInput in;
    in.stats = {{CharacterStat::Health, nan, 1}, {CharacterStat::Health, 90, 100}, {CharacterStat::Health, 999, 1},
        {CharacterStat::Stamina, inf, 1}, {CharacterStat::Carry, -5, 1}, {CharacterStat::Attunement, 1e300, 1},
        {CharacterStat::Stamina, 10, nan}};
    const auto view = buildCharacterScreen(in);
    const auto* stats = find(view, CharacterSectionKind::Stats);
    REQUIRE(stats != nullptr);
    REQUIRE(stats->lines.size() == 2);
    CHECK(stats->lines[0].text == "Health");
    CHECK(stats->lines[0].value == 90);
    CHECK(stats->lines[0].growthPercent == 0);
    CHECK(stats->lines[1].text == "Stamina");
    CHECK_FALSE(stats->lines[1].growthPercent.has_value());
}

TEST_CASE("an out of range stat enum and huge growth do not crash", "[character_screen]")
{
    CharacterScreenInput in;
    in.stats = {{static_cast<CharacterStat>(200), 5, 1}, {CharacterStat::Health, 1e9, 1e-9}};
    const auto view = buildCharacterScreen(in);
    const auto* stats = find(view, CharacterSectionKind::Stats);
    REQUIRE(stats != nullptr);
    REQUIRE(stats->lines.size() == 1);
    CHECK(stats->lines[0].growthPercent == 10000);
}

TEST_CASE("capabilities list unlocked first, then by id, once each", "[character_screen]")
{
    CharacterScreenInput in;
    in.capabilities = {{"c.b", "Read Linear B", false}, {"c.z", "Pick locks", true}, {"c.a", "Offer while moving", true},
        {"c.z", "Duplicate", false}, {"", "No id", true}, {"c.n", "", false}};
    const auto view = buildCharacterScreen(in);
    const auto* caps = find(view, CharacterSectionKind::Capabilities);
    REQUIRE(caps != nullptr);
    REQUIRE(caps->lines.size() == 4);
    CHECK(caps->lines[0].text == "Offer while moving");
    CHECK(caps->lines[1].text == "Pick locks");
    CHECK(caps->lines[2].text == "Read Linear B");
    CHECK_FALSE(caps->lines[2].unlocked);
    CHECK(caps->lines[3].text == "c.n");
}

TEST_CASE("capability and verb lists are capped and report the omitted count", "[character_screen]")
{
    CharacterScreenInput in;
    for (std::size_t i = 0; i < kCharacterScreenMaxCapabilityLines + 5; ++i) {
        in.capabilities.push_back({"c." + std::to_string(1000 + i), "Cap", true});
    }
    for (std::size_t i = 0; i < kCharacterScreenMaxVerbLines + 3; ++i) {
        in.verbs.push_back({"v." + std::to_string(1000 + i), "Verb", true});
    }
    const auto view = buildCharacterScreen(in);
    const auto* caps = find(view, CharacterSectionKind::Capabilities);
    const auto* verbs = find(view, CharacterSectionKind::Verbs);
    REQUIRE(caps != nullptr);
    REQUIRE(verbs != nullptr);
    CHECK(caps->lines.size() == kCharacterScreenMaxCapabilityLines);
    CHECK(caps->omitted == 5);
    CHECK(verbs->lines.size() == kCharacterScreenMaxVerbLines);
    CHECK(verbs->omitted == 3);
}

TEST_CASE("standing states what each tier opens", "[character_screen]")
{
    CharacterScreenInput in;
    CharacterScreenStanding s;
    s.faction = "Salt Guild";
    s.standing = -2000;
    s.tier = "Wary";
    s.unlocksNow = {"Safe passage", "Basic stock", "Basic stock"};
    s.nextTier = "Trusted";
    s.pointsToNext = 40;
    s.unlocksNext = {"Trainer"};
    in.standing = {s, {"Ash Court", 15, "Known", {}, "", {}, 0}};
    const auto view = buildCharacterScreen(in);
    const auto* section = find(view, CharacterSectionKind::Standing);
    REQUIRE(section != nullptr);
    REQUIRE(section->lines.size() == 2);
    CHECK(section->lines[0].text == "Ash Court");
    CHECK(section->lines[0].details == std::vector<std::string>{"Tier: Known"});
    const auto& salt = section->lines[1];
    CHECK(salt.value == -1000);
    REQUIRE(salt.details.size() == 4);
    CHECK(salt.details[0] == "Tier: Wary");
    CHECK(salt.details[1] == "Opens: Basic stock, Safe passage");
    CHECK(salt.details[2] == "Next: Trusted in 40");
    CHECK(salt.details[3] == "Will open: Trainer");
}

TEST_CASE("hostile standing text is cleaned and bounded", "[character_screen]")
{
    CharacterScreenInput in;
    CharacterScreenStanding s;
    s.faction = std::string("  Guild\n\t\x01 of   Salt ") + std::string(200, 'x');
    s.tier = "\n";
    for (int i = 0; i < 100; ++i) {
        s.unlocksNow.push_back("gate" + std::to_string(1000 + i));
    }
    in.standing = {s, {"", 1, "", {}, "", {}, 0}, {"\n\n", 1, "", {}, "", {}, 0}, s};
    const auto view = buildCharacterScreen(in);
    const auto* section = find(view, CharacterSectionKind::Standing);
    REQUIRE(section != nullptr);
    REQUIRE(section->lines.size() == 1);
    const auto& line = section->lines[0];
    CHECK(line.text.size() <= kCharacterScreenMaxTextBytes);
    CHECK(line.text.rfind("Guild of Salt xxx", 0) == 0);
    REQUIRE(line.details.size() == 1);
    CHECK(line.details[0].find("and 92 more") != std::string::npos);
}

TEST_CASE("a cut never splits a UTF-8 sequence", "[character_screen]")
{
    CharacterScreenInput in;
    in.capabilities = {{"c", std::string(kCharacterScreenMaxTextBytes - 1, 'a') + "\xC3\xA9\xC3\xA9", true}};
    const auto view = buildCharacterScreen(in);
    const auto* caps = find(view, CharacterSectionKind::Capabilities);
    REQUIRE(caps != nullptr);
    CHECK(caps->lines[0].text.size() == kCharacterScreenMaxTextBytes - 1);
}

TEST_CASE("the same input builds the same screen", "[character_screen]")
{
    CharacterScreenInput in;
    in.verbs = {{"v.climb", "Climb", true}, {"v.swim", "Swim", false}, {"v.dive", "Dive", true}};
    in.capabilities = {{"c.a", "A", false}, {"c.b", "B", true}};
    in.standing = {{"Zed", 1, "T", {"b", "a"}, "", {}, 0}, {"Abe", 2, "T", {"d", "c"}, "", {}, 0}};
    auto reversed = in;
    std::reverse(reversed.verbs.begin(), reversed.verbs.end());
    std::reverse(reversed.capabilities.begin(), reversed.capabilities.end());
    std::reverse(reversed.standing.begin(), reversed.standing.end());
    for (auto& s : reversed.standing) {
        std::reverse(s.unlocksNow.begin(), s.unlocksNow.end());
    }
    CHECK(buildCharacterScreen(in) == buildCharacterScreen(reversed));
    CHECK(buildCharacterScreen(in).size() == 3);
}
