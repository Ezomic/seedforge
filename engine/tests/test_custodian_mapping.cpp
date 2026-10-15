#include "ichor/data/axiom.h"
#include "ichor/factions/custodian_mapping.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

bool parse(const std::string& text, std::vector<CustodianEntry>& out, std::vector<std::string>& problems)
{
    return parseCustodianMapping("custodians", text, out, problems);
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
    std::ifstream in(std::string(ICHOR_REPO_DATA) + "/factions/custodians.json", std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const char* const kTwo = R"([
    {"id": "b", "name": "B", "reward": "map_knowledge", "cataclysms": ["cataclysm.zeta"]},
    {"id": "a", "name": "A", "reward": "secret", "cataclysms": ["cataclysm.alpha", "cataclysm.beta"]}])";

}

TEST_CASE("a valid mapping parses sorted by id and resolves Cataclysms", "[custodian]")
{
    std::vector<CustodianEntry> out;
    std::vector<std::string> problems;
    REQUIRE(parse(kTwo, out, problems));
    CHECK(problems.empty());
    REQUIRE(out.size() == 2);
    CHECK(out[0].faction.id == "a");
    CHECK(out[0].faction.role == FactionRole::Custodian);
    CHECK(out[0].reward == CustodianReward::Secret);
    CHECK(out[1].reward == CustodianReward::MapKnowledge);
    const CustodianEntry* beta = custodianFor(out, "cataclysm.beta");
    REQUIRE(beta != nullptr);
    CHECK(beta->faction.id == "a");
    CHECK(custodianFor(out, "cataclysm.zeta")->faction.id == "b");
    CHECK(custodianFor(out, "cataclysm.missing") == nullptr);
    CHECK(custodianFor(out, "") == nullptr);
    CHECK(custodianFor({}, "cataclysm.alpha") == nullptr);
}

TEST_CASE("reward names are the JSON spellings", "[custodian]")
{
    CHECK(custodianRewardName(CustodianReward::Secret) == "secret");
    CHECK(custodianRewardName(CustodianReward::MapKnowledge) == "map_knowledge");
}

TEST_CASE("bad mappings are refused with a path and leave the output empty", "[custodian]")
{
    std::vector<CustodianEntry> out;
    std::vector<std::string> problems;
    SECTION("not an array, empty file, empty array")
    {
        CHECK_FALSE(parse("{}", out, problems));
        CHECK(hasProblem(problems, "expected an array"));
        problems.clear();
        CHECK_FALSE(parse("   ", out, problems));
        CHECK(hasProblem(problems, "empty file"));
        problems.clear();
        CHECK_FALSE(parse("[]", out, problems));
        CHECK(hasProblem(problems, "no entries"));
    }
    SECTION("duplicate custodian ids and a Cataclysm claimed twice")
    {
        CHECK_FALSE(parse(R"([
            {"id": "a", "name": "A", "reward": "secret", "cataclysms": ["cataclysm.x"]},
            {"id": "a", "name": "A2", "reward": "secret", "cataclysms": ["cataclysm.y"]},
            {"id": "c", "name": "C", "reward": "secret", "cataclysms": ["cataclysm.x", "cataclysm.x"]}])", out, problems));
        CHECK(hasProblem(problems, "[1].id: duplicate"));
        CHECK(hasProblem(problems, "[2].cataclysms[0]: duplicate"));
        CHECK(hasProblem(problems, "[2].cataclysms[1]: duplicate"));
    }
    SECTION("missing, wrong typed, unknown and malformed fields")
    {
        CHECK_FALSE(parse(R"([5, {"id": 3, "name": "x", "reward": "gold"},
            {"id": "Bad Id", "name": "  ", "reward": "secret", "cataclysms": ["flood", 4, []], "extra": 1},
            {"id": "d", "name": "D", "reward": 1, "cataclysms": "cataclysm.x"},
            {"id": "e", "name": "E", "reward": "secret", "cataclysms": []}])", out, problems));
        CHECK(hasProblem(problems, "[0]: expected an object"));
        CHECK(hasProblem(problems, "[1].id: expected a string"));
        CHECK(hasProblem(problems, "[1].reward: expected \"secret\" or \"map_knowledge\""));
        CHECK(hasProblem(problems, "[1].cataclysms: missing"));
        CHECK(hasProblem(problems, "[2].id: expected lowercase"));
        CHECK(hasProblem(problems, "[2].name: must not be empty"));
        CHECK(hasProblem(problems, "[2].cataclysms[0]: expected a Cataclysm axiom id"));
        CHECK(hasProblem(problems, "[2].cataclysms[1]: expected a string"));
        CHECK(hasProblem(problems, "[2].cataclysms[2]: expected a string"));
        CHECK(hasProblem(problems, "[2].extra: unknown key"));
        CHECK(hasProblem(problems, "[3].reward: expected a string"));
        CHECK(hasProblem(problems, "[3].cataclysms: expected an array"));
        CHECK(hasProblem(problems, "[4].cataclysms: must not be empty"));
    }
    SECTION("em-dash and oversize names")
    {
        CHECK_FALSE(parse("[{\"id\": \"a\", \"name\": \"A \xE2\x80\x94 B\", \"reward\": \"secret\", \"cataclysms\": [\"cataclysm.x\"]}]", out, problems));
        CHECK_FALSE(parse(std::string("[{\"id\": \"a\", \"name\": \"") + std::string(500, 'n') + "\", \"reward\": \"secret\", \"cataclysms\": [\"cataclysm.x\"]}]", out, problems));
        CHECK(problems.size() == 2);
    }
    SECTION("duplicate keys")
    {
        CHECK_FALSE(parse(R"([{"id": "a", "id": "b", "name": "A", "reward": "secret", "cataclysms": ["cataclysm.x"]}])", out, problems));
        CHECK(hasProblem(problems, "duplicate key"));
    }
    CHECK(out.empty());
}

