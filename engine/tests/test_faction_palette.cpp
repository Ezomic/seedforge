#include "ichor/factions/faction_palette.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string readRepoPalette()
{
    std::ifstream file(std::filesystem::path(ICHOR_REPO_DATA) / "ui" / "faction_palette.json", std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

FactionPalette repoPalette()
{
    FactionPalette palette;
    std::vector<std::string> problems;
    REQUIRE(parseFactionPalette("faction_palette.json", readRepoPalette(), palette, problems));
    REQUIRE(problems.empty());
    return palette;
}

std::string paletteText(const std::string& incumbent, const std::string& challenger, const std::string& minDifference = "15")
{
    return R"({"min_difference": )" + minDifference + R"(, "roles": {"incumbent": ")" + incumbent + R"(", "challenger": ")" + challenger
        + R"(", "parasite": "#CC79A7", "custodian": "#56B4E9", "refusenik": "#E69F00", "herald": "#F0E442"}})";
}

bool parses(const std::string& text, std::vector<std::string>& problems)
{
    FactionPalette palette;
    return parseFactionPalette("p.json", text, palette, problems);
}

}

TEST_CASE("the shipped palette keeps every role pair distinguishable under all colour visions", "[faction_palette]")
{
    const FactionPalette palette = repoPalette();
    REQUIRE(checkPaletteDistinguishable(palette).empty());
    for (const ColourVision vision : kColourVisions) {
        INFO(colourVisionName(vision));
        REQUIRE(minPaletteDifference(palette, vision) >= palette.minDifference);
    }
}

TEST_CASE("the check goes red for a pair that is distinct normally but collapses for deuteranopes", "[faction_palette]")
{
    FactionPalette palette = repoPalette();
    palette.roles[static_cast<std::size_t>(FactionRole::Incumbent)] = *parseHexColour("#B4783C");
    palette.roles[static_cast<std::size_t>(FactionRole::Challenger)] = *parseHexColour("#78A03C");

    REQUIRE(colourDifference(palette.colour(FactionRole::Incumbent), palette.colour(FactionRole::Challenger), ColourVision::Normal) > 40.0);
    REQUIRE(colourDifference(palette.colour(FactionRole::Incumbent), palette.colour(FactionRole::Challenger), ColourVision::Deuteranopia) < 5.0);

    const std::vector<std::string> problems = checkPaletteDistinguishable(palette);
    REQUIRE_FALSE(problems.empty());
    REQUIRE(std::any_of(problems.begin(), problems.end(), [](const std::string& p) {
        return p.find("incumbent and challenger") != std::string::npos && p.find("deuteranopia") != std::string::npos;
    }));
    REQUIRE(minPaletteDifference(palette, ColourVision::Deuteranopia) < palette.minDifference);
}

TEST_CASE("identical colours are flagged under every vision", "[faction_palette]")
{
    FactionPalette palette = repoPalette();
    palette.roles[1] = palette.roles[0];
    REQUIRE(checkPaletteDistinguishable(palette).size() == kColourVisions.size());
    REQUIRE(minPaletteDifference(palette, ColourVision::Normal) == 0.0);
}

TEST_CASE("colour vision simulation keeps greys and black and white fixed", "[faction_palette]")
{
    for (const ColourVision vision : kColourVisions) {
        for (const char* hex : {"#000000", "#FFFFFF", "#808080"}) {
            const Rgb8 grey = *parseHexColour(hex);
            const Rgb8 seen = simulateColourVision(grey, vision);
            REQUIRE(std::abs(int(seen.r) - int(grey.r)) <= 2);
            REQUIRE(std::abs(int(seen.g) - int(grey.g)) <= 2);
            REQUIRE(std::abs(int(seen.b) - int(grey.b)) <= 2);
        }
    }
    REQUIRE(simulateColourVision(*parseHexColour("#336699"), ColourVision::Normal) == *parseHexColour("#336699"));
}

TEST_CASE("colour difference is symmetric, zero for equal colours and larger for darker against lighter", "[faction_palette]")
{
    const Rgb8 a = *parseHexColour("#102030");
    const Rgb8 b = *parseHexColour("#F0E0D0");
    for (const ColourVision vision : kColourVisions) {
        REQUIRE(colourDifference(a, a, vision) == 0.0);
        REQUIRE(colourDifference(a, b, vision) == colourDifference(b, a, vision));
        REQUIRE(colourDifference(a, b, vision) > 50.0);
    }
}

