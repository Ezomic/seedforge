#include "ichor/data/npc_tiers.h"
#include "ichor/npc/regular_generation.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

TraitPool testTraits()
{
    std::vector<Trait> traits;
    for (const char* id : {"bitter", "kind", "proud", "wary", "curious", "frugal"}) {
        traits.push_back({id, id, "d", {}});
    }
    std::string error;
    auto pool = TraitPool::fromTraits(std::move(traits), error);
    REQUIRE(pool.has_value());
    return *pool;
}

std::vector<Desire> testDesires()
{
    return {
        {"want_grain", DesireKind::Want, "grain", 7, {"grain", CompareOp::Lt, 40}},
        {"want_trade", DesireKind::Want, "trade", 4, {"blockaded", CompareOp::Eq, 1}},
        {"fear_flood", DesireKind::Fear, "flood", 8, {"sea", CompareOp::Gt, 3}},
        {"fear_famine", DesireKind::Fear, "famine", 6, {"grain", CompareOp::Lt, 20}},
    };
}

std::vector<std::string> testNames(int count = 120)
{
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
        names.push_back("Name" + std::to_string(i));
    }
    return names;
}

SettlementEconomy town(const std::string& id, std::uint32_t population, std::vector<EconomyGood> goods = {})
{
    SettlementEconomy s;
    s.id = id;
    s.population = population;
    s.goods = std::move(goods);
    return s;
}

std::vector<EconomicRole> testRoles()
{
    return {{"salt_panner", "salt", 3}, {"fisher", "fish", 1}, {"porter", "", 1}};
}

struct Fixture {
    TraitPool pool = testTraits();
    RegularInputs inputs;

    Fixture()
    {
        inputs.settlements = {town("saltmere", 300, {{"salt", 8}, {"fish", 2}}), town("harl", 100, {{"fish", 5}}), town("dune", 50)};
        inputs.roles = testRoles();
        inputs.names = testNames();
        inputs.desires = testDesires();
        inputs.traits = &pool;
    }
};

std::map<std::string, std::size_t> roleCounts(const std::vector<Regular>& regulars, const std::string& home)
{
    std::map<std::string, std::size_t> out;
    for (const Regular& r : regulars) {
        if (r.home == home) {
            ++out[r.role];
        }
    }
    return out;
}

}

TEST_CASE("regulars fill the doc 10 range and survive a round trip through the npc file format", "[npc][regular]")
{
    Fixture f;
    for (std::uint64_t seed : {1ull, 2ull, 77ull, 123456789ull}) {
        const auto regulars = generateRegulars(seed, f.inputs);
        REQUIRE(regulars.size() >= kMinRegulars);
        REQUIRE(regulars.size() <= kMaxRegulars);

        std::string json = "[";
        for (std::size_t i = 0; i < regulars.size(); ++i) {
            json += (i ? "," : "") + npcCanonicalJson(NpcRecord{regulars[i]});
        }
        json += "]";
        std::vector<NpcRecord> parsed;
        std::vector<std::string> problems;
        const bool ok = parseNpcFile("generated", json, parsed, problems);
        INFO((problems.empty() ? std::string() : problems.front()));
        REQUIRE(ok);
        REQUIRE(parsed.size() == regulars.size());
    }
}

TEST_CASE("an economic role exists only where the settlement's goods support it", "[npc][regular]")
{
    Fixture f;
    const auto regulars = generateRegulars(9, f.inputs);

    const auto dune = roleCounts(regulars, "dune");
    REQUIRE_FALSE(dune.empty());
    REQUIRE(dune.size() == 1);
    REQUIRE(dune.count("porter") == 1);

    const auto harl = roleCounts(regulars, "harl");
    REQUIRE(harl.count("salt_panner") == 0);
    REQUIRE(harl.count("fisher") == 1);

    REQUIRE(roleCounts(regulars, "saltmere").count("salt_panner") == 1);
}

TEST_CASE("removing the salt removes the salt-panners", "[npc][regular]")
{
    Fixture f;
    const auto before = roleCounts(generateRegulars(5, f.inputs), "saltmere");
    REQUIRE(before.count("salt_panner") == 1);
    const std::size_t withSalt = before.at("salt_panner");
    REQUIRE(withSalt >= 3);

    f.inputs.settlements[0].goods = {{"fish", 2}};
    const auto after = roleCounts(generateRegulars(5, f.inputs), "saltmere");
    REQUIRE(after.count("salt_panner") == 0);

    f.inputs.settlements[0].goods = {{"salt", 2}};
    REQUIRE(roleCounts(generateRegulars(5, f.inputs), "saltmere").count("salt_panner") == 0);
}

TEST_CASE("a more abundant good yields more of its role than a barely sufficient one", "[npc][regular]")
{
    Fixture f;
    f.inputs.roles = {{"salt_panner", "salt", 1}, {"porter", "", 1}};
    std::size_t rich = 0;
    std::size_t poor = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        f.inputs.settlements = {town("only", 1, {{"salt", 100}})};
        const auto a = roleCounts(generateRegulars(seed, f.inputs), "only");
        rich += a.count("salt_panner") != 0 ? a.at("salt_panner") : 0;
        f.inputs.settlements = {town("only", 1, {{"salt", 1}})};
        const auto b = roleCounts(generateRegulars(seed, f.inputs), "only");
        poor += b.count("salt_panner") != 0 ? b.at("salt_panner") : 0;
    }
    REQUIRE(rich * 2 > poor * 3);
}

