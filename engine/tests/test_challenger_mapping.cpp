#include "ichor/factions/challenger_mapping.h"
#include "ichor/factions/incumbent.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string shippedText()
{
    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/factions/challengers.json", std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<ChallengerEntry> shipped()
{
    std::vector<ChallengerEntry> out;
    std::vector<std::string> problems;
    const bool ok = parseChallengerMapping("challengers.json", shippedText(), out, problems);
    INFO((problems.empty() ? std::string() : problems.front()));
    REQUIRE(ok);
    return out;
}

std::string entry(const std::string& id, const std::string& name = "The A", const std::string& extra = "")
{
    return R"({"id":")" + id + R"(","name":")" + name + R"(","diagnosis":"d","prescription":"p","requires_incumbent":false)" + extra + "}";
}

bool rejects(const std::string& text, const std::string& expectedFragment)
{
    std::vector<ChallengerEntry> out;
    std::vector<std::string> problems;
    if (parseChallengerMapping("t.json", text, out, problems) || !out.empty() || problems.empty()) {
        return false;
    }
    for (const std::string& problem : problems) {
        if (problem.find(expectedFragment) != std::string::npos) {
            return true;
        }
    }
    INFO(problems.front());
    return false;
}

bool offers(const std::vector<ChallengerEntry>& mapping, bool hasIncumbent, const std::string& id)
{
    for (const ChallengerEntry* e : availableChallengers(mapping, hasIncumbent)) {
        if (e->faction.id == id) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("the shipped file has the three archetypes, each a Challenger with a diagnosis and a prescription")
{
    const auto mapping = shipped();
    REQUIRE(mapping.size() == 3);
    for (const char* id : {"archaists", "diadochoi", "isonomoi"}) {
        const ChallengerEntry* found = challengerFor(mapping, id);
        REQUIRE(found != nullptr);
        REQUIRE(found->faction.id == id);
        REQUIRE(found->faction.role == FactionRole::Challenger);
        REQUIRE(found->faction.name.rfind("The ", 0) == 0);
        REQUIRE_FALSE(found->diagnosis.empty());
        REQUIRE_FALSE(found->prescription.empty());
        REQUIRE(found->diagnosis != found->prescription);
    }
    REQUIRE(challengerFor(mapping, "archaists")->faction.name == "The Archaists");
    REQUIRE(challengerFor(mapping, "diadochoi")->faction.name == "The Diadochoi");
    REQUIRE(challengerFor(mapping, "isonomoi")->faction.name == "The Isonomoi");
}

TEST_CASE("only the Diadochoi require an Incumbent")
{
    const auto mapping = shipped();
    REQUIRE(challengerFor(mapping, "diadochoi")->requiresIncumbent);
    REQUIRE_FALSE(challengerFor(mapping, "archaists")->requiresIncumbent);
    REQUIRE_FALSE(challengerFor(mapping, "isonomoi")->requiresIncumbent);
}

TEST_CASE("a world with no Incumbent cannot field the Diadochoi")
{
    const auto mapping = shipped();
    const auto with = availableChallengers(mapping, true);
    REQUIRE(with.size() == 3);
    const auto without = availableChallengers(mapping, false);
    REQUIRE(without.size() == 2);
    REQUIRE(offers(mapping, true, "diadochoi"));
    REQUIRE_FALSE(offers(mapping, false, "diadochoi"));
    REQUIRE(offers(mapping, false, "archaists"));
    REQUIRE(offers(mapping, false, "isonomoi"));
    REQUIRE(availableChallengers({}, true).empty());
}

TEST_CASE("availability agrees with the Incumbent plan of every Powers axiom")
{
    const auto mapping = shipped();
    for (const char* powers : {"powers.hegemony", "powers.league", "powers.satrapy", "powers.amphictyony", "powers.poleis", "powers.dark_age"}) {
        const auto plan = planIncumbents(powers);
        REQUIRE(plan.has_value());
        const bool hasIncumbent = !plan->incumbents.empty();
        REQUIRE(offers(mapping, hasIncumbent, "diadochoi") == hasIncumbent);
        REQUIRE(offers(mapping, hasIncumbent, "archaists"));
    }
}

TEST_CASE("challengers form valid faction records")
{
    std::vector<Faction> factions;
    for (const ChallengerEntry& e : shipped()) {
        factions.push_back(e.faction);
    }
    std::vector<std::string> problems;
    std::vector<Faction> reparsed;
    REQUIRE(parseFactions("canon", factionsCanonicalJson(factions), reparsed, problems));
    REQUIRE(reparsed.size() == 3);
}

TEST_CASE("an unknown or empty id resolves to nothing")
{
    const auto mapping = shipped();
    REQUIRE(challengerFor(mapping, "nobody") == nullptr);
    REQUIRE(challengerFor(mapping, "") == nullptr);
    REQUIRE(challengerFor({}, "archaists") == nullptr);
}

TEST_CASE("the output is sorted by id whatever the source order")
{
    std::vector<ChallengerEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parseChallengerMapping("t.json", "[" + entry("zeta") + "," + entry("alpha") + "]", out, problems));
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].faction.id == "alpha");
    REQUIRE(out[1].faction.id == "zeta");
}