TEST_CASE("hostile bytes never crash or allocate unbounded memory", "[custodian]")
{
    std::vector<CustodianEntry> out;
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
    withNul += "b\",\"name\":\"n\",\"reward\":\"secret\",\"cataclysms\":[\"cataclysm.x\"]}]";
    refused(withNul, "not valid JSON");
    std::string many = "[";
    for (int i = 0; i < 65; ++i) {
        many += (i ? "," : "") + std::string("{\"id\":\"c") + std::to_string(i) + "\",\"name\":\"n\",\"reward\":\"secret\",\"cataclysms\":[\"cataclysm.p" + std::to_string(i) + "\"]}";
    }
    refused(many + "]", "more than 64");
    std::string longList = R"([{"id":"a","name":"n","reward":"secret","cataclysms":[)";
    for (int i = 0; i < 65; ++i) {
        longList += (i ? "," : "") + std::string("\"cataclysm.p") + std::to_string(i) + "\"";
    }
    refused(longList + "]}]", "more than 64");
    CHECK(out.empty());
}

TEST_CASE("the order of the source does not change the result", "[custodian]")
{
    std::vector<CustodianEntry> a;
    std::vector<CustodianEntry> b;
    std::vector<std::string> problems;
    REQUIRE(parse(kTwo, a, problems));
    REQUIRE(parse(R"([
        {"id": "a", "name": "A", "reward": "secret", "cataclysms": ["cataclysm.alpha", "cataclysm.beta"]},
        {"id": "b", "name": "B", "reward": "map_knowledge", "cataclysms": ["cataclysm.zeta"]}])", b, problems));
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].faction.id == b[i].faction.id);
        CHECK(a[i].cataclysms == b[i].cataclysms);
    }
}

TEST_CASE("coverage names every Cataclysm no Custodian claims", "[custodian]")
{
    std::vector<CustodianEntry> mapping;
    std::vector<std::string> problems;
    REQUIRE(parse(kTwo, mapping, problems));
    CHECK(custodianCoverageProblems(mapping, {"cataclysm.alpha", "cataclysm.zeta"}).empty());
    const auto missing = custodianCoverageProblems(mapping, {"cataclysm.alpha", "cataclysm.nope"});
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].find("cataclysm.nope") != std::string::npos);
}

TEST_CASE("the shipped mapping gives every authored Cataclysm a Custodian", "[custodian]")
{
    std::vector<CustodianEntry> mapping;
    std::vector<std::string> problems;
    const bool ok = parse(shippedText(), mapping, problems);
    for (const std::string& problem : problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(ok);
    REQUIRE(mapping.size() == 2);
    const CustodianEntry* mnemones = custodianFor(mapping, "cataclysm.silence_of_the_oracles");
    const CustodianEntry* periegetes = custodianFor(mapping, "cataclysm.flood");
    REQUIRE(mnemones != nullptr);
    REQUIRE(periegetes != nullptr);
    CHECK(mnemones->faction.id == "mnemones");
    CHECK(mnemones->faction.name == "The Mnemones");
    CHECK(mnemones->reward == CustodianReward::Secret);
    CHECK(periegetes->faction.id == "periegetes");
    CHECK(periegetes->reward == CustodianReward::MapKnowledge);

    AxiomSet axioms;
    REQUIRE(axioms.loadDirectory(ICHOR_GAME_DATA_DIR));
    std::vector<std::string> cataclysms;
    for (const auto& [id, axiom] : axioms.axioms()) {
        if (axiom.slot == AxiomSlot::Cataclysm) {
            cataclysms.push_back(id);
        }
    }
    CHECK(cataclysms.size() >= 3);
    CHECK(custodianCoverageProblems(mapping, cataclysms).empty());
}
