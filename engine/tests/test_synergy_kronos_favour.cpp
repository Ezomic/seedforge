#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"
#include "ichor/economy/law_currency_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
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

bool conceals(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs)
{
    const std::vector<std::string> fired = detectSynergies(axiomIds, defs);
    return std::any_of(fired.begin(), fired.end(), [&](const std::string& id) {
        const Synergy* s = find(defs, id);
        return s != nullptr && s->mechanic == "concealed_shortage";
    });
}

// What the Last Favour really costs after `months`.
std::uint64_t truePrice(std::int64_t months)
{
    return lawPrice(lawCurrencyPolicy("law.last_favour"), 100, months);
}

// What the public is shown. A world with the concealed_shortage mechanic posts the base price
// while the real one climbs; any other world posts the truth. Removing the synergy file
// removes the concealment this measures.
std::uint64_t postedPrice(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs, std::int64_t months)
{
    return conceals(axiomIds, defs) ? 100 : truePrice(months);
}

std::uint64_t concealedGap(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs, std::int64_t months)
{
    return truePrice(months) - postedPrice(axiomIds, defs, months);
}

}

TEST_CASE("the Kronos + Last Favour flagship loads through the real loader as tier 1", "[synergy_kronos_favour]")
{
    Loaded l;
    load(l);
    const Synergy* s = find(l.defs, "kronos_favour");
    REQUIRE(s != nullptr);
    CHECK(s->tier == SynergyTier::CataclysmLaw);
    CHECK(s->contentTier == ContentTier::Flagship);
    CHECK(s->faction == "thesauroi");
    CHECK(s->dungeonType == "hidden_reckoning");
    CHECK(s->mechanic == "concealed_shortage");
    CHECK(s->worldName == "The Gilded Count");
    CHECK(s->axioms == std::vector<std::string>{"cataclysm.kronos", "law.last_favour"});
}

TEST_CASE("the Age of Kronos axiom is a cataclysm", "[synergy_kronos_favour]")
{
    Loaded l;
    load(l);
    const Axiom* kronos = l.axioms.find("cataclysm.kronos");
    REQUIRE(kronos != nullptr);
    CHECK(kronos->slot == AxiomSlot::Cataclysm);
    CHECK(kronos->title == "The Age of Kronos");
}

TEST_CASE("the Kronos + Last Favour flagship has no dangling reference", "[synergy_kronos_favour]")
{
    Loaded l;
    load(l);
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("the Kronos + Last Favour flagship fires for its pair and for nothing less", "[synergy_kronos_favour]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies({"cataclysm.kronos", "law.last_favour"}, l.defs);
    CHECK(std::find(fired.begin(), fired.end(), "kronos_favour") != fired.end());
    const SynergyOverride result = applyOverrides(fired, l.defs, {});
    CHECK(std::find(result.factions.begin(), result.factions.end(), "thesauroi") != result.factions.end());
    CHECK(result.worldName == "The Gilded Count");

    for (const std::vector<std::string>& partial : {std::vector<std::string>{"cataclysm.kronos"}, std::vector<std::string>{"law.last_favour"}, std::vector<std::string>{"cataclysm.kronos", "law.hecatomb"}, std::vector<std::string>{"cataclysm.flood", "law.last_favour"}}) {
        const auto none = detectSynergies(partial, l.defs);
        CHECK(std::find(none.begin(), none.end(), "kronos_favour") == none.end());
    }
}

TEST_CASE("the Kronos + Last Favour conspiracy hides a shortage that grows", "[synergy_kronos_favour]")
{
    Loaded l;
    load(l);
    const std::vector<std::string> world = {"cataclysm.kronos", "law.last_favour"};

    CHECK(postedPrice(world, l.defs, 36) == 100);
    const std::uint64_t act1 = concealedGap(world, l.defs, 1);
    const std::uint64_t act3 = concealedGap(world, l.defs, 36);
    CHECK(act1 == 5);
    CHECK(act3 >= 4 * act1);
    CHECK(act3 > concealedGap(world, l.defs, 12));
    CHECK(concealedGap(world, l.defs, 12) > act1);

    const std::vector<std::string> noSynergy = {"cataclysm.kronos", "law.hecatomb"};
    CHECK(concealedGap(noSynergy, l.defs, 36) == 0);

    std::vector<Synergy> removed;
    std::copy_if(l.defs.begin(), l.defs.end(), std::back_inserter(removed), [](const Synergy& s) { return s.id != "kronos_favour"; });
    CHECK(concealedGap(world, removed, 36) == 0);
}

TEST_CASE("the Kronos + Last Favour concealed gap is bounded for hostile month counts", "[synergy_kronos_favour]")
{
    Loaded l;
    load(l);
    const std::vector<std::string> world = {"cataclysm.kronos", "law.last_favour"};
    CHECK(concealedGap(world, l.defs, -5) == 0);
    CHECK(concealedGap(world, l.defs, INT64_MAX) == concealedGap(world, l.defs, kMaxPriceMonths));
    CHECK(postedPrice({}, l.defs, INT64_MIN) == truePrice(0));
}