TEST_CASE("challenger mapping: malformed input is refused without crashing")
{
    REQUIRE(rejects("", "empty file"));
    REQUIRE(rejects("   \n", "empty file"));
    REQUIRE(rejects("[", "not valid JSON"));
    REQUIRE(rejects("{}", "expected an array"));
    REQUIRE(rejects("[1]", "[0]: expected an object"));
    for (const char* key : {"id", "name", "diagnosis", "prescription", "requires_incumbent"}) {
        REQUIRE(rejects("[{}]", std::string("[0].") + key + ": missing"));
    }
    REQUIRE(rejects("[" + entry("a", "The A", R"(,"x":1)") + "]", "[0].x: unknown key"));
    REQUIRE(rejects(R"([{"id":5,"name":"A","diagnosis":"d","prescription":"p","requires_incumbent":false}])", "[0].id: expected a string"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","diagnosis":"d","prescription":"p","requires_incumbent":"yes"}])", "[0].requires_incumbent: expected a boolean"));
    REQUIRE(rejects("[" + entry("Bad Id") + "]", "[0].id"));
    REQUIRE(rejects("[" + entry("") + "]", "[0].id"));
    REQUIRE(rejects("[" + entry("a", "  ") + "]", "[0].name: must not be empty"));
    REQUIRE(rejects("[" + entry("a", "The A \xE2\x80\x94 B") + "]", "em-dash"));
    REQUIRE(rejects("[" + entry("a", "A\\nB") + "]", "control characters"));
    REQUIRE(rejects(R"([{"id":"a","id":"b","name":"A","diagnosis":"d","prescription":"p","requires_incumbent":false}])", "duplicate key"));
    REQUIRE(rejects("[" + entry("a") + "," + entry("a") + "]", "[1].id: duplicate"));
    REQUIRE(rejects("[" + entry(std::string(65, 'a')) + "]", "[0].id: longer than"));
    REQUIRE(rejects("[" + entry("a", std::string(121, 'n')) + "]", "[0].name: longer than"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","diagnosis":"","prescription":"p","requires_incumbent":false}])", "[0].diagnosis: must not be empty"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","diagnosis":"d","prescription":" ","requires_incumbent":false}])", "[0].prescription: must not be empty"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","diagnosis":")" + std::string(401, 'x') + R"(","prescription":"p","requires_incumbent":false}])", "[0].diagnosis: longer than"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","diagnosis":"d — e","prescription":"p","requires_incumbent":false}])", "[0].diagnosis: must not contain an em-dash"));
}

TEST_CASE("challenger mapping: hostile bytes are bounded")
{
    REQUIRE(rejects(std::string(1024 * 1024 + 1, ' '), "larger than"));
    REQUIRE(rejects(std::string(40, '[') + std::string(40, ']'), "nested deeper"));
    std::string many = "[";
    for (int i = 0; i < 65; ++i) {
        many += (i ? "," : "") + std::string("{}");
    }
    REQUIRE(rejects(many + "]", "more than"));
    REQUIRE(rejects("[{\"id\":\"a\",\"name\":\"A\xFF\"}]", "not valid JSON"));
}

TEST_CASE("challenger mapping: a failed parse leaves the output untouched")
{
    std::vector<ChallengerEntry> out(1);
    out[0].faction.id = "keep";
    std::vector<std::string> problems;
    REQUIRE_FALSE(parseChallengerMapping("t.json", "[{}]", out, problems));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].faction.id == "keep");
}

TEST_CASE("challenger mapping: an empty array is valid")
{
    std::vector<ChallengerEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parseChallengerMapping("t.json", "[]", out, problems));
    REQUIRE(out.empty());
}
