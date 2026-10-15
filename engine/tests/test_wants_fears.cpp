#include "ichor/data/wants_fears.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string record(const std::string& id, const std::string& kind, int intensity, const std::string& tag, const std::string& op, const std::string& value)
{
    return "{\"id\":\"" + id + "\",\"kind\":\"" + kind + "\",\"subject\":\"grain\",\"intensity\":" + std::to_string(intensity) + ",\"tag\":\"" + tag + "\",\"op\":\"" + op + "\",\"value\":" + value + "}";
}

std::vector<Desire> parseOk(const std::string& text)
{
    std::vector<Desire> out;
    std::vector<std::string> problems;
    const bool ok = parseDesires("t.json", text, out, problems);
    for (const auto& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(ok);
    return out;
}

std::vector<std::string> problemsOf(const std::string& text)
{
    std::vector<Desire> out;
    std::vector<std::string> problems;
    REQUIRE_FALSE(parseDesires("t.json", text, out, problems));
    REQUIRE(out.empty());
    REQUIRE_FALSE(problems.empty());
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    for (const auto& p : problems) {
        if (p.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ids(const std::vector<Desire>& list)
{
    std::vector<std::string> out;
    for (const auto& d : list) {
        out.push_back(d.id);
    }
    return out;
}

}

TEST_CASE("desires parse and round trip their fields", "[npc][desires]")
{
    const auto list = parseOk("[" + record("fear_flood", "fear", 7, "sea_level", ">", "3") + "]");
    REQUIRE(list.size() == 1);
    CHECK(list[0].id == "fear_flood");
    CHECK(list[0].kind == DesireKind::Fear);
    CHECK(list[0].subject == "grain");
    CHECK(list[0].intensity == 7);
    CHECK(list[0].condition.tag == "sea_level");
    CHECK(list[0].condition.op == CompareOp::Gt);
    CHECK(list[0].condition.value == 3);
}

TEST_CASE("desire validation reports every problem with a path", "[npc][desires]")
{
    CHECK(mentions(problemsOf("[" + record("a", "fear", 0, "t", ">", "1") + "]"), "t.json: [0].intensity"));
    CHECK(mentions(problemsOf("[" + record("a", "fear", 11, "t", ">", "1") + "]"), "[0].intensity"));
    CHECK(mentions(problemsOf("[" + record("a", "fear", 5, "t", "!=", "1") + "]"), "[0].op"));
    CHECK(mentions(problemsOf("[" + record("", "fear", 5, "t", ">", "1") + "]"), "[0].id"));
    CHECK(mentions(problemsOf("[" + record("a", "hope", 5, "t", ">", "1") + "]"), "[0].kind"));
    CHECK(mentions(problemsOf("[" + record("a", "want", 5, "", ">", "1") + "]"), "[0].tag"));
    CHECK(mentions(problemsOf("[" + record("a", "want", 5, "t", ">", "1.5") + "]"), "[0].value"));
    CHECK(mentions(problemsOf("[" + record("a", "want", 5, "t", ">", "18446744073709551615") + "]"), "[0].value"));
    CHECK(mentions(problemsOf("[" + record("a", "want", 5, "t", ">", "1") + "," + record("a", "fear", 5, "t", ">", "1") + "]"), "[1].id"));
    CHECK(mentions(problemsOf("{}"), "expected an array"));
    CHECK(mentions(problemsOf("not json"), "not valid JSON"));
    CHECK(mentions(problemsOf("[{\"id\":\"a\"}]"), "[0].kind"));
    CHECK(mentions(problemsOf("[" + record("a", "want", 5, "t", ">", "1").insert(1, "\"extra\":1,") + "]"), "[0].extra"));
    const auto many = problemsOf("[" + record("a", "want", 0, "t", "?", "1") + "]");
    CHECK(many.size() == 2);
}

TEST_CASE("activeDesires applies each operator and sorts by intensity then id", "[npc][desires]")
{
    const auto list = parseOk("[" + record("d_lt", "want", 3, "x", "<", "5") + "," + record("d_le", "want", 9, "x", "<=", "5") + ","
        + record("d_eq", "fear", 9, "x", "==", "5") + "," + record("d_ge", "fear", 2, "x", ">=", "5") + ","
        + record("d_gt", "want", 6, "x", ">", "5") + "]");

    CHECK(ids(activeDesires(list, { { "x", 5 } })) == std::vector<std::string> { "d_eq", "d_le", "d_ge" });
    CHECK(ids(activeDesires(list, { { "x", 4 } })) == std::vector<std::string> { "d_le", "d_lt" });
    CHECK(ids(activeDesires(list, { { "x", 6 } })) == std::vector<std::string> { "d_gt", "d_ge" });
}

TEST_CASE("missing tags, empty inputs and extreme values", "[npc][desires]")
{
    const auto list = parseOk("[" + record("a", "want", 4, "grain", "<", "10") + "," + record("b", "fear", 5, "sea", ">", "9223372036854775806") + "]");
    CHECK(activeDesires(list, {}).empty());
    CHECK(activeDesires({}, { { "grain", 1 } }).empty());
    CHECK(ids(activeDesires(list, { { "grain", 1 } })) == std::vector<std::string> { "a" });

    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    CHECK(ids(activeDesires(list, { { "sea", kMax } })) == std::vector<std::string> { "b" });
    CHECK(activeDesires(list, { { "sea", kMin } }).empty());

    const auto edge = parseOk("[" + record("lo", "want", 1, "t", "<", std::to_string(kMin)) + "," + record("hi", "want", 1, "t", "<=", std::to_string(kMax)) + "]");
    CHECK(ids(activeDesires(edge, { { "t", kMin } })) == std::vector<std::string> { "hi" });
}

TEST_CASE("activeDesires is deterministic regardless of input order", "[npc][desires]")
{
    const auto a = parseOk("[" + record("p", "want", 5, "x", ">", "0") + "," + record("q", "fear", 5, "x", ">", "0") + "]");
    const auto b = parseOk("[" + record("q", "fear", 5, "x", ">", "0") + "," + record("p", "want", 5, "x", ">", "0") + "]");
    CHECK(ids(activeDesires(a, { { "x", 1 } })) == ids(activeDesires(b, { { "x", 1 } })));
    CHECK(ids(activeDesires(a, { { "x", 1 } })) == std::vector<std::string> { "p", "q" });
}

TEST_CASE("canonical form and digest ignore key order and list order", "[npc][desires]")
{
    const auto one = parseOk("[" + record("a", "want", 5, "x", ">", "0") + "]");
    const auto reordered = parseOk("[{\"value\":0,\"op\":\">\",\"tag\":\"x\",\"intensity\":5,\"subject\":\"grain\",\"kind\":\"want\",\"id\":\"a\"}]");
    CHECK(desireCanonicalJson(one[0]) == desireCanonicalJson(reordered[0]));
    CHECK(desireDigest(one[0]) == desireDigest(reordered[0]));

    const auto other = parseOk("[" + record("a", "want", 6, "x", ">", "0") + "]");
    CHECK(desireDigest(one[0]) != desireDigest(other[0]));

    const auto ab = parseOk("[" + record("a", "want", 5, "x", ">", "0") + "," + record("b", "fear", 5, "x", ">", "0") + "]");
    const auto ba = parseOk("[" + record("b", "fear", 5, "x", ">", "0") + "," + record("a", "want", 5, "x", ">", "0") + "]");
    CHECK(desiresDigest(ab) == desiresDigest(ba));
}

TEST_CASE("the shipped desires file parses and every record can fire", "[npc][desires]")
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "npc_desires" / "desires.json", std::ios::binary);
    REQUIRE(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto list = parseOk(text);
    CHECK(list.size() >= 4);
    bool hasWant = false;
    bool hasFear = false;
    for (const auto& d : list) {
        hasWant = hasWant || d.kind == DesireKind::Want;
        hasFear = hasFear || d.kind == DesireKind::Fear;
        const auto& c = d.condition;
        const std::int64_t holds = c.op == CompareOp::Lt ? c.value - 1 : c.op == CompareOp::Gt ? c.value + 1 : c.value;
        const std::int64_t fails = c.op == CompareOp::Lt ? c.value : c.op == CompareOp::Gt ? c.value : c.op == CompareOp::Le ? c.value + 1 : c.op == CompareOp::Ge ? c.value - 1 : c.value + 1;
        INFO(d.id);
        CHECK(ids(activeDesires({ d }, { { c.tag, holds } })) == std::vector<std::string> { d.id });
        CHECK(activeDesires({ d }, { { c.tag, fails } }).empty());
    }
    CHECK(hasWant);
    CHECK(hasFear);
}

TEST_CASE("the canonical form is pinned and the digest follows the content", "[npc][desires]")
{
    const auto one = parseOk("[" + record("a", "want", 5, "x", ">", "0") + "]");
    CHECK(desireCanonicalJson(one[0]) == "{\"id\":\"a\",\"intensity\":5,\"kind\":\"want\",\"op\":\">\",\"subject\":\"grain\",\"tag\":\"x\",\"value\":0}");

    const auto ab = parseOk("[" + record("a", "want", 5, "x", ">", "0") + "," + record("b", "fear", 5, "x", ">", "0") + "]");
    const auto changed = parseOk("[" + record("a", "want", 5, "x", ">", "0") + "," + record("b", "fear", 5, "x", ">", "1") + "]");
    const auto renamed = parseOk("[" + record("a", "want", 5, "x", ">", "0") + "," + record("c", "fear", 5, "x", ">", "0") + "]");
    const auto dropped = parseOk("[" + record("a", "want", 5, "x", ">", "0") + "]");
    CHECK(desiresDigest(ab) != desiresDigest(changed));
    CHECK(desiresDigest(ab) != desiresDigest(renamed));
    CHECK(desiresDigest(ab) != desiresDigest(dropped));
}

TEST_CASE("malformed shapes are reported, an empty array is fine", "[npc][desires]")
{
    CHECK(parseOk("[]").empty());
    CHECK(mentions(problemsOf("[1]"), "[0]: expected an object"));
    CHECK(mentions(problemsOf("[" + record("A", "want", 5, "t", ">", "1") + "]"), "[0].id"));
    CHECK(mentions(problemsOf("[" + record("1a", "want", 5, "t", ">", "1") + "]"), "[0].id"));
    CHECK(mentions(problemsOf("[" + record(std::string(65, 'a'), "want", 5, "t", ">", "1") + "]"), "[0].id"));
    CHECK(mentions(problemsOf("[{\"id\":5,\"kind\":7,\"subject\":\"s\",\"intensity\":\"3\",\"tag\":\"t\",\"op\":1,\"value\":\"1\"}]"), "[0].intensity"));
    CHECK(mentions(problemsOf("[" + record("a", "want", 5, "t", ">", "-9223372036854775809") + "]"), "[0].value"));
    CHECK(mentions(problemsOf("[" + record("a", "want", 0, "t", ">", "1") + "," + record("a", "want", 5, "t", ">", "1") + "]"), "[1].id"));
}
