#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"
#include "ichor/economy/law_currency_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace ichor;

namespace {

struct Loaded {
    DataSet data;
    AxiomSet axioms;
    std::vector<Synergy> defs;
    std::vector<std::string> problems;
};

void load(Loaded& l)
{
    const std::filesystem::path root = ICHOR_GAME_DATA_DIR;
    REQUIRE(l.data.loadDirectory(root));
    REQUIRE(l.axioms.loadDirectory(root));
    const bool ok = loadSynergyContentDirectory(root / "synergies", axiomPool(l.axioms), l.defs, l.problems);
    for (const std::string& problem : l.problems) {
        UNSCOPED_INFO(problem);
    }
    REQUIRE(ok);
}

const Synergy* find(const std::vector<Synergy>& defs, const std::string& id)
{
    const auto it = std::find_if(defs.begin(), defs.end(), [&](const Synergy& s) { return s.id == id; });
    return it == defs.end() ? nullptr : &*it;
}

// The price of a base-100 good after `months`, under the Law of a world whose fired synergies
// include one with the inflating_prices mechanic. A world without such a synergy gets the
// plain Law policy, so removing the synergy file removes the climb this measures.
std::uint64_t synergyPrice(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs, std::int64_t months)
{
    const std::vector<std::string> fired = detectSynergies(axiomIds, defs);
    const bool inflating = std::any_of(fired.begin(), fired.end(), [&](const std::string& id) {
        const Synergy* s = find(defs, id);
        return s != nullptr && s->mechanic == "inflating_prices";
    });
    LawCurrencyPolicy policy = lawCurrencyPolicy("law.last_favour");
    if (!inflating) {
        policy.monthlyPriceRisePermille = 0;
    }
    return lawPrice(policy, 100, months);
}

}

TEST_CASE("the Last Favour + League flagship loads through the real loader as tier 1", "[synergy_favour_league]")
{
    Loaded l;
    load(l);
    const Synergy* s = find(l.defs, "favour_league");
    REQUIRE(s != nullptr);
    CHECK(s->tier == SynergyTier::LawPowers);
    CHECK(s->contentTier == ContentTier::Flagship);
    CHECK(s->faction == "thesauroi");
    CHECK(s->dungeonType == "sealed_treasury");
    CHECK(s->mechanic == "inflating_prices");
    CHECK(s->worldName == "The Cornered Favour");
    CHECK(s->axioms == std::vector<std::string>{"law.last_favour", "powers.league"});
}

TEST_CASE("the Last Favour + League flagship has no dangling reference", "[synergy_favour_league]")
{
    Loaded l;
    load(l);
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("the Last Favour + League flagship fires for its pair and for nothing less", "[synergy_favour_league]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies({"law.last_favour", "powers.league"}, l.defs);
    CHECK(std::find(fired.begin(), fired.end(), "favour_league") != fired.end());
    const SynergyOverride result = applyOverrides(fired, l.defs, {});
    CHECK(std::find(result.factions.begin(), result.factions.end(), "thesauroi") != result.factions.end());
    CHECK(result.worldName == "The Cornered Favour");

    for (const std::vector<std::string>& partial : {std::vector<std::string>{"law.last_favour"}, std::vector<std::string>{"powers.league"}, std::vector<std::string>{"law.last_favour", "powers.amphictyony"}, std::vector<std::string>{"law.hecatomb", "powers.league"}}) {
        const auto none = detectSynergies(partial, l.defs);
        CHECK(std::find(none.begin(), none.end(), "favour_league") == none.end());
    }
}

TEST_CASE("the Last Favour + League makes prices climb over campaign time", "[synergy_favour_league]")
{
    Loaded l;
    load(l);
    const std::vector<std::string> world = {"law.last_favour", "powers.league"};
    const std::uint64_t act1 = synergyPrice(world, l.defs, 1);
    const std::uint64_t act3 = synergyPrice(world, l.defs, 36);
    CHECK(act1 == 105);
    CHECK(act3 >= 2 * act1);
    CHECK(act3 > synergyPrice(world, l.defs, 12));
    CHECK(synergyPrice(world, l.defs, 12) > act1);

    const std::vector<std::string> noSynergy = {"law.last_favour", "powers.amphictyony"};
    CHECK(synergyPrice(noSynergy, l.defs, 36) == 100);

    std::vector<Synergy> removed;
    std::copy_if(l.defs.begin(), l.defs.end(), std::back_inserter(removed), [](const Synergy& s) { return s.id != "favour_league"; });
    CHECK(synergyPrice(world, removed, 36) == 100);
}

TEST_CASE("the Last Favour + League price climb is bounded for hostile month counts", "[synergy_favour_league]")
{
    Loaded l;
    load(l);
    const std::vector<std::string> world = {"law.last_favour", "powers.league"};
    CHECK(synergyPrice(world, l.defs, -5) == 100);
    CHECK(synergyPrice(world, l.defs, INT64_MAX) == synergyPrice(world, l.defs, kMaxPriceMonths));
}
