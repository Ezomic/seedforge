#include "ichor/progression/power_growth.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string stat(const std::string& id, const std::string& start, const std::string& end, const std::string& weight)
{
    return R"({"id": ")" + id + R"(", "start": )" + start + R"(, "end": )" + end + R"(, "weight_permille": )" + weight + "}";
}

std::string table(const std::string& health = "100", const std::string& healthEnd = "180")
{
    return "[" + stat("health", health, healthEnd, "300") + "," + stat("stamina", "100", "170", "250") + "," + stat("carry", "40", "70", "200") + "," + stat("attunement", "10", "17", "250") + "]";
}

std::vector<std::string> problemsOf(const std::string& text, std::vector<SpineStat>* out = nullptr)
{
    std::vector<SpineStat> local;
    std::vector<std::string> problems;
    const bool ok = parsePowerGrowth("p.json", text, out != nullptr ? *out : local, problems);
    CHECK(ok == problems.empty());
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

std::vector<SpineStat> parsed(const std::string& text)
{
    std::vector<SpineStat> out;
    REQUIRE(problemsOf(text, &out).empty());
    return out;
}

}

TEST_CASE("the authored power growth table passes and sits under the cap")
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "progression" / "power_growth.json", std::ios::binary);
    REQUIRE(file.good());
    const std::string text(std::istreambuf_iterator<char>(file), {});
    std::vector<SpineStat> stats;
    CHECK(problemsOf(text, &stats).empty());
    REQUIRE(stats.size() == 4);
    CHECK(stats[0].id == "attunement");
    CHECK(totalGrowthPermille(stats) <= kPowerCapPermille);
    CHECK(totalGrowthPermille(stats) >= kSpineMinGrowthPermille);
}

TEST_CASE("growth is integer permille, rounded down")
{
    CHECK(statGrowthPermille({"health", 100, 180, 0}) == 1800);
    CHECK(statGrowthPermille({"attunement", 3, 7, 0}) == 2333);
    CHECK(statGrowthPermille({"health", 0, 5, 0}) == 0);
    const auto stats = parsed(table());
    CHECK(totalGrowthPermille(stats) == (300 * 1800 + 250 * 1700 + 200 * 1750 + 250 * 1700) / 1000);
}

TEST_CASE("exactly 2x passes and 2x plus a step fails")
{
    CHECK(problemsOf(table("100", "200")).empty());
    const auto over = problemsOf(table("100", "201"));
    CHECK(mentions(over, "[0]"));
    CHECK(mentions(over, "2010 permille"));
}

TEST_CASE("a stat growing less than 50 percent fails")
{
    CHECK(mentions(problemsOf(table("100", "149")), "1490 permille"));
    CHECK(problemsOf(table("100", "150")).empty());
}

TEST_CASE("the total cap fires on its own when every stat is inside the band")
{
    // Every stat is at exactly 2x, so the band passes; only the cap sees the next step.
    std::vector<SpineStat> stats = parsed(table("100", "200"));
    for (SpineStat& s : stats) {
        s.start = 100;
        s.end = 200;
    }
    std::vector<std::string> problems;
    checkPowerGrowthCap("p", stats, problems);
    CHECK(problems.empty());
    CHECK(totalGrowthPermille(stats) == kPowerCapPermille);

    stats[0].weightPermille = 0;
    stats[1].weightPermille = 0;
    stats[2].weightPermille = 0;
    stats[3].weightPermille = 1000;
    stats[3].end = 201;
    problems.clear();
    checkPowerGrowthCap("p", stats, problems);
    CHECK(mentions(problems, "[3]"));
    CHECK(mentions(problems, "over the cap"));
}

TEST_CASE("weights must sum to 1000")
{
    std::string text = table();
    text.replace(text.find("300"), 3, "301");
    CHECK(mentions(problemsOf(text), "weights sum to 1001"));
    std::vector<SpineStat> stats = parsed(table());
    stats[0].weightPermille = 0;
    CHECK(totalGrowthPermille(stats) == 0);
}

