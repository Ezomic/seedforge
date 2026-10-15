#include "ichor/factions/faction_names.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<std::string> kNouns = { "salt", "ash", "bronze", "oath", "tide", "ichor", "laurel", "ember" };

std::vector<Faction> sixFactions()
{
    std::vector<Faction> out;
    const FactionRole roles[] = { FactionRole::Incumbent, FactionRole::Challenger, FactionRole::Parasite, FactionRole::Custodian, FactionRole::Refusenik, FactionRole::Herald };
    for (std::size_t i = 0; i < 6; ++i) {
        out.push_back(Faction { "f" + std::to_string(i), "", roles[i] });
    }
    return out;
}

}

TEST_CASE("cleanNameNoun normalises and rejects hostile bytes", "[faction_names]")
{
    CHECK(cleanNameNoun("salt") == "Salt");
    CHECK(cleanNameNoun("  lamp_oil ") == "Lamp Oil");
    CHECK(cleanNameNoun("") == "");
    CHECK(cleanNameNoun("\x01\x02\xff\xfe") == "");
    CHECK(cleanNameNoun("a\"<b>\n") == "Ab");
    CHECK(cleanNameNoun(std::string(500, 'x')).size() == kMaxNameNounLength);
    CHECK(cleanNameNoun(std::string("sa\0lt", 5)) == "Salt");
}

TEST_CASE("composeFactionName is deterministic", "[faction_names]")
{
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        CHECK(composeFactionName(seed, "f0", FactionRole::Incumbent, kNouns) == composeFactionName(seed, "f0", FactionRole::Incumbent, kNouns));
    }
    CHECK(composeFactionName(7, "f0", FactionRole::Herald, kNouns) == "The Ichor Oracles");
}

TEST_CASE("composeFactionName uses a pool noun and varies with the seed", "[faction_names]")
{
    std::set<std::string> seen;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        const std::string name = composeFactionName(seed, "f0", FactionRole::Challenger, kNouns);
        bool usesNoun = false;
        for (const std::string& n : kNouns) {
            if (name.find(cleanNameNoun(n)) != std::string::npos) {
                usesNoun = true;
            }
        }
        CHECK(usesNoun);
        CHECK(name.rfind("The ", 0) == 0);
        seen.insert(name);
    }
    CHECK(seen.size() > 10);
}

TEST_CASE("composeFactionName empty and hostile inputs", "[faction_names]")
{
    const std::set<std::string> bare = { "The Assize", "The Throne", "The Synod", "The Council", "The Hegemony" };
    CHECK(bare.contains(composeFactionName(1, "f0", FactionRole::Incumbent, {})));
    CHECK(composeFactionName(1, "f0", FactionRole::Incumbent, { "", "\xff", "  " }) == composeFactionName(1, "f0", FactionRole::Incumbent, {}));
    CHECK(composeFactionName(1, "", FactionRole::Custodian, kNouns).empty() == false);

    std::vector<std::string> huge(100000, std::string(10000, 'q'));
    const std::string name = composeFactionName(1, std::string(100000, 'z'), FactionRole::Parasite, huge);
    CHECK(name.size() <= kMaxFactionNameLength);
    CHECK(name.find('\n') == std::string::npos);
}

TEST_CASE("composeFactionNames are unique across a batch of seeds", "[faction_names]")
{
    const std::vector<Faction> factions = sixFactions();
    for (std::uint64_t seed = 0; seed < 500; ++seed) {
        const std::vector<std::string> names = composeFactionNames(seed, factions, kNouns);
        REQUIRE(names.size() == factions.size());
        CHECK(std::set<std::string>(names.begin(), names.end()).size() == names.size());
        CHECK(names == composeFactionNames(seed, factions, kNouns));
    }
}

TEST_CASE("composeFactionNames stay unique with one noun and with none", "[faction_names]")
{
    std::vector<Faction> many;
    for (int i = 0; i < 40; ++i) {
        many.push_back(Faction { "f" + std::to_string(i), "", FactionRole::Challenger });
    }
    for (const std::vector<std::string>& pool : { std::vector<std::string> { "salt" }, std::vector<std::string> {} }) {
        const std::vector<std::string> names = composeFactionNames(3, many, pool);
        REQUIRE(names.size() == many.size());
        CHECK(std::set<std::string>(names.begin(), names.end()).size() == names.size());
        for (const std::string& n : names) {
            CHECK(n.size() <= kMaxFactionNameLength);
        }
    }
    CHECK(composeFactionNames(3, {}, kNouns).empty());
}

TEST_CASE("composeFactionNames depends on the faction id, not its position", "[faction_names]")
{
    std::vector<Faction> a = sixFactions();
    std::vector<Faction> b = a;
    std::swap(b[0], b[5]);
    const auto namesA = composeFactionNames(11, a, kNouns);
    const auto namesB = composeFactionNames(11, b, kNouns);
    CHECK(namesA[0] == composeFactionName(11, "f0", FactionRole::Incumbent, kNouns));
    CHECK(namesB[5] == namesA[0]);
}
