#include "ichor/factions/factions.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string entry(const std::string& id, const std::string& role)
{
    return R"({"id": ")" + id + R"(", "name": "The )" + id + R"(", "role": ")" + role + R"("})";
}

const std::string kValid = "[" + entry("archonate", "incumbent") + "," + entry("archaists", "challenger") + ","
    + entry("metallarchs", "parasite") + "," + entry("mnemones", "custodian") + "," + entry("asebeis", "refusenik") + ","
    + entry("kerykes", "herald") + "]";

std::vector<std::string> parseProblems(const std::string& text)
{
    std::vector<Faction> out;
    std::vector<std::string> problems;
    const bool ok = parseFactions("f.json", text, out, problems);
    CHECK(ok == problems.empty());
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

Faction make(const std::string& id, FactionRole role) { return Faction{id, "The " + id, role}; }

std::vector<Faction> validSet()
{
    return {make("a", FactionRole::Incumbent), make("b", FactionRole::Challenger), make("c", FactionRole::Parasite),
        make("d", FactionRole::Custodian), make("e", FactionRole::Refusenik), make("f", FactionRole::Herald)};
}

}

TEST_CASE("the six roles round-trip through their names", "[factions]")
{
    const char* names[] = {"incumbent", "challenger", "parasite", "custodian", "refusenik", "herald"};
    for (std::size_t i = 0; i < kFactionRoleCount; ++i) {
        const auto role = factionRoleFromName(names[i]);
        REQUIRE(role.has_value());
        CHECK(static_cast<std::size_t>(*role) == i);
        CHECK(factionRoleName(*role) == names[i]);
    }
    CHECK_FALSE(factionRoleFromName("Incumbent").has_value());
    CHECK_FALSE(factionRoleFromName("").has_value());
    CHECK_FALSE(factionRoleFromName("king").has_value());
}

TEST_CASE("a valid faction list parses", "[factions]")
{
    std::vector<Faction> out;
    std::vector<std::string> problems;
    REQUIRE(parseFactions("f.json", kValid, out, problems));
    CHECK(problems.empty());
    REQUIRE(out.size() == 6);
    CHECK(out[0].id == "archonate");
    CHECK(out[0].role == FactionRole::Incumbent);
    CHECK(out[5].role == FactionRole::Herald);
    CHECK(validateFactionSet(out).empty());
}

TEST_CASE("parse problems carry the label and a path", "[factions]")
{
    const auto problems = parseProblems("[" + entry("ok", "herald") + "," + entry("bad", "Herald") + "]");
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].rfind("f.json: [1].role: ", 0) == 0);
}

TEST_CASE("a bad document reports every problem, not the first", "[factions]")
{
    const auto problems = parseProblems(R"([{"id": "A", "name": "", "role": "king", "extra": 1}, 5, {"id": 3, "name": 4, "role": 5}])");
    CHECK(mentions(problems, "[0].id"));
    CHECK(mentions(problems, "[0].name"));
    CHECK(mentions(problems, "[0].role"));
    CHECK(mentions(problems, "[0].extra"));
    CHECK(mentions(problems, "[1]"));
    CHECK(mentions(problems, "[2].id"));
    CHECK(mentions(problems, "[2].name"));
    CHECK(mentions(problems, "[2].role"));
}

TEST_CASE("missing keys are named", "[factions]")
{
    const auto problems = parseProblems("[{}]");
    CHECK(mentions(problems, "[0].id: missing"));
    CHECK(mentions(problems, "[0].name: missing"));
    CHECK(mentions(problems, "[0].role: missing"));
}

TEST_CASE("the document shape is enforced", "[factions]")
{
    CHECK_FALSE(parseProblems("").empty());
    CHECK_FALSE(parseProblems("   ").empty());
    CHECK_FALSE(parseProblems("{").empty());
    CHECK_FALSE(parseProblems("{}").empty());
    CHECK_FALSE(parseProblems("null").empty());
    CHECK_FALSE(parseProblems(R"([{"id": "a", "id": "b", "name": "n", "role": "herald"}])").empty());
    CHECK(parseProblems("[]").empty());
}

TEST_CASE("role names are lowercase and exact", "[factions]")
{
    for (const char* role : {"Incumbent", "HERALD", "herald ", "", "tyrant", "herald\\u0000"}) {
        CHECK_FALSE(parseProblems("[" + entry("a", role) + "]").empty());
    }
    CHECK(parseProblems(R"([{"id": "a", "name": "n", "role": null}])").size() == 1);
}

