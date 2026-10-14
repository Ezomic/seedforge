#include "ichor/npc/dialogue_select.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

DialogueLine line(const std::string& id, std::int64_t weight = 1, std::vector<std::string> conditions = {}, std::uint64_t cooldown = 0)
{
    DialogueLine l;
    l.id = id;
    l.text = "text of " + id;
    l.weight = weight;
    l.cooldown = cooldown;
    for (const auto& c : conditions) {
        DialogueCondition parsed;
        std::string error;
        REQUIRE(parseCondition(c, parsed, error));
        l.conditions.push_back(parsed);
    }
    return l;
}

DialoguePool poolOf(std::vector<DialogueLine> lines)
{
    std::string error;
    auto pool = DialoguePool::fromLines(std::move(lines), error);
    INFO(error);
    REQUIRE(pool.has_value());
    return *pool;
}

DialogueContext ctx(std::uint64_t npc, std::vector<std::pair<std::string, std::string>> tags = {})
{
    DialogueContext c;
    c.npcId = npc;
    c.tags = std::move(tags);
    return c;
}

DialogueHistory atCounter(std::uint64_t counter)
{
    DialogueHistory h;
    h.counter = counter;
    return h;
}

std::filesystem::path writeTemp(const std::string& name, const std::string& body)
{
    const auto dir = std::filesystem::temp_directory_path() / ("ichor_dialogue_" + std::to_string(ICHOR_PID) + "_" + name);
    std::filesystem::create_directories(dir);
    const auto file = dir / "lines.json";
    std::ofstream(file, std::ios::binary) << body;
    return file;
}

}

TEST_CASE("an empty pool selects nothing", "[dialogue]")
{
    const auto pool = poolOf({});
    CHECK(selectLine(pool, ctx(1), DialogueHistory{}, 7) == nullptr);
}

TEST_CASE("conditions gate lines, and a missing tag fails a plain condition", "[dialogue]")
{
    const auto pool = poolOf({line("bitter", 1, {"trait=bitter"}), line("any_work", 1, {"schedule"}), line("not_sleep", 1, {"schedule!=sleep"}), line("no_debt", 1, {"!debt"})});
    const auto reachable = [&](const DialogueContext& c) {
        std::vector<std::string> ids;
        for (std::uint64_t counter = 0; counter < 200; ++counter) {
            if (const auto* l = selectLine(pool, c, atCounter(counter), 5)) {
                ids.push_back(l->id);
            }
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    };
    CHECK(reachable(ctx(1)) == std::vector<std::string>{"no_debt", "not_sleep"});
    CHECK(reachable(ctx(1, {{"trait", "bitter"}, {"schedule", "sleep"}, {"debt", "yes"}})) == std::vector<std::string>{"any_work", "bitter"});
    CHECK(reachable(ctx(1, {{"trait", "kind"}, {"trait", "bitter"}, {"schedule", "work"}})) == std::vector<std::string>{"any_work", "bitter", "no_debt", "not_sleep"});
}

TEST_CASE("selection is deterministic and independent of pool order and tag order", "[dialogue]")
{
    std::vector<DialogueLine> lines;
    for (int i = 0; i < 12; ++i) {
        lines.push_back(line("l" + std::to_string(i), 1 + i));
    }
    const auto forward = poolOf(lines);
    std::reverse(lines.begin(), lines.end());
    const auto backward = poolOf(lines);
    for (std::uint64_t counter = 0; counter < 300; ++counter) {
        const auto h = atCounter(counter);
        const auto* a = selectLine(forward, ctx(9, {{"a", "1"}, {"b", "2"}}), h, 42);
        const auto* b = selectLine(backward, ctx(9, {{"b", "2"}, {"a", "1"}}), h, 42);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->id == b->id);
        CHECK(selectLine(forward, ctx(9, {{"a", "1"}, {"b", "2"}}), h, 42)->id == a->id);
    }
}

TEST_CASE("different inputs change the pick sequence", "[dialogue]")
{
    std::vector<DialogueLine> lines;
    for (int i = 0; i < 12; ++i) {
        lines.push_back(line("l" + std::to_string(i)));
    }
    const auto pool = poolOf(lines);
    const auto sequence = [&](std::uint64_t npc, std::uint64_t seed) {
        std::string s;
        for (std::uint64_t counter = 0; counter < 40; ++counter) {
            s += selectLine(pool, ctx(npc), atCounter(counter), seed)->id + ",";
        }
        return s;
    };
    CHECK(sequence(1, 1) != sequence(2, 1));
    CHECK(sequence(1, 1) != sequence(1, 2));
}

