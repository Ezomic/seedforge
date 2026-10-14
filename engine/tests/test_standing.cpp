#include "ichor/factions/standing.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<Faction> kFactions = {
    {"archonate", "The Archonate", FactionRole::Incumbent},
    {"archaists", "The Archaists", FactionRole::Challenger},
    {"mnemones", "The Mnemones", FactionRole::Custodian},
};

const std::string kTiersJson = R"([
    {"id": "hostile", "name": "Hostile", "threshold": -1000},
    {"id": "wary", "name": "Wary", "threshold": -250},
    {"id": "neutral", "name": "Neutral", "threshold": 0},
    {"id": "friendly", "name": "Friendly", "threshold": 250},
    {"id": "allied", "name": "Allied", "threshold": 750}
])";

const std::string kGatesJson = R"([
    {"id": "safe_passage", "faction": "archonate", "min_tier": "wary"},
    {"id": "trainer_basic", "faction": "archaists", "min_tier": "friendly"},
    {"id": "base_access", "faction": "mnemones", "min_tier": "allied"}
])";

std::vector<StandingTier> tiers()
{
    std::vector<StandingTier> out;
    std::vector<std::string> problems;
    REQUIRE(parseStandingTiers("t.json", kTiersJson, out, problems));
    return out;
}

std::vector<StandingGate> gates()
{
    std::vector<StandingGate> out;
    std::vector<std::string> problems;
    REQUIRE(parseStandingGates("g.json", kGatesJson, tiers(), kFactions, out, problems));
    return out;
}

FactionStanding make() { return FactionStanding(kFactions, tiers(), gates()); }

std::vector<std::string> tierProblems(const std::string& text)
{
    std::vector<StandingTier> out;
    std::vector<std::string> problems;
    const bool ok = parseStandingTiers("t.json", text, out, problems);
    CHECK(ok == problems.empty());
    return problems;
}

std::vector<std::string> gateProblems(const std::string& text)
{
    std::vector<StandingGate> out;
    std::vector<std::string> problems;
    const bool ok = parseStandingGates("g.json", text, tiers(), kFactions, out, problems);
    CHECK(ok == problems.empty());
    return problems;
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle)
{
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(needle) != std::string::npos; });
}

}

TEST_CASE("a new table starts every faction at zero, neutral", "[standing]")
{
    const auto standing = make();
    for (const Faction& faction : kFactions) {
        CHECK(standing.standing(faction.id) == 0);
        REQUIRE(standing.tier(faction.id) != nullptr);
        CHECK(standing.tier(faction.id)->id == "neutral");
    }
}

TEST_CASE("standing goes negative and the tier follows", "[standing]")
{
    auto standing = make();
    const auto change = standing.change("archonate", -100);
    REQUIRE(change.has_value());
    CHECK(change->oldValue == 0);
    CHECK(change->newValue == -100);
    CHECK(change->faction == "archonate");
    CHECK(standing.standing("archonate") == -100);
    CHECK(standing.tier("archonate")->id == "wary");
    standing.change("archonate", -200);
    CHECK(standing.tier("archonate")->id == "hostile");
    CHECK(standing.standing("archaists") == 0);
}

TEST_CASE("standing saturates at both ends", "[standing]")
{
    auto standing = make();
    auto up = standing.change("archaists", 600);
    CHECK(up->newValue == 600);
    up = standing.change("archaists", 600);
    CHECK(up->oldValue == 600);
    CHECK(up->newValue == kStandingMax);
    up = standing.change("archaists", 1);
    CHECK(up->oldValue == kStandingMax);
    CHECK(up->newValue == kStandingMax);
    CHECK(standing.tier("archaists")->id == "allied");

    auto down = standing.change("archonate", -kStandingMax);
    CHECK(down->newValue == -1000);
    down = standing.change("archonate", -1);
    CHECK(down->newValue == kStandingMin);
    CHECK(standing.tier("archonate")->id == "hostile");
}

TEST_CASE("extreme deltas do not overflow", "[standing]")
{
    auto standing = make();
    CHECK(standing.change("archonate", std::numeric_limits<std::int32_t>::max())->newValue == kStandingMax);
    CHECK(standing.change("archonate", std::numeric_limits<std::int32_t>::min())->newValue == kStandingMin);
    CHECK(standing.change("archonate", std::numeric_limits<std::int32_t>::min())->newValue == kStandingMin);
    CHECK(standing.change("archonate", std::numeric_limits<std::int32_t>::max())->newValue == kStandingMax);
}

TEST_CASE("tier boundaries sit exactly on the thresholds", "[standing]")
{
    auto standing = make();
    standing.change("archaists", 249);
    CHECK(standing.tier("archaists")->id == "neutral");
    standing.change("archaists", 1);
    CHECK(standing.tier("archaists")->id == "friendly");
    standing.change("archaists", -1);
    CHECK(standing.tier("archaists")->id == "neutral");
    standing.change("archaists", -249);
    CHECK(standing.standing("archaists") == 0);
    CHECK(standing.tier("archaists")->id == "neutral");
    standing.change("archaists", -1);
    CHECK(standing.tier("archaists")->id == "wary");
}