TEST_CASE("the table must name exactly the four spine stats")
{
    CHECK(mentions(problemsOf("[]"), "missing spine stat \"health\""));
    CHECK(mentions(problemsOf("[" + stat("luck", "1", "2", "1000") + "]"), "not a spine stat"));
    std::string text = table();
    text.replace(text.find("stamina"), 7, "health");
    CHECK(mentions(problemsOf(text), "duplicate of \"health\""));
    const std::string t = table();
    CHECK(mentions(problemsOf(t.substr(0, t.size() - 1) + "," + stat("carry", "1", "2", "0") + "]"), "more than 4"));
}

TEST_CASE("bad data is reported, never thrown")
{
    CHECK(mentions(problemsOf(""), "empty file"));
    CHECK(mentions(problemsOf("{"), "not valid JSON"));
    CHECK(mentions(problemsOf("{}"), "expected an array"));
    CHECK(mentions(problemsOf("[1]"), "expected an object"));
    CHECK(mentions(problemsOf(std::string(200, '[')), "nested deeper"));
    CHECK(mentions(problemsOf(std::string(70 * 1024, ' ')), "larger than"));
    CHECK(mentions(problemsOf(table("0", "5")), "must be above zero"));
    CHECK(mentions(problemsOf(table("100", "50")), "below start"));
    CHECK(mentions(problemsOf(table("1.5", "3")), "whole number"));
    CHECK(mentions(problemsOf(table("\"100\"", "180")), "whole number"));
    CHECK(mentions(problemsOf(table("-5", "180")), "outside 0 to"));
    const auto huge = problemsOf(table("100", "1e999"));
    CHECK((mentions(huge, "whole number") || mentions(huge, "not valid JSON")));
    CHECK(mentions(problemsOf(R"([{"id": 5}])"), "expected a string"));
    CHECK(mentions(problemsOf(R"([{"id": "health", "start": 1, "end": 2}])"), "weight_permille: missing"));
    CHECK(mentions(problemsOf(table("100", "18446744073709551615")), "outside 0 to"));
    CHECK(mentions(problemsOf(table("100", "9223372036854775807")), "outside 0 to"));
    CHECK(mentions(problemsOf(R"([{"id": "health", "bogus": 1}])"), "unknown key"));
}

TEST_CASE("the result is sorted by id whatever the order in the file")
{
    const std::string a = stat("health", "100", "180", "300") + "," + stat("stamina", "100", "170", "250") + "," + stat("carry", "40", "70", "200") + "," + stat("attunement", "10", "17", "250");
    const std::string b = stat("attunement", "10", "17", "250") + "," + stat("carry", "40", "70", "200") + "," + stat("stamina", "100", "170", "250") + "," + stat("health", "100", "180", "300");
    const auto first = parsed("[" + a + "]");
    const auto second = parsed("[" + b + "]");
    REQUIRE(first.size() == 4);
    REQUIRE(second.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(first[i].id == second[i].id);
        CHECK(first[i].end == second[i].end);
    }
    CHECK(first[3].id == "stamina");
}

TEST_CASE("a negative weight cannot hide behind a balanced sum")
{
    std::vector<SpineStat> stats = parsed(table());
    stats[0].weightPermille = 1500;
    stats[1].weightPermille = -500;
    stats[2].weightPermille = 0;
    stats[3].weightPermille = 0;
    std::vector<std::string> problems;
    checkPowerGrowthCap("p", stats, problems);
    CHECK(mentions(problems, "weight outside"));
}

TEST_CASE("hand built extreme values do not overflow")
{
    std::vector<SpineStat> stats{{"health", 1, std::int64_t{1} << 62, 1000}};
    std::vector<std::string> problems;
    checkPowerGrowthCap("p", stats, problems);
    CHECK(!problems.empty());
    CHECK(statGrowthPermille(stats[0]) == 0);
    stats[0].weightPermille = std::int64_t{1} << 62;
    CHECK(totalGrowthPermille(stats) == 0);
    problems.clear();
    checkPowerGrowthCap("p", stats, problems);
    CHECK(!problems.empty());
}
