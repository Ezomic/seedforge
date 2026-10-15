#include "ichor/data/axiom.h"
#include "ichor/factions/parasite_mapping.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

std::string shippedText()
{
    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/factions/parasites.json", std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<ParasiteEntry> shipped()
{
    std::vector<ParasiteEntry> out;
    std::vector<std::string> problems;
    const bool ok = parseParasiteMapping("parasites.json", shippedText(), out, problems);
    INFO((problems.empty() ? std::string() : problems.front()));
    REQUIRE(ok);
    return out;
}

bool rejects(const std::string& text, const std::string& expectedFragment)
{
    std::vector<ParasiteEntry> out;
    std::vector<std::string> problems;
    if (parseParasiteMapping("t.json", text, out, problems) || !out.empty() || problems.empty()) {
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

}

TEST_CASE("the shipped mapping gives each of the six Law axioms its face")
{
    const auto mapping = shipped();
    REQUIRE(mapping.size() == 6);
    const std::vector<std::pair<std::string, std::string>> expected = {{"law.orichalcum", "metallarchs"}, {"law.hecatomb", "hekatombaia"},
        {"law.mysteries", "telestai"}, {"law.temenos", "horistai"}, {"law.last_favour", "thesauroi"}, {"law.oath", "horkos_brokers"}};
    for (const auto& [axiom, id] : expected) {
        const auto faction = parasiteFor(mapping, axiom);
        REQUIRE(faction.has_value());
        REQUIRE(faction->id == id);
        REQUIRE(faction->role == FactionRole::Parasite);
        REQUIRE(faction->name.rfind("The ", 0) == 0);
    }
}

TEST_CASE("every Law axiom in the shipped data has a Parasite face")
{
    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_REPO_DATA));
    std::vector<std::string> lawIds;
    for (const auto& [id, axiom] : axioms.axioms()) {
        if (axiom.slot == AxiomSlot::Law) {
            lawIds.push_back(id);
        }
    }
    REQUIRE(lawIds.size() >= 3);
    REQUIRE(parasiteCoverageProblems(shipped(), lawIds).empty());
}

TEST_CASE("mapped Parasites form valid faction records")
{
    const auto mapping = shipped();
    std::vector<Faction> factions;
    for (const ParasiteEntry& entry : mapping) {
        factions.push_back(*parasiteFor(mapping, entry.axiom));
    }
    std::vector<std::string> problems;
    std::vector<Faction> reparsed;
    REQUIRE(parseFactions("canon", factionsCanonicalJson(factions), reparsed, problems));
    REQUIRE(reparsed.size() == 6);
}

TEST_CASE("an axiom with no face, or a non-Law id, resolves to nothing")
{
    const auto mapping = shipped();
    REQUIRE_FALSE(parasiteFor(mapping, "law.unknown").has_value());
    REQUIRE_FALSE(parasiteFor(mapping, "").has_value());
    REQUIRE_FALSE(parasiteFor(mapping, "powers.hegemony").has_value());
    REQUIRE_FALSE(parasiteFor({}, "law.orichalcum").has_value());
}

TEST_CASE("coverage names every Law axiom that has no face")
{
    const auto mapping = shipped();
    REQUIRE(parasiteCoverageProblems(mapping, {}).empty());
    const auto problems = parasiteCoverageProblems(mapping, {"law.orichalcum", "law.new_one", "law.other"});
    REQUIRE(problems.size() == 2);
    REQUIRE(problems[0].find("law.new_one") != std::string::npos);
    REQUIRE(problems[1].find("law.other") != std::string::npos);
    REQUIRE(parasiteCoverageProblems({}, {"law.a"}).size() == 1);
}

TEST_CASE("the mapping parser refuses malformed input without crashing")
{
    REQUIRE(rejects("", "empty file"));
    REQUIRE(rejects("   \n", "empty file"));
    REQUIRE(rejects("[", "not valid JSON"));
    REQUIRE(rejects("{}", "expected an array"));
    REQUIRE(rejects("[1]", "[0]: expected an object"));
    REQUIRE(rejects("[{}]", "[0].axiom: missing"));
    REQUIRE(rejects("[{}]", "[0].id: missing"));
    REQUIRE(rejects("[{}]", "[0].name: missing"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"a","name":"The A","x":1}])", "[0].x: unknown key"));
    REQUIRE(rejects(R"([{"axiom":5,"id":"a","name":"The A"}])", "[0].axiom: expected a string"));
    REQUIRE(rejects(R"([{"axiom":"powers.a","id":"a","name":"The A"}])", "[0].axiom: expected a Law axiom id"));
    REQUIRE(rejects(R"([{"axiom":"law.","id":"a","name":"The A"}])", "[0].axiom: expected a Law axiom id"));
    REQUIRE(rejects(R"([{"axiom":"law.A","id":"a","name":"The A"}])", "[0].axiom: expected a Law axiom id"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"Bad Id","name":"The A"}])", "[0].id"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"a","name":"  "}])", "[0].name: must not be empty"));
    REQUIRE(rejects("[{\"axiom\":\"law.a\",\"id\":\"a\",\"name\":\"The A \xE2\x80\x94 B\"}]", "em-dash"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"a","name":"The A","id":"b"}])", "duplicate key"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"a","name":"A"},{"axiom":"law.b","id":"b","id":"c","name":"B"}])", "duplicate key"));
    REQUIRE(rejects("[{\"axiom\":\"law.a\",\"id\":\"a\",\"name\":\"A\nB\"}]", "control characters"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"a","name":"A"},{"axiom":"law.a","id":"b","name":"B"}])", "[1].axiom: duplicate"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"a","name":"A"},{"axiom":"law.b","id":"a","name":"B"}])", "[1].id: duplicate"));
    REQUIRE(rejects(std::string(R"([{"axiom":"law.a","id":")") + std::string(65, 'a') + R"(","name":"A"}])", "[0].id: longer than"));
    REQUIRE(rejects(R"([{"axiom":"law.a","id":"a","name":")" + std::string(121, 'n') + R"("}])", "[0].name: longer than"));
}

TEST_CASE("hostile bytes are bounded")
{
    REQUIRE(rejects(std::string(1024 * 1024 + 1, ' '), "larger than"));
    REQUIRE(rejects(std::string(40, '[') + std::string(40, ']'), "nested deeper"));
    std::string many = "[";
    for (int i = 0; i < 65; ++i) {
        many += (i ? "," : "") + std::string("{}");
    }
    REQUIRE(rejects(many + "]", "more than"));
    REQUIRE(rejects("[{\"axiom\":\"law.a\",\"id\":\"a\",\"name\":\"A\xFF\"}]", "not valid JSON"));
}

TEST_CASE("a failed parse leaves the output untouched")
{
    std::vector<ParasiteEntry> out = {{"law.keep", "keep", "The Keep"}};
    std::vector<std::string> problems;
    REQUIRE_FALSE(parseParasiteMapping("t.json", "[{}]", out, problems));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].id == "keep");
}

TEST_CASE("an empty array is a valid empty mapping")
{
    std::vector<ParasiteEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parseParasiteMapping("t.json", "[]", out, problems));
    REQUIRE(out.empty());
}
