#include "ichor/ui/journal_screen_model.h"

#include <catch2/catch_test_macros.hpp>

using namespace ichor;

namespace {

JournalModelEntry entry(std::uint64_t tick, std::string category, std::string text, std::uint8_t weight = 0)
{
    return {tick, "t" + std::to_string(tick), std::move(category), std::move(text), weight};
}

}

TEST_CASE("an empty journal has two empty tabs", "[journal_model]")
{
    const auto view = buildJournalModel({});
    REQUIRE(view.size() == 2);
    CHECK(view[0].tab == JournalTab::Prose);
    CHECK(view[1].tab == JournalTab::Consequences);
    CHECK(view[0].groups.empty());
    CHECK(view[1].groups.empty());
}

TEST_CASE("prose is the journal category, newest first", "[journal_model]")
{
    const auto view = buildJournalModel({entry(1, "journal", "first"), entry(5, "war", "not prose"), entry(3, "journal", "second")});
    REQUIRE(view[0].groups.size() == 1);
    const auto& lines = view[0].groups[0].lines;
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].text == "second");
    CHECK(lines[1].text == "first");
    CHECK(lines[0].time == "t3");
}

TEST_CASE("consequences group by category, newest group first", "[journal_model]")
{
    const auto view = buildJournalModel({entry(1, "trade-route", "a"), entry(9, "war", "b"), entry(4, "trade-route", "c"),
        entry(2, "journal", "prose only")});
    const auto& groups = view[1].groups;
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].title == "War");
    CHECK(groups[1].title == "Trade route");
    REQUIRE(groups[1].lines.size() == 2);
    CHECK(groups[1].lines[0].text == "c");
    CHECK(groups[1].lines[1].text == "a");
}

TEST_CASE("equal ticks keep the later input first and groups tie on title", "[journal_model]")
{
    const auto view = buildJournalModel({entry(2, "b", "x"), entry(2, "a", "y"), entry(2, "a", "z")});
    const auto& groups = view[1].groups;
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].title == "A");
    CHECK(groups[0].lines[0].text == "z");
    CHECK(groups[1].title == "B");
}

TEST_CASE("removing the category split would put consequences in prose", "[journal_model]")
{
    const auto view = buildJournalModel({entry(1, "journal", "p"), entry(2, "famine", "c")});
    CHECK(view[0].groups[0].lines.size() == 1);
    CHECK(view[1].groups.size() == 1);
    CHECK(view[1].groups[0].lines[0].text == "c");
}

TEST_CASE("hostile text is cleaned, cut and dropped when empty", "[journal_model]")
{
    const auto view = buildJournalModel({entry(1, "journal", "  \t\n "), entry(2, "journal", "a\tb\nc\x01 d"),
        entry(3, "journal", std::string(2000, 'y')), entry(4, "journal", "\xC3\xA9" + std::string(499, 'z'))});
    const auto& lines = view[0].groups[0].lines;
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].text.size() == kJournalModelMaxTextBytes);
    CHECK(lines[1].text.size() == kJournalModelMaxTextBytes);
    CHECK(lines[2].text == "a b c d");
}

TEST_CASE("a cut never splits a UTF-8 sequence", "[journal_model]")
{
    std::string text(kJournalModelMaxTextBytes - 1, 'a');
    text += "\xC3\xA9tail";
    const auto view = buildJournalModel({entry(1, "journal", text)});
    const std::string& out = view[0].groups[0].lines[0].text;
    CHECK(out.size() == kJournalModelMaxTextBytes - 1);
}

TEST_CASE("caps bound the lines and report what was left out", "[journal_model]")
{
    std::vector<JournalModelEntry> in;
    for (std::uint64_t i = 0; i < 100; ++i) {
        in.push_back(entry(i, "war", "w" + std::to_string(i)));
    }
    for (std::uint64_t i = 0; i < 40; ++i) {
        in.push_back(entry(i, "cat" + std::to_string(i), "c"));
    }
    const auto view = buildJournalModel(in);
    REQUIRE(view[1].groups.size() == kJournalModelMaxGroups);
    CHECK(view[1].omittedGroups == 41 - kJournalModelMaxGroups);
    const JournalModelGroup* war = nullptr;
    for (const auto& g : view[1].groups) {
        if (g.title == "War") {
            war = &g;
        }
    }
    REQUIRE(war != nullptr);
    CHECK(war->lines.size() == kJournalModelMaxGroupLines);
    CHECK(war->omitted == 100 - kJournalModelMaxGroupLines);
    CHECK(war->lines[0].text == "w99");
}

TEST_CASE("prose is capped", "[journal_model]")
{
    std::vector<JournalModelEntry> in;
    for (std::uint64_t i = 0; i < kJournalModelMaxProseLines + 5; ++i) {
        in.push_back(entry(i, "journal", "p"));
    }
    const auto view = buildJournalModel(in);
    CHECK(view[0].groups[0].lines.size() == kJournalModelMaxProseLines);
    CHECK(view[0].groups[0].omitted == 5);
}

TEST_CASE("a category with no usable name falls back to a plain title", "[journal_model]")
{
    const auto view = buildJournalModel({entry(1, "", "orphan"), entry(2, "__", "also")});
    REQUIRE(view[1].groups.size() == 1);
    CHECK(view[1].groups[0].title == "Other");
    CHECK(view[1].groups[0].lines.size() == 2);
}

TEST_CASE("the same input always gives the same view", "[journal_model]")
{
    const std::vector<JournalModelEntry> in = {entry(3, "a", "x", 200), entry(1, "journal", "y"), entry(3, "b", "z")};
    CHECK(buildJournalModel(in) == buildJournalModel(in));
}