TEST_CASE("a gate flips exactly at its threshold", "[standing]")
{
    auto standing = make();
    CHECK(standing.allowed("safe_passage"));
    CHECK_FALSE(standing.allowed("trainer_basic"));

    standing.change("archaists", 249);
    CHECK_FALSE(standing.allowed("trainer_basic"));
    standing.change("archaists", 1);
    CHECK(standing.allowed("trainer_basic"));
    standing.change("archaists", -1);
    CHECK_FALSE(standing.allowed("trainer_basic"));

    standing.change("archonate", -250);
    CHECK(standing.allowed("safe_passage"));
    standing.change("archonate", -1);
    CHECK(standing.standing("archonate") == -251);
    CHECK_FALSE(standing.allowed("safe_passage"));
    standing.change("archonate", 1);
    CHECK(standing.allowed("safe_passage"));
}

TEST_CASE("unknown faction and gate ids fail closed", "[standing]")
{
    auto standing = make();
    CHECK_FALSE(standing.allowed("no_such_gate"));
    CHECK_FALSE(standing.allowed(""));
    CHECK_FALSE(standing.standing("nobody").has_value());
    CHECK(standing.tier("nobody") == nullptr);
    const auto before = standing.digest();
    CHECK_FALSE(standing.change("nobody", 10).has_value());
    CHECK(standing.digest() == before);
}

TEST_CASE("tiers must cover the range in ascending order", "[standing]")
{
    CHECK(tierProblems(kTiersJson).empty());
    CHECK_FALSE(tierProblems("[]").empty());
    CHECK(mentions(tierProblems(R"([{"id": "a", "name": "A", "threshold": -999}])"), "-1000"));
    CHECK(mentions(tierProblems(R"([{"id": "a", "name": "A", "threshold": -1000}, {"id": "b", "name": "B", "threshold": -1000}])"), "ascending"));
    CHECK(mentions(tierProblems(R"([{"id": "a", "name": "A", "threshold": -1000}, {"id": "b", "name": "B", "threshold": -2000}])"), "ascending"));
    CHECK(mentions(tierProblems(R"([{"id": "a", "name": "A", "threshold": -1000}, {"id": "b", "name": "B", "threshold": 1001}])"), "[1].threshold"));
    CHECK(mentions(tierProblems(R"([{"id": "a", "name": "A", "threshold": -1000}, {"id": "a", "name": "B", "threshold": 0}])"), "duplicate"));
    CHECK(mentions(tierProblems(R"([{"id": "a", "name": "A", "threshold": 1.5}])"), "[0].threshold"));
    CHECK(mentions(tierProblems(R"([{"id": "a", "name": "A", "threshold": "x"}])"), "[0].threshold"));
}

TEST_CASE("tier documents report every problem with a path", "[standing]")
{
    const auto problems = tierProblems(R"([{"id": "A", "name": "", "threshold": null, "extra": 1}, 5, {}])");
    CHECK(mentions(problems, "t.json: [0].id"));
    CHECK(mentions(problems, "[0].name"));
    CHECK(mentions(problems, "[0].threshold"));
    CHECK(mentions(problems, "[0].extra: unknown key"));
    CHECK(mentions(problems, "[1]"));
    CHECK(mentions(problems, "[2].id: missing"));
    CHECK_FALSE(tierProblems("").empty());
    CHECK_FALSE(tierProblems("{}").empty());
    CHECK_FALSE(tierProblems(std::string(100000, '[')).empty());
    CHECK_FALSE(tierProblems(R"([{"id": "a", "id": "b", "name": "n", "threshold": -1000}])").empty());
}

TEST_CASE("tier names reject em-dashes and control characters", "[standing]")
{
    const auto named = [](const std::string& name) { return tierProblems(R"([{"id": "a", "name": ")" + name + R"(", "threshold": -1000}])"); };
    CHECK(named("Wary").empty());
    CHECK(mentions(named("a \xE2\x80\x94 b"), "em-dash"));
    CHECK(mentions(named("a\\nb"), "control"));
    CHECK(mentions(named("a\\u0000b"), "control"));
}

TEST_CASE("threshold bounds are inclusive", "[standing]")
{
    const auto two = [](const std::string& second) { return tierProblems(R"([{"id": "a", "name": "A", "threshold": -1000}, {"id": "b", "name": "B", "threshold": )" + second + "}]"); };
    CHECK(two("1000").empty());
    CHECK_FALSE(two("1001").empty());
    CHECK_FALSE(two("18446744073709551615").empty());
    CHECK_FALSE(two("-1001").empty());
}

TEST_CASE("a bad first threshold does not blame the next tier", "[standing]")
{
    const auto problems = tierProblems(R"([{"id": "a", "name": "A", "threshold": null}, {"id": "b", "name": "B", "threshold": -1000}])");
    CHECK_FALSE(mentions(problems, "[1]"));
}