TEST_CASE("one abundant good does not make every Regular the same job", "[npc][regular]")
{
    Fixture f;
    f.inputs.settlements = {town("only", 1, {{"salt", 100}, {"fish", 100}})};
    const auto counts = roleCounts(generateRegulars(3, f.inputs), "only");
    REQUIRE(counts.size() == 3);
    for (const auto& [role, n] : counts) {
        REQUIRE(n < kMinRegulars);
    }
}

TEST_CASE("regulars are valid, unique and spread by population", "[npc][regular]")
{
    Fixture f;
    const auto regulars = generateRegulars(11, f.inputs);
    std::set<std::string> ids;
    std::set<std::string> names;
    std::map<std::string, std::size_t> perTown;
    for (const Regular& r : regulars) {
        REQUIRE(ids.insert(r.id).second);
        REQUIRE(names.insert(r.name).second);
        REQUIRE(r.traits.size() >= kMinNpcTraits);
        REQUIRE(r.traits.size() <= kMaxNpcTraits);
        REQUIRE(r.relationships.size() >= kMinRelationships);
        REQUIRE(r.relationships.size() <= kMaxRelationships);
        REQUIRE(r.memory == "ledger_" + r.id);
        REQUIRE_FALSE(r.want.empty());
        REQUIRE_FALSE(r.fear.empty());
        for (const Relationship& rel : r.relationships) {
            REQUIRE(rel.target != r.id);
            REQUIRE(std::any_of(regulars.begin(), regulars.end(), [&](const Regular& o) { return o.id == rel.target; }));
        }
        ++perTown[r.home];
    }
    REQUIRE(std::is_sorted(regulars.begin(), regulars.end(), [](const Regular& l, const Regular& r) { return l.id < r.id; }));
    REQUIRE(perTown["saltmere"] > perTown["harl"]);
    REQUIRE(perTown["harl"] > perTown["dune"]);
    REQUIRE(perTown["dune"] > 0);
}

TEST_CASE("wants and fears follow the settlement's live state", "[npc][regular]")
{
    Fixture f;
    f.inputs.settlements = {town("dry", 1, {{"fish", 3}}), town("flooded", 1, {{"fish", 3}}), town("calm", 1, {{"fish", 3}})};
    f.inputs.settlements[0].world = {{"grain", 5}, {"sea", 0}};
    f.inputs.settlements[1].world = {{"grain", 90}, {"sea", 9}};
    f.inputs.settlements[2].world = {{"grain", 90}, {"sea", 0}};
    std::set<std::string> calmWants;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        for (const Regular& r : generateRegulars(seed, f.inputs)) {
            if (r.home == "dry") {
                REQUIRE(r.want == "want_grain");
                REQUIRE(r.fear == "fear_famine");
            } else if (r.home == "flooded") {
                REQUIRE(r.fear == "fear_flood");
            } else {
                calmWants.insert(r.want);
                REQUIRE((r.fear == "fear_flood" || r.fear == "fear_famine"));
            }
        }
    }
    REQUIRE(calmWants.size() > 1);
}

TEST_CASE("generation is deterministic and independent of input order", "[npc][regular]")
{
    Fixture f;
    const auto a = generateRegulars(42, f.inputs);
    const auto again = generateRegulars(42, f.inputs);
    REQUIRE(again.size() == a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(npcCanonicalJson(NpcRecord{a[i]}) == npcCanonicalJson(NpcRecord{again[i]}));
    }

    Fixture g;
    std::reverse(g.inputs.settlements.begin(), g.inputs.settlements.end());
    std::reverse(g.inputs.roles.begin(), g.inputs.roles.end());
    std::reverse(g.inputs.names.begin(), g.inputs.names.end());
    std::reverse(g.inputs.desires.begin(), g.inputs.desires.end());
    const auto b = generateRegulars(42, g.inputs);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(npcCanonicalJson(NpcRecord{a[i]}) == npcCanonicalJson(NpcRecord{b[i]}));
    }

    const auto c = generateRegulars(43, f.inputs);
    const bool differs = c.size() != a.size() || npcCanonicalJson(NpcRecord{c.front()}) != npcCanonicalJson(NpcRecord{a.front()});
    REQUIRE(differs);
}