TEST_CASE("ids are lowercase ascii words", "[factions]")
{
    for (const char* id : {"", "A", "1a", "a b", "a-b", "\xCE\xB1\xCF\x81\xCF\x87", "caf\xC3\xA9", "a.b"}) {
        CHECK_FALSE(parseProblems("[" + entry(id, "herald") + "]").empty());
    }
    CHECK(parseProblems("[" + entry("a_1", "herald") + "]").empty());
    CHECK_FALSE(parseProblems("[" + entry(std::string(65, 'a'), "herald") + "]").empty());
    CHECK(parseProblems("[" + entry(std::string(64, 'a'), "herald") + "]").empty());
}

TEST_CASE("names are bounded, non-empty and free of em-dashes", "[factions]")
{
    const auto named = [](const std::string& name) { return parseProblems(R"([{"id": "a", "name": ")" + name + R"(", "role": "herald"}])"); };
    CHECK(named("The Horkos-brokers").empty());
    CHECK(named("\xCE\x9C\xCE\xBD\xCE\xB7\xCE\xBC\xCF\x8C\xCE\xBD\xCE\xB5\xCF\x82").empty());
    CHECK_FALSE(named("").empty());
    CHECK_FALSE(named("   ").empty());
    CHECK_FALSE(named("a \xE2\x80\x94 b").empty());
    CHECK_FALSE(named(std::string(121, 'x')).empty());
    CHECK(named(std::string(120, 'x')).empty());
    CHECK_FALSE(named(std::string(200000, 'x')).empty());
}

TEST_CASE("hostile documents are refused without crashing", "[factions]")
{
    CHECK_FALSE(parseProblems(std::string(100000, '[')).empty());
    CHECK_FALSE(parseProblems(std::string(2 * 1024 * 1024, ' ') + "[]").empty());
    CHECK_FALSE(parseProblems("[" + std::string(1000, '[') + "]").empty());
}

TEST_CASE("a failed parse leaves the output untouched", "[factions]")
{
    std::vector<Faction> out = {make("keep", FactionRole::Herald)};
    std::vector<std::string> problems;
    CHECK_FALSE(parseFactions("f.json", "[{}]", out, problems));
    REQUIRE(out.size() == 1);
    CHECK(out[0].id == "keep");
}

TEST_CASE("duplicate ids are a parse problem and a set violation", "[factions]")
{
    const auto problems = parseProblems("[" + entry("a", "herald") + "," + entry("a", "parasite") + "]");
    CHECK(mentions(problems, "[1].id"));
    CHECK(mentions(problems, "duplicate"));

    auto set = validSet();
    set[1].id = "a";
    CHECK(mentions(validateFactionSet(set), "duplicate"));
}

TEST_CASE("the canonical form and digest ignore order and layout", "[factions]")
{
    std::vector<Faction> a;
    std::vector<std::string> problems;
    REQUIRE(parseFactions("a", kValid, a, problems));
    auto b = a;
    std::reverse(b.begin(), b.end());
    CHECK(factionsCanonicalJson(a) == factionsCanonicalJson(b));
    CHECK(factionsDigest(a) == factionsDigest(b));

    std::vector<Faction> spaced;
    REQUIRE(parseFactions("b", R"( [ { "role" : "herald", "name": "N", "id": "x" } ] )", spaced, problems));
    std::vector<Faction> compact;
    REQUIRE(parseFactions("c", R"([{"id":"x","name":"N","role":"herald"}])", compact, problems));
    CHECK(factionsCanonicalJson(spaced) == factionsCanonicalJson(compact));
    CHECK(factionsCanonicalJson(compact) == R"([{"id":"x","name":"N","role":"herald"}])");
}

TEST_CASE("any change to a faction changes the digest", "[factions]")
{
    const auto base = validSet();
    const auto digest = factionsDigest(base);
    auto renamed = base;
    renamed[0].name = "Other";
    auto reroled = base;
    reroled[0].role = FactionRole::Herald;
    auto reid = base;
    reid[0].id = "z";
    auto fewer = base;
    fewer.pop_back();
    CHECK(factionsDigest(renamed) != digest);
    CHECK(factionsDigest(reroled) != digest);
    CHECK(factionsDigest(reid) != digest);
    CHECK(factionsDigest(fewer) != digest);
}