TEST_CASE("the lowest tier gate is always open and the top tier gate flips at its threshold", "[standing]")
{
    std::vector<StandingGate> open = {{"open", "archonate", "hostile"}};
    FactionStanding low(kFactions, tiers(), open);
    low.change("archonate", -5000);
    CHECK(low.standing("archonate") == kStandingMin);
    CHECK(low.allowed("open"));

    auto standing = make();
    standing.change("mnemones", 749);
    CHECK_FALSE(standing.allowed("base_access"));
    standing.change("mnemones", 1);
    CHECK(standing.allowed("base_access"));
    standing.change("mnemones", 5000);
    CHECK(standing.allowed("base_access"));
    standing.change("mnemones", -5000);
    CHECK_FALSE(standing.allowed("base_access"));
    CHECK_FALSE(standing.allowed("trainer_basic"));
}

TEST_CASE("gates must name a known faction and tier", "[standing]")
{
    CHECK(gateProblems(kGatesJson).empty());
    CHECK(gateProblems("[]").empty());
    CHECK(mentions(gateProblems(R"([{"id": "g", "faction": "ghosts", "min_tier": "wary"}])"), "[0].faction"));
    CHECK(mentions(gateProblems(R"([{"id": "g", "faction": "archonate", "min_tier": "godlike"}])"), "[0].min_tier"));
    CHECK(mentions(gateProblems(R"([{"id": "g", "faction": "archonate", "min_tier": "wary"}, {"id": "g", "faction": "archonate", "min_tier": "wary"}])"), "duplicate"));
    CHECK(mentions(gateProblems(R"([{"id": "G", "faction": "archonate", "min_tier": "wary"}])"), "[0].id"));
    CHECK(mentions(gateProblems(R"([{}])"), "[0].faction: missing"));
}

TEST_CASE("a failed parse leaves the output untouched", "[standing]")
{
    std::vector<StandingTier> out = tiers();
    std::vector<std::string> problems;
    CHECK_FALSE(parseStandingTiers("t.json", "[{}]", out, problems));
    CHECK(out.size() == 5);
}

TEST_CASE("the digest follows the standing and nothing else", "[standing]")
{
    auto a = make();
    auto b = make();
    CHECK(a.digest() == b.digest());
    a.change("archonate", 40);
    CHECK(a.digest() != b.digest());
    b.change("archonate", 40);
    CHECK(a.digest() == b.digest());

    auto c = make();
    c.change("archonate", 100);
    c.change("archonate", -60);
    CHECK(c.digest() == a.digest());
}

TEST_CASE("the digest does not depend on faction order", "[standing]")
{
    auto reversed = kFactions;
    std::reverse(reversed.begin(), reversed.end());
    FactionStanding a(kFactions, tiers(), gates());
    FactionStanding b(reversed, tiers(), gates());
    CHECK(a.digest() == b.digest());
    a.change("mnemones", -77);
    b.change("mnemones", -77);
    CHECK(a.serialise() == b.serialise());
}

TEST_CASE("standing round-trips through binary", "[standing]")
{
    auto standing = make();
    standing.change("archonate", -640);
    standing.change("archaists", 5000);
    const auto bytes = standing.serialise();

    auto loaded = make();
    REQUIRE(loaded.deserialise(bytes));
    CHECK(loaded.standing("archonate") == -640);
    CHECK(loaded.standing("archaists") == kStandingMax);
    CHECK(loaded.standing("mnemones") == 0);
    CHECK(loaded.digest() == standing.digest());
    CHECK(loaded.serialise() == bytes);
}

TEST_CASE("bad binary is refused and leaves the table untouched", "[standing]")
{
    auto standing = make();
    standing.change("archonate", 123);
    const auto good = standing.serialise();
    const auto before = standing.digest();

    CHECK_FALSE(standing.deserialise({}));
    for (std::size_t cut = 0; cut < good.size(); ++cut) {
        CHECK_FALSE(standing.deserialise(std::vector<std::uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut))));
    }
    auto trailing = good;
    trailing.push_back(0);
    CHECK_FALSE(standing.deserialise(trailing));

    auto badVersion = good;
    badVersion[0] ^= 0xFF;
    CHECK_FALSE(standing.deserialise(badVersion));

    CHECK(standing.digest() == before);
}

TEST_CASE("binary with out of range values or foreign factions is refused", "[standing]")
{
    auto standing = make();
    const auto good = standing.serialise();

    auto high = good;
    high[high.size() - 1] = 0x7F;
    CHECK_FALSE(standing.deserialise(high));

    auto low = good;
    low[low.size() - 1] = 0x80;
    CHECK_FALSE(standing.deserialise(low));

    standing.change("archaists", 10);
    auto bytes = make().serialise();
    bytes[bytes.size() - 1] = 0x7F;
    CHECK_FALSE(standing.deserialise(bytes));
    CHECK(standing.standing("archaists") == 10);
    standing.change("archaists", -10);

    const std::vector<Faction> others ={{"zzz", "Z", FactionRole::Herald}};
    FactionStanding other(others, tiers(), {});
    CHECK_FALSE(standing.deserialise(other.serialise()));
    CHECK_FALSE(other.deserialise(good));
    CHECK(standing.standing("archonate") == 0);
}
