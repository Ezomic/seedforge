#include "ichor/data/axiom.h"
#include "ichor/factions/herald_mapping.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

bool parse(const std::string& text, std::vector<HeraldEntry>& out, std::vector<std::string>& problems)
{
    return parseHeraldMapping("heralds", text, out, problems);
}

bool hasProblem(const std::vector<std::string>& problems, const std::string& needle)
{
    for (const std::string& p : problems) {
        if (p.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string shippedText()
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "heralds" / "mapping.json", std::ios::binary);
    REQUIRE(file.good());
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

const char* kTwo = R"([
    {"id": "b", "pressure": "pressure.zeta", "name": "B"},
    {"id": "a", "pressure": "pressure.alpha", "name": "A"}
])";

}

TEST_CASE("a mapping parses into Herald factions sorted by pressure", "[herald]")
{
    std::vector<HeraldEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parse(kTwo, out, problems));
    REQUIRE(out.size() == 2);
    CHECK(out[0].pressure == "pressure.alpha");
    CHECK(out[0].faction.id == "a");
    CHECK(out[0].faction.role == FactionRole::Herald);
    const Faction* found = heraldFor(out, "pressure.zeta");
    REQUIRE(found != nullptr);
    CHECK(found->name == "B");
}

TEST_CASE("an unknown or empty pressure id has no Herald", "[herald]")
{
    std::vector<HeraldEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parse(kTwo, out, problems));
    CHECK(heraldFor(out, "pressure.nope") == nullptr);
    CHECK(heraldFor(out, "") == nullptr);
    CHECK(heraldFor({}, "pressure.alpha") == nullptr);
}

TEST_CASE("a mapping that is not well formed is refused with every problem", "[herald]")
{
    std::vector<HeraldEntry> out;
    std::vector<std::string> problems;
    SECTION("empty and not JSON")
    {
        CHECK_FALSE(parse("", out, problems));
        CHECK_FALSE(parse("{oops", out, problems));
        CHECK_FALSE(parse("{}", out, problems));
        CHECK(problems.size() == 3);
    }
    SECTION("an empty array has nothing to map")
    {
        CHECK_FALSE(parse("[]", out, problems));
    }
    SECTION("duplicate herald and pressure ids")
    {
        CHECK_FALSE(parse(R"([
            {"id": "a", "pressure": "pressure.x", "name": "A"},
            {"id": "a", "pressure": "pressure.y", "name": "A2"},
            {"id": "c", "pressure": "pressure.x", "name": "C"}])", out, problems));
        CHECK(hasProblem(problems, "[1].id: duplicate"));
        CHECK(hasProblem(problems, "[2].pressure: duplicate"));
    }
    SECTION("missing, wrong typed, unknown and malformed fields")
    {
        CHECK_FALSE(parse(R"([5, {"id": 3, "name": "x"}, {"id": "Bad Id", "pressure": "nope", "name": "  ", "extra": 1}])", out, problems));
        CHECK(hasProblem(problems, "[0]: expected an object"));
        CHECK(hasProblem(problems, "[1].id: expected a string"));
        CHECK(hasProblem(problems, "[1].pressure: missing"));
        CHECK(hasProblem(problems, "[2].id: expected lowercase"));
        CHECK(hasProblem(problems, "[2].pressure: expected"));
        CHECK(hasProblem(problems, "[2].name: must not be empty"));
        CHECK(hasProblem(problems, "[2].extra: unknown key"));
    }
    SECTION("em-dash and oversize names")
    {
        CHECK_FALSE(parse("[{\"id\": \"a\", \"pressure\": \"pressure.x\", \"name\": \"A \xE2\x80\x94 B\"}]", out, problems));
        CHECK_FALSE(parse(std::string("[{\"id\": \"a\", \"pressure\": \"pressure.x\", \"name\": \"") + std::string(500, 'n') + "\"}]", out, problems));
        CHECK(problems.size() == 2);
    }
    SECTION("duplicate keys")
    {
        CHECK_FALSE(parse(R"([{"id": "a", "id": "b", "pressure": "pressure.x", "name": "A"}])", out, problems));
        CHECK(hasProblem(problems, "duplicate key"));
    }
    CHECK(out.empty());
}

TEST_CASE("hostile bytes never crash or allocate unbounded memory", "[herald]")
{
    std::vector<HeraldEntry> out;
    std::vector<std::string> problems;
    const auto refused = [&](const std::string& text, const std::string& why) {
        problems.clear();
        CHECK_FALSE(parse(text, out, problems));
        CHECK(hasProblem(problems, why));
    };
    refused("[" + std::string(2 * 1024 * 1024, ' ') + "]", "larger than");
    refused(std::string(17, '[') + std::string(17, ']'), "nested deeper");
    refused(std::string(100000, '['), "nested deeper");
    std::string withNul = "[{\"id\":\"a";
    withNul += '\0';
    withNul += "b\",\"pressure\":\"pressure.x\",\"name\":\"n\"}]";
    refused(withNul, "not valid JSON");
    std::string many = "[";
    for (int i = 0; i < 65; ++i) {
        many += (i ? "," : "") + std::string("{\"id\":\"h") + std::to_string(i) + "\",\"pressure\":\"pressure.p" + std::to_string(i) + "\",\"name\":\"n\"}";
    }
    refused(many + "]", "more than 64");
    CHECK(out.empty());
}

TEST_CASE("the order of the source does not change the result", "[herald]")
{
    std::vector<HeraldEntry> a;
    std::vector<HeraldEntry> b;
    std::vector<std::string> problems;
    REQUIRE(parse(kTwo, a, problems));
    REQUIRE(parse(R"([
        {"id": "a", "pressure": "pressure.alpha", "name": "A"},
        {"id": "b", "pressure": "pressure.zeta", "name": "B"}])", b, problems));
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].pressure == b[i].pressure);
        CHECK(a[i].faction.id == b[i].faction.id);
    }
}

TEST_CASE("the shipped mapping covers every authored Pressure axiom", "[herald]")
{
    std::vector<HeraldEntry> mapping;
    std::vector<std::string> problems;
    const bool ok = parse(shippedText(), mapping, problems);
    for (const std::string& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(ok);
    CHECK(mapping.size() == 6);
    const Faction* miasma = heraldFor(mapping, "pressure.miasma");
    const Faction* ret = heraldFor(mapping, "pressure.return");
    REQUIRE(miasma != nullptr);
    REQUIRE(ret != nullptr);
    CHECK(miasma->id == "miastores");
    CHECK(ret->id == "kerykes");

    // Famine, Return and Opening are in doc 01 but not authored yet, so the mapping may run ahead of the axioms, never behind.
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_GAME_DATA_DIR));
    std::size_t pressures = 0;
    for (const auto& [id, axiom] : axioms.axioms()) {
        if (axiom.slot == AxiomSlot::Pressure) {
            ++pressures;
            INFO(id);
            CHECK(heraldFor(mapping, id) != nullptr);
        }
    }
    CHECK(pressures >= 3);
    CHECK(mapping.size() >= pressures);
}
