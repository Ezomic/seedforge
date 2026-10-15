#include "ichor/save/chronicle_export.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace ichor;

namespace {

std::size_t countOf(const std::string& text, char c)
{
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), c));
}

}

TEST_CASE("an empty chronicle exports a header and says nothing happened", "[chronicle_export]")
{
    const std::string text = exportChronicleText(Chronicle{}, 1.0);
    REQUIRE(text == "Chronicle\n0 entries\n\nNothing has happened yet.\n");
}

TEST_CASE("entries come out in order with play time, category and text", "[chronicle_export]")
{
    Chronicle story;
    story.add(0, "start", "The caravan set out.");
    story.add(3725, "death", "Maren fell at the ford.", ChronicleTags{{4, 9}, 7, 200});
    const std::string text = exportChronicleText(story, 1.0);
    REQUIRE(text == "Chronicle\n2 entries\n\n"
                    "[0:00:00] start: The caravan set out.\n"
                    "[1:02:05] death: Maren fell at the ford. (weight 200)\n");
}

TEST_CASE("one entry reads as one entry", "[chronicle_export]")
{
    Chronicle story;
    story.add(1, "a", "x");
    REQUIRE(exportChronicleText(story, 1.0).starts_with("Chronicle\n1 entry\n"));
}

TEST_CASE("the export is byte identical on repeat and from a parsed copy", "[chronicle_export]")
{
    Chronicle story;
    for (int i = 0; i < 50; ++i) {
        story.add(static_cast<std::uint64_t>(i) * 97, i % 2 ? "trade" : "war", "event \xC3\xA9 " + std::to_string(i), ChronicleTags{{1, 2}, 3, static_cast<std::uint8_t>(i)});
    }
    const std::string first = exportChronicleText(story, 0.5);
    REQUIRE(exportChronicleText(story, 0.5) == first);
    Chronicle back;
    REQUIRE(Chronicle::parse(story.serialize(), back));
    REQUIRE(exportChronicleText(back, 0.5) == first);
}

TEST_CASE("hostile text cannot break the lines or smuggle control characters", "[chronicle_export]")
{
    Chronicle story;
    // add() already flattens tabs and line breaks; the rest must be handled by the exporter.
    story.add(5, "note", std::string("esc\x1b[31mred\x1b[0m bell\x07 nul") + '\0' + "end del\x7f next\x0b\x0c");
    story.add(6, "note", "line\none\r\ntwo\tthree");
    story.add(7, "note", "c1 \xC2\x85 \xC2\x9B ls \xE2\x80\xA8 ps \xE2\x80\xA9 rlo \xE2\x80\xAE x \xE2\x81\xA6 y");
    const std::string text = exportChronicleText(story, 1.0);
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        REQUIRE((u == '\n' || u >= 0x20));
        REQUIRE(u != 0x7F);
    }
    REQUIRE(countOf(text, '\n') == 3 + 3);
    REQUIRE(text.find("\xC2\x85") == std::string::npos);
    REQUIRE(text.find("\xE2\x80\xAE") == std::string::npos);
    REQUIRE(text.find("\xE2\x80\xA8") == std::string::npos);
    REQUIRE(text.find("red") != std::string::npos);
}

TEST_CASE("invalid UTF-8 becomes replacement characters and the output is valid", "[chronicle_export]")
{
    const std::string bad[] = {
        "lone \x80 continuation",
        "truncated \xE2\x82",
        "overlong \xC0\xAF slash",
        "surrogate \xED\xA0\x80 half",
        "too big \xF4\x90\x80\x80 point",
        "bad lead \xFF\xFE bytes",
        "four byte \xF0\x9F\x98\x80 ok",
    };
    Chronicle story;
    for (const std::string& s : bad) {
        story.add(1, "note", s);
    }
    const std::string text = exportChronicleText(story, 1.0);
    REQUIRE(isValidUtf8(text));
    REQUIRE(text.find("\xEF\xBF\xBD") != std::string::npos);
    REQUIRE(text.find("\xF0\x9F\x98\x80") != std::string::npos);
    REQUIRE(text.find("lone \xEF\xBF\xBD continuation") != std::string::npos);
    REQUIRE(text.find("overlong \xEF\xBF\xBD\xEF\xBF\xBD slash") != std::string::npos);
    REQUIRE(text.find("surrogate \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD half") != std::string::npos);
}

TEST_CASE("a cut multi-byte character in the stored text still exports valid UTF-8", "[chronicle_export]")
{
    Chronicle story;
    story.add(1, "note", std::string(Chronicle::kMaxTextBytes - 1, 'a') + "\xE2\x82\xAC");
    REQUIRE(isValidUtf8(exportChronicleText(story, 1.0)));
}

TEST_CASE("a very long chronicle exports every entry in bounded size", "[chronicle_export]")
{
    Chronicle story;
    const std::string longText(Chronicle::kMaxTextBytes + 100, 'z');
    std::size_t added = 0;
    while (story.add(added, "log", longText)) {
        ++added;
    }
    REQUIRE(added == Chronicle::kMaxEntries);
    const std::string text = exportChronicleText(story, 1.0);
    REQUIRE(countOf(text, '\n') == Chronicle::kMaxEntries + 3);
    REQUIRE(text.size() < Chronicle::kMaxEntries * (Chronicle::kMaxTextBytes + 64));
    REQUIRE(text.starts_with("Chronicle\n100000 entries\n"));
}

TEST_CASE("a bad seconds-per-tick or huge tick never produces undefined output", "[chronicle_export]")
{
    Chronicle story;
    story.add(~std::uint64_t{0}, "late", "end of time");
    story.add(10, "early", "soon");
    const double rates[] = {std::nan(""), -1.0, 0.0, HUGE_VAL, 1e300};
    for (const double rate : rates) {
        const std::string text = exportChronicleText(story, rate);
        REQUIRE(text.find("late: end of time") != std::string::npos);
        REQUIRE(isValidUtf8(text));
    }
    REQUIRE(exportChronicleText(story, std::nan("")).find("[0:00:00] early") != std::string::npos);
    REQUIRE(exportChronicleText(story, 1e300).find("[1000000000:00:00] late") != std::string::npos);
    Chronicle hidden;
    hidden.add(1, "note", "a\xE2\x80\x8B" "b\xEF\xBB\xBF" "c\xF3\xA0\x80\x81" "d");
    REQUIRE(exportChronicleText(hidden, 1.0).find("a b c d") != std::string::npos);
}

TEST_CASE("the sanitiser matters: raw control bytes survive Chronicle::exportText", "[chronicle_export]")
{
    Chronicle story;
    story.add(1, "note", "a\x1b[2Jb");
    REQUIRE(story.exportText(1.0).find('\x1b') != std::string::npos);
    REQUIRE(exportChronicleText(story, 1.0).find('\x1b') == std::string::npos);
}