TEST_CASE("hex colours parse strictly and round-trip", "[faction_palette]")
{
    REQUIRE(*parseHexColour("#0072b2") == Rgb8{0, 0x72, 0xB2});
    REQUIRE(toHexColour(Rgb8{0, 0x72, 0xB2}) == "#0072B2");
    for (const char* bad : {"", "#", "0072B2", "#0072B", "#0072B2F", "#00G2B2", " #0072B2", "#0072B2\n", "rgb(0,0,0)"}) {
        INFO(bad);
        REQUIRE_FALSE(parseHexColour(bad).has_value());
    }
}

TEST_CASE("palette parsing reports every problem and never throws", "[faction_palette]")
{
    std::vector<std::string> problems;
    REQUIRE(parses(paletteText("#0072B2", "#D55E00"), problems));
    REQUIRE(problems.empty());

    problems.clear();
    REQUIRE_FALSE(parses(paletteText("blue", "#D55E00"), problems));
    REQUIRE(problems.size() == 1);
    REQUIRE(problems[0].find("p.json: roles.incumbent") == 0);

    for (const char* bad : {"", "[]", "null", "{", "{}", R"({"min_difference": 15})", R"({"roles": {}})", "\xff\xfe\x00", R"({"min_difference": "15", "roles": 3})"}) {
        problems.clear();
        INFO(bad);
        REQUIRE_FALSE(parses(bad, problems));
        REQUIRE_FALSE(problems.empty());
    }
}

TEST_CASE("palette parsing rejects unknown keys, unknown roles and bad thresholds", "[faction_palette]")
{
    std::vector<std::string> problems;
    REQUIRE_FALSE(parses(R"({"min_difference": 15, "extra": 1, "roles": {"incumbent": "#000000"}})", problems));
    REQUIRE(std::any_of(problems.begin(), problems.end(), [](const std::string& p) { return p.find("extra: unknown key") != std::string::npos; }));
    REQUIRE(std::any_of(problems.begin(), problems.end(), [](const std::string& p) { return p.find("roles.herald: missing") != std::string::npos; }));

    problems.clear();
    REQUIRE_FALSE(parses(R"({"min_difference": 15, "roles": {"wizard": "#000000"}})", problems));
    REQUIRE(std::any_of(problems.begin(), problems.end(), [](const std::string& p) { return p.find("roles.wizard: unknown role") != std::string::npos; }));

    for (const char* threshold : {"0", "-1", "100.5", "1e999", "-1e999"}) {
        problems.clear();
        INFO(threshold);
        REQUIRE_FALSE(parses(paletteText("#0072B2", "#D55E00", threshold), problems));
    }
}

TEST_CASE("palette parsing survives hostile size and nesting", "[faction_palette]")
{
    std::vector<std::string> problems;
    REQUIRE_FALSE(parses(std::string(200 * 1024, ' '), problems));
    REQUIRE(problems.size() == 1);

    problems.clear();
    REQUIRE_FALSE(parses(std::string(100000, '[') + std::string(100000, ']'), problems));
    REQUIRE(problems.size() == 1);

    problems.clear();
    FactionPalette untouched;
    untouched.minDifference = 7.0;
    REQUIRE_FALSE(parseFactionPalette("p.json", "{}", untouched, problems));
    REQUIRE(untouched.minDifference == 7.0);
}

TEST_CASE("an unreachable threshold flags every pair", "[faction_palette]")
{
    FactionPalette palette = repoPalette();
    palette.minDifference = std::numeric_limits<double>::infinity();
    REQUIRE_FALSE(checkPaletteDistinguishable(palette).empty());
}

TEST_CASE("palette parsing rejects duplicate keys and unbalanced closers", "[faction_palette]")
{
    std::vector<std::string> problems;
    REQUIRE_FALSE(parses(R"({"min_difference": 15, "roles": {"incumbent": "#000000", "incumbent": "#FFFFFF", "challenger": "#D55E00", "parasite": "#CC79A7", "custodian": "#56B4E9", "refusenik": "#E69F00", "herald": "#F0E442"}})", problems));
    REQUIRE(problems.size() == 1);
    REQUIRE(problems[0].find("duplicate key \"incumbent\"") != std::string::npos);

    problems.clear();
    REQUIRE_FALSE(parses(std::string(100000, ']') + std::string(100000, '['), problems));
    REQUIRE(problems.size() == 1);
}