TEST_CASE("weights are respected within bounds", "[dialogue]")
{
    const auto pool = poolOf({line("a", 1), line("b", 3), line("c", 6), line("never", 0)});
    std::map<std::string, int> hits;
    constexpr int kDraws = 20000;
    for (int i = 0; i < kDraws; ++i) {
        ++hits[selectLine(pool, ctx(static_cast<std::uint64_t>(i % 50)), atCounter(static_cast<std::uint64_t>(i)), 3)->id];
    }
    // Bound: each share within 2 percentage points of weight / total (more than 4 sigma at this count).
    CHECK(hits.count("never") == 0);
    CHECK(std::abs(hits["a"] / double(kDraws) - 0.1) < 0.02);
    CHECK(std::abs(hits["b"] / double(kDraws) - 0.3) < 0.02);
    CHECK(std::abs(hits["c"] / double(kDraws) - 0.6) < 0.02);
}

TEST_CASE("a line with a thousand times the weight dominates but does not starve the rest", "[dialogue]")
{
    const auto pool = poolOf({line("big", kMaxDialogueWeight), line("small", 1000)});
    int small = 0;
    for (int i = 0; i < 20000; ++i) {
        small += selectLine(pool, ctx(1), atCounter(static_cast<std::uint64_t>(i)), 1)->id == "small" ? 1 : 0;
    }
    CHECK(small > 0);
    CHECK(small < 100);
}

TEST_CASE("many lines at the maximum weight neither overflow nor skew", "[dialogue]")
{
    std::vector<DialogueLine> lines;
    for (int i = 0; i < 64; ++i) {
        lines.push_back(line("h" + std::to_string(i), kMaxDialogueWeight));
    }
    const auto pool = poolOf(lines);
    std::map<std::string, int> hits;
    for (int i = 0; i < 6400; ++i) {
        ++hits[selectLine(pool, ctx(1), atCounter(static_cast<std::uint64_t>(i)), 1)->id];
    }
    CHECK(hits.size() == 64);
}

TEST_CASE("weights outside the range, empty ids and duplicate ids are rejected", "[dialogue]")
{
    std::string error;
    CHECK_FALSE(DialoguePool::fromLines({line("a", -1)}, error).has_value());
    CHECK_FALSE(DialoguePool::fromLines({line("a", kMaxDialogueWeight + 1)}, error).has_value());
    CHECK_FALSE(DialoguePool::fromLines({line("a", std::numeric_limits<std::int64_t>::max())}, error).has_value());
    CHECK_FALSE(DialoguePool::fromLines({line("")}, error).has_value());
    CHECK_FALSE(DialoguePool::fromLines({line("a"), line("b"), line("a")}, error).has_value());
    CHECK(error.find("duplicate") != std::string::npos);
}

TEST_CASE("all zero weights select nothing", "[dialogue]")
{
    CHECK(selectLine(poolOf({line("a", 0), line("b", 0)}), ctx(1), DialogueHistory{}, 1) == nullptr);
}

TEST_CASE("cooldowns hold across a long run and lift when due", "[dialogue]")
{
    const auto pool = poolOf({line("a", 5, {}, 10), line("b", 5, {}, 3), line("c", 1)});
    DialogueHistory h;
    std::map<std::string, std::uint64_t> last;
    const std::map<std::string, std::uint64_t> cooldown = {{"a", 10}, {"b", 3}, {"c", 0}};
    int spokenA = 0;
    int spokenB = 0;
    for (int step = 0; step < 2000; ++step) {
        h.now = static_cast<std::uint64_t>(step);
        const auto* l = selectLine(pool, ctx(4), h, 11);
        REQUIRE(l != nullptr);
        if (last.count(l->id) != 0) {
            CHECK(h.now - last[l->id] >= cooldown.at(l->id));
        }
        spokenA += l->id == "a" ? 1 : 0;
        spokenB += l->id == "b" ? 1 : 0;
        last[l->id] = h.now;
        h.record(l->id);
    }
    CHECK(spokenA > 50);
    CHECK(spokenB > 50);
}

TEST_CASE("when every line is in cooldown nothing is selected", "[dialogue]")
{
    const auto pool = poolOf({line("a", 1, {}, 100), line("b", 1, {}, 100)});
    DialogueHistory h;
    h.now = 5;
    h.record("a");
    h.record("b");
    h.now = 50;
    CHECK(selectLine(pool, ctx(1), h, 1) == nullptr);
    h.now = 105;
    CHECK(selectLine(pool, ctx(1), h, 1) != nullptr);
}

TEST_CASE("a clock that ran backwards keeps lines in cooldown", "[dialogue]")
{
    const auto pool = poolOf({line("a", 1, {}, 5)});
    DialogueHistory h;
    h.now = 100;
    h.record("a");
    h.now = 10;
    CHECK(selectLine(pool, ctx(1), h, 1) == nullptr);
}

