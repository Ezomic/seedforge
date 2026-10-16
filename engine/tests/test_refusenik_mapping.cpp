#include "ichor/factions/refusenik_mapping.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string shippedText()
{
    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/factions/refuseniks.json", std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<RefusenikEntry> shipped()
{
    std::vector<RefusenikEntry> out;
    std::vector<std::string> problems;
    const bool ok = parseRefusenikMapping("refuseniks.json", shippedText(), out, problems);
    INFO((problems.empty() ? std::string() : problems.front()));
    REQUIRE(ok);
    return out;
}

std::string entry(const std::string& id, const std::string& name = "The A", const std::string& extra = "")
{
    return R"({"id":")" + id + R"(","name":")" + name + R"(","placement":"routes","claims_territory":false,"offers_services_anywhere":false,"summary":"s")" + extra + "}";
}

bool rejects(const std::string& text, const std::string& expectedFragment)
{
    std::vector<RefusenikEntry> out;
    std::vector<std::string> problems;
    if (parseRefusenikMapping("t.json", text, out, problems) || !out.empty() || problems.empty()) {
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

TEST_CASE("the shipped file has the two archetypes, each a Refusenik")
{
    const auto mapping = shipped();
    REQUIRE(mapping.size() == 2);
    for (const char* id : {"asebeis", "xenoi"}) {
        const RefusenikEntry* found = refusenikFor(mapping, id);
        REQUIRE(found != nullptr);
        REQUIRE(found->faction.id == id);
        REQUIRE(found->faction.role == FactionRole::Refusenik);
        REQUIRE(found->faction.name.rfind("The ", 0) == 0);
        REQUIRE_FALSE(found->summary.empty());
    }
    REQUIRE(refusenikFor(mapping, "asebeis")->faction.name == "The Asebeis");
    REQUIRE(refusenikFor(mapping, "xenoi")->faction.name == "The Xenoi");
}

TEST_CASE("the Asebeis hold marginal land and the Xenoi hold none")
{
    const auto mapping = shipped();
    const RefusenikEntry* asebeis = refusenikFor(mapping, "asebeis");
    const RefusenikEntry* xenoi = refusenikFor(mapping, "xenoi");
    REQUIRE(asebeis->placement == RefusenikPlacement::MarginalLand);
    REQUIRE(asebeis->claimsTerritory);
    REQUIRE(xenoi->placement == RefusenikPlacement::Routes);
    REQUIRE_FALSE(xenoi->claimsTerritory);
    REQUIRE(refusenikPlacementName(asebeis->placement) == "marginal_land");
    REQUIRE(refusenikPlacementName(xenoi->placement) == "routes");
}

TEST_CASE("only the Xenoi offer services anywhere")
{
    const auto mapping = shipped();
    REQUIRE(refusenikFor(mapping, "xenoi")->offersServicesAnywhere);
    REQUIRE_FALSE(refusenikFor(mapping, "asebeis")->offersServicesAnywhere);
    const auto anywhere = servicesAnywhereRefuseniks(mapping);
    REQUIRE(anywhere.size() == 1);
    REQUIRE(anywhere[0]->faction.id == "xenoi");
    REQUIRE(servicesAnywhereRefuseniks({}).empty());
}

TEST_CASE("refuseniks form valid faction records")
{
    std::vector<Faction> factions;
    for (const RefusenikEntry& e : shipped()) {
        factions.push_back(e.faction);
    }
    std::vector<std::string> problems;
    std::vector<Faction> reparsed;
    REQUIRE(parseFactions("canon", factionsCanonicalJson(factions), reparsed, problems));
    REQUIRE(reparsed.size() == 2);
}

TEST_CASE("an unknown or empty refusenik id resolves to nothing")
{
    const auto mapping = shipped();
    REQUIRE(refusenikFor(mapping, "nobody") == nullptr);
    REQUIRE(refusenikFor(mapping, "") == nullptr);
    REQUIRE(refusenikFor({}, "xenoi") == nullptr);
}

TEST_CASE("refusenik output is sorted by id whatever the source order")
{
    std::vector<RefusenikEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parseRefusenikMapping("t.json", "[" + entry("zeta") + "," + entry("alpha") + "]", out, problems));
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].faction.id == "alpha");
    REQUIRE(out[1].faction.id == "zeta");
}