TEST_CASE("regular generation rejects unusable input without crashing", "[npc][regular][adversarial]")
{
    Fixture f;

    SECTION("no settlements, roles, names, wants, fears or traits")
    {
        auto in = f.inputs;
        in.settlements.clear();
        REQUIRE(generateRegulars(1, in).empty());
        in = f.inputs;
        in.roles.clear();
        REQUIRE(generateRegulars(1, in).empty());
        in = f.inputs;
        in.names.clear();
        REQUIRE(generateRegulars(1, in).empty());
        in = f.inputs;
        in.desires.erase(std::remove_if(in.desires.begin(), in.desires.end(), [](const Desire& d) { return d.kind == DesireKind::Fear; }), in.desires.end());
        REQUIRE(generateRegulars(1, in).empty());
        in = f.inputs;
        in.desires.erase(std::remove_if(in.desires.begin(), in.desires.end(), [](const Desire& d) { return d.kind == DesireKind::Want; }), in.desires.end());
        REQUIRE(generateRegulars(1, in).empty());
        in = f.inputs;
        in.traits = nullptr;
        REQUIRE(generateRegulars(1, in).empty());
    }

    SECTION("no settlement supports any role")
    {
        f.inputs.roles = {{"salt_panner", "salt", 3}};
        f.inputs.settlements = {town("dune", 10)};
        REQUIRE(generateRegulars(1, f.inputs).empty());
    }

    SECTION("bad ids and names are dropped")
    {
        f.inputs.settlements.push_back(town("Bad Id", 1000));
        f.inputs.settlements.push_back(town("", 1000));
        f.inputs.settlements.push_back(town(std::string(200, 'a'), 1000));
        f.inputs.roles.push_back({"Not A Word", "", 1});
        f.inputs.roles.push_back({"", "", 1});
        f.inputs.names.push_back("");
        f.inputs.names.push_back("   ");
        f.inputs.names.push_back(std::string(500, 'x'));
        f.inputs.names.push_back("Dash \xE2\x80\x94 Name");
        for (const Regular& r : generateRegulars(1, f.inputs)) {
            REQUIRE((r.home == "saltmere" || r.home == "harl" || r.home == "dune"));
            REQUIRE(r.role != "Not A Word");
            REQUIRE(r.name.find("Dash") == std::string::npos);
            REQUIRE(r.name.size() <= 80);
        }
    }

    SECTION("conflicting duplicates are ambiguous and dropped, identical ones collapse")
    {
        const auto baseline = generateRegulars(1, f.inputs);
        f.inputs.settlements.push_back(f.inputs.settlements.front());
        REQUIRE(generateRegulars(1, f.inputs).size() == baseline.size());

        f.inputs.settlements.push_back(town("harl", 5, {{"salt", 9}}));
        for (const Regular& r : generateRegulars(1, f.inputs)) {
            REQUIRE(r.home != "harl");
        }
    }

    SECTION("extreme numbers are clamped")
    {
        const int big = std::numeric_limits<int>::max();
        const int small = std::numeric_limits<int>::min();
        f.inputs.settlements = {town("a", std::numeric_limits<std::uint32_t>::max(), {{"salt", big}, {"fish", small}}), town("b", 0, {{"salt", small}})};
        f.inputs.roles.push_back({"clerk", "paper", std::numeric_limits<int>::min()});
        f.inputs.roles.push_back({"scribe", "ink", big});
        f.inputs.desires.push_back({"want_big", DesireKind::Want, "x", std::numeric_limits<int>::max(), {"t", CompareOp::Eq, std::numeric_limits<std::int64_t>::min()}});
        f.inputs.desires.push_back({"want_neg", DesireKind::Want, "x", std::numeric_limits<int>::min(), {"t", CompareOp::Eq, 0}});
        const auto regulars = generateRegulars(1, f.inputs);
        REQUIRE(regulars.size() >= kMinRegulars);
        REQUIRE(regulars.size() <= kMaxRegulars);
    }

    SECTION("a short name pool is reused with a numeric suffix, names stay unique")
    {
        f.inputs.names = {"Ilse", "Harl", "Ilse 2", std::string(76, 'n')};
        const auto regulars = generateRegulars(1, f.inputs);
        std::set<std::string> names;
        for (const Regular& r : regulars) {
            REQUIRE(names.insert(r.name).second);
        }
        REQUIRE(names.size() == regulars.size());
        for (const Regular& r : regulars) {
            REQUIRE(r.name.size() <= 80);
        }
    }

    SECTION("a long settlement id still yields records that parse")
    {
        f.inputs.settlements = {town(std::string(49, 'a'), 1, {{"fish", 3}}), town(std::string(50, 'b'), 1, {{"fish", 3}})};
        const auto regulars = generateRegulars(1, f.inputs);
        REQUIRE_FALSE(regulars.empty());
        for (const Regular& r : regulars) {
            REQUIRE(r.id.size() <= 64);
            REQUIRE(r.memory.size() <= 64);
            REQUIRE(r.home.size() == 49);
        }
    }

    SECTION("oversized input is refused")
    {
        f.inputs.names.assign(10001, "x");
        REQUIRE(generateRegulars(1, f.inputs).empty());
    }

    SECTION("many settlements still give a bounded number of Regulars")
    {
        f.inputs.settlements.clear();
        for (int i = 0; i < 2000; ++i) {
            f.inputs.settlements.push_back(town("t" + std::to_string(i), static_cast<std::uint32_t>(i)));
        }
        const auto regulars = generateRegulars(1, f.inputs);
        REQUIRE(regulars.size() <= kMaxRegulars);
        REQUIRE(regulars.size() >= kMinRegulars);
    }
}