TEST_CASE("the line just spoken is not repeated unless it is the only one", "[dialogue]")
{
    const auto pool = poolOf({line("a"), line("b"), line("c")});
    DialogueHistory h;
    for (int i = 0; i < 500; ++i) {
        const auto* l = selectLine(pool, ctx(2), h, 8);
        REQUIRE(l != nullptr);
        CHECK(l->id != h.lastId);
        h.record(l->id);
    }
    const auto lone = poolOf({line("only")});
    DialogueHistory g;
    g.record("only");
    const auto* l = selectLine(lone, ctx(2), g, 8);
    REQUIRE(l != nullptr);
    CHECK(l->id == "only");
}

TEST_CASE("seed 0 and the maximum seed select", "[dialogue]")
{
    const auto pool = poolOf({line("a"), line("b")});
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    CHECK(selectLine(pool, ctx(0), DialogueHistory{}, 0) != nullptr);
    CHECK(selectLine(pool, ctx(kMax), atCounter(kMax), kMax) != nullptr);
}

TEST_CASE("a golden pick sequence stays put", "[dialogue]")
{
    const auto pool = poolOf({line("a", 1), line("b", 2), line("c", 3), line("d", 4)});
    std::string s;
    for (std::uint64_t counter = 0; counter < 12; ++counter) {
        s += selectLine(pool, ctx(77), atCounter(counter), 2024)->id;
    }
    CHECK(s == "ccdacbddcbdd");
}

TEST_CASE("conditions are validated", "[dialogue]")
{
    DialogueCondition c;
    std::string error;
    CHECK(parseCondition("trait=bitter", c, error));
    CHECK(parseCondition("!debt", c, error));
    CHECK(parseCondition("schedule!=sleep", c, error));
    CHECK_FALSE(parseCondition("", c, error));
    CHECK_FALSE(parseCondition("=x", c, error));
    CHECK_FALSE(parseCondition("a=", c, error));
    CHECK_FALSE(parseCondition("!a=b", c, error));
    CHECK_FALSE(parseCondition("A=b", c, error));
    CHECK_FALSE(parseCondition("a b", c, error));
}

TEST_CASE("a pool loads from JSON and reports every problem with its path", "[dialogue]")
{
    const auto good = writeTemp("good", R"([
        {"id": "greet_bitter", "text": "Back again.", "weight": 3, "conditions": ["trait=bitter"], "cooldown": 20},
        {"id": "greet_plain", "text": "Morning.", "weight": 1}
    ])");
    std::vector<std::string> problems;
    const auto pool = DialoguePool::load(good, problems);
    REQUIRE(pool.has_value());
    CHECK(problems.empty());
    REQUIRE(pool->lines().size() == 2);
    CHECK(pool->lines()[0].id == "greet_bitter");
    CHECK(pool->lines()[0].cooldown == 20);

    const auto bad = writeTemp("bad",
        "[\n"
        "{\"id\": \"a\", \"text\": \"Fine.\", \"weight\": 1},\n"
        "{\"id\": \"a\", \"text\": \"Dup.\", \"weight\": 1},\n"
        "{\"id\": \"b\", \"text\": \"Dash \xE2\x80\x94 here\", \"weight\": 1},\n"
        "{\"id\": \"c\", \"text\": \"Heavy\", \"weight\": 99999999999},\n"
        "{\"id\": \"d\", \"text\": \"Neg\", \"weight\": -2},\n"
        "{\"id\": \"e\", \"text\": \"Frac\", \"weight\": 1.5},\n"
        "{\"id\": \"f\", \"text\": \"Cond\", \"weight\": 1, \"conditions\": [\"=bad\", 5]},\n"
        "{\"id\": \"g\", \"weight\": 1},\n"
        "{\"id\": \"h\", \"text\": \"Cool\", \"weight\": 1, \"cooldown\": -1}\n"
        "]");
    problems.clear();
    CHECK_FALSE(DialoguePool::load(bad, problems).has_value());
    const auto mentions = [&](const std::string& needle) {
        return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
    };
    CHECK(mentions("[1].id"));
    CHECK(mentions("[2].text"));
    CHECK(mentions("[3].weight"));
    CHECK(mentions("[4].weight"));
    CHECK(mentions("[5].weight"));
    CHECK(mentions("[6].conditions[0]"));
    CHECK(mentions("[6].conditions[1]"));
    CHECK(mentions("[7].text"));
    CHECK(mentions("[8].cooldown"));
    CHECK(mentions("lines.json"));

    problems.clear();
    CHECK_FALSE(DialoguePool::load(std::filesystem::temp_directory_path() / "ichor_dialogue_missing_file.json", problems).has_value());
    CHECK_FALSE(problems.empty());
    problems.clear();
    CHECK_FALSE(DialoguePool::load(writeTemp("junk", "{not json"), problems).has_value());
    CHECK_FALSE(problems.empty());
    problems.clear();
    CHECK_FALSE(DialoguePool::load(writeTemp("obj", "{}"), problems).has_value());
    CHECK_FALSE(problems.empty());
}