TEST_CASE("refusenik mapping: malformed input is refused without crashing")
{
    REQUIRE(rejects("", "empty file"));
    REQUIRE(rejects("   \n", "empty file"));
    REQUIRE(rejects("[", "not valid JSON"));
    REQUIRE(rejects("{}", "expected an array"));
    REQUIRE(rejects("[1]", "[0]: expected an object"));
    for (const char* key : {"id", "name", "placement", "claims_territory", "offers_services_anywhere", "summary"}) {
        REQUIRE(rejects("[{}]", std::string("[0].") + key + ": missing"));
    }
    REQUIRE(rejects("[" + entry("a", "The A", R"(,"x":1)") + "]", "[0].x: unknown key"));
    REQUIRE(rejects(R"([{"id":5,"name":"A","placement":"routes","claims_territory":false,"offers_services_anywhere":false,"summary":"s"}])", "[0].id: expected a string"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","placement":"routes","claims_territory":"no","offers_services_anywhere":false,"summary":"s"}])", "[0].claims_territory: expected a boolean"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","placement":"routes","claims_territory":false,"offers_services_anywhere":1,"summary":"s"}])", "[0].offers_services_anywhere: expected a boolean"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","placement":"sea","claims_territory":false,"offers_services_anywhere":false,"summary":"s"}])", "[0].placement: expected"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","placement":3,"claims_territory":false,"offers_services_anywhere":false,"summary":"s"}])", "[0].placement: expected a string"));
    REQUIRE(rejects("[" + entry("Bad Id") + "]", "[0].id"));
    REQUIRE(rejects("[" + entry("") + "]", "[0].id"));
    REQUIRE(rejects("[" + entry("a", "  ") + "]", "[0].name: must not be empty"));
    REQUIRE(rejects("[" + entry("a", "The A \xE2\x80\x94 B") + "]", "em-dash"));
    REQUIRE(rejects("[" + entry("a", "A\\nB") + "]", "control characters"));
    REQUIRE(rejects(R"([{"id":"a","id":"b","name":"A","placement":"routes","claims_territory":false,"offers_services_anywhere":false,"summary":"s"}])", "duplicate key"));
    REQUIRE(rejects("[" + entry("a") + "," + entry("a") + "]", "[1].id: duplicate"));
    REQUIRE(rejects("[" + entry(std::string(65, 'a')) + "]", "[0].id: longer than"));
    REQUIRE(rejects("[" + entry("a", std::string(121, 'n')) + "]", "[0].name: longer than"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","placement":"routes","claims_territory":false,"offers_services_anywhere":false,"summary":""}])", "[0].summary: must not be empty"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","placement":"routes","claims_territory":false,"offers_services_anywhere":false,"summary":")" + std::string(401, 'x') + R"("}])", "[0].summary: longer than"));
    REQUIRE(rejects(R"([{"id":"a","name":"A","placement":"routes","claims_territory":false,"offers_services_anywhere":false,"summary":"d — e"}])", "[0].summary: must not contain an em-dash"));
}

TEST_CASE("refusenik mapping: hostile bytes are bounded")
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

TEST_CASE("refusenik mapping: a failed parse leaves the output untouched")
{
    std::vector<RefusenikEntry> out(1);
    out[0].faction.id = "keep";
    std::vector<std::string> problems;
    REQUIRE_FALSE(parseRefusenikMapping("t.json", "[{}]", out, problems));
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].faction.id == "keep");
}

TEST_CASE("refusenik mapping: an empty array is valid")
{
    std::vector<RefusenikEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parseRefusenikMapping("t.json", "[]", out, problems));
    REQUIRE(out.empty());
}