TEST_CASE("a valid set has no violations", "[factions]")
{
    CHECK(validateFactionSet(validSet()).empty());

    auto seven = validSet();
    seven.push_back(make("g", FactionRole::Challenger));
    CHECK(validateFactionSet(seven).empty());

    auto darkAge = validSet();
    darkAge[0] = make("a", FactionRole::Parasite);
    CHECK(validateFactionSet(darkAge).empty());

    auto poleis = validSet();
    poleis[2] = make("c", FactionRole::Incumbent);
    poleis.push_back(make("g", FactionRole::Parasite));
    CHECK(validateFactionSet(poleis).empty());
}

TEST_CASE("an empty set breaks the count and every required role", "[factions]")
{
    const auto violations = validateFactionSet({});
    CHECK(mentions(violations, "5 to 7"));
    CHECK(mentions(violations, "challenger"));
    CHECK(mentions(violations, "parasite"));
    CHECK(mentions(violations, "custodian"));
    CHECK(mentions(violations, "refusenik"));
    CHECK(mentions(violations, "herald"));
    CHECK_FALSE(mentions(violations, "incumbent"));
}

TEST_CASE("the faction count is 5 to 7", "[factions]")
{
    auto four = validSet();
    four.pop_back();
    four.pop_back();
    CHECK(mentions(validateFactionSet(four), "5 to 7"));

    auto five = validSet();
    five.pop_back();
    CHECK_FALSE(mentions(validateFactionSet(five), "5 to 7"));

    auto eight = validSet();
    eight.push_back(make("g", FactionRole::Challenger));
    eight.push_back(make("h", FactionRole::Parasite));
    CHECK(mentions(validateFactionSet(eight), "5 to 7"));
}

TEST_CASE("each missing role is named", "[factions]")
{
    for (std::size_t drop = 1; drop < kFactionRoleCount; ++drop) {
        auto set = validSet();
        set[drop] = make("x" + std::to_string(drop), FactionRole::Incumbent);
        const auto violations = validateFactionSet(set);
        REQUIRE(violations.size() == 1);
        CHECK(violations[0].find(std::string(factionRoleName(static_cast<FactionRole>(drop)))) != std::string::npos);
    }
}

TEST_CASE("at most two Challengers", "[factions]")
{
    auto set = validSet();
    set.push_back(make("g", FactionRole::Challenger));
    CHECK(validateFactionSet(set).empty());
    set[0] = make("a", FactionRole::Challenger);
    const auto violations = validateFactionSet(set);
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].find("challenger") != std::string::npos);
}

TEST_CASE("every violation is returned at once", "[factions]")
{
    std::vector<Faction> set = {make("a", FactionRole::Herald), make("a", FactionRole::Herald), make("b", FactionRole::Challenger),
        make("c", FactionRole::Challenger), make("d", FactionRole::Challenger)};
    const auto violations = validateFactionSet(set);
    CHECK(mentions(violations, "duplicate"));
    CHECK(mentions(violations, "challenger"));
    CHECK(mentions(violations, "parasite"));
    CHECK(mentions(violations, "custodian"));
    CHECK(mentions(violations, "refusenik"));
    CHECK(violations.size() == 5);
}

TEST_CASE("an oversized list is refused at parse time", "[factions]")
{
    std::string many = "[";
    for (int i = 0; i < 65; ++i) {
        many += (i == 0 ? "" : ",") + entry("f" + std::to_string(i), "herald");
    }
    CHECK(mentions(parseProblems(many + "]"), "more than 64"));
    CHECK(parseProblems(many.substr(0, many.rfind(",")) + "]").empty());

    std::string empties = "[{}";
    for (int i = 0; i < 100000; ++i) {
        empties += ",{}";
    }
    CHECK(parseProblems(empties + "]").size() == 1);
}

TEST_CASE("the digest ignores order even when ids repeat", "[factions]")
{
    std::vector<Faction> a = {make("x", FactionRole::Herald), make("x", FactionRole::Parasite), Faction{"x", "Other", FactionRole::Herald}};
    auto b = a;
    std::reverse(b.begin(), b.end());
    CHECK(factionsDigest(a) == factionsDigest(b));
}

TEST_CASE("equal keys in sibling and nested objects are not duplicates", "[factions]")
{
    CHECK(parseProblems(kValid).empty());
    CHECK(mentions(parseProblems(R"([{"id": "a", "id": "b", "name": "n", "role": "herald"}])"), "duplicate key"));
    CHECK(mentions(parseProblems(R"([{"id": "a", "name": "n", "role": "herald", "z": 1}])"), "[0].z: unknown key"));
}
