#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"
#include "ichor/economy/wallet.h"
#include "ichor/progression/capabilities.h"
#include "ichor/progression/trainers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace ichor;

namespace {

constexpr const char* kSanctionGate = "amphictyony_sanctioned";

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

std::string readProgression(const char* name)
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "progression" / name, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

CurrencyTable shippedCurrencies(const DataSet& data)
{
    const DataTable* table = data.table("currency");
    REQUIRE(table != nullptr);
    std::string error;
    auto result = CurrencyTable::fromTable(*table, error);
    REQUIRE(result.has_value());
    return *result;
}

// Under a fired capital_initiation mechanic a trainer's lesson is licensed: only the league's
// seal opens a gate. Without it every gate answers as the trainer's own standing says (open here).
bool gateOpen(bool licensed, bool holdsSeal, std::string_view gate)
{
    if (!licensed) {
        return true;
    }
    return holdsSeal && gate == kSanctionGate;
}

bool licensedBy(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs)
{
    const std::vector<std::string> fired = detectSynergies(axiomIds, defs);
    return std::any_of(fired.begin(), fired.end(), [&](const std::string& id) {
        const Synergy* s = find(defs, id);
        return s != nullptr && s->mechanic == "capital_initiation";
    });
}

// How many (trainer, capability) lessons the player manages to buy from the shipped trainers
// with a full purse, optionally with every trainer re-gated behind the league's seal.
std::size_t lessonsLearned(const Loaded& l, bool licensed, bool holdsSeal, bool resealTrainers)
{
    std::vector<Capability> table;
    std::vector<std::string> problems;
    REQUIRE(parseCapabilities("capabilities.json", readProgression("capabilities.json"), table, problems));
    std::vector<Trainer> trainers;
    REQUIRE(parseTrainers("trainers.json", readProgression("trainers.json"), table, trainers, problems));
    const CurrencyTable currencies = shippedCurrencies(l.data);

    FoundTrainers found;
    for (Trainer& t : trainers) {
        found.insert(t.id);
        if (resealTrainers) {
            t.gate = kSanctionGate;
        }
    }
    CapabilityUnlocks unlocks{table};
    Wallet wallet;
    REQUIRE(wallet.earn(currencies, CurrencySlot::Coin, 1'000'000, "loot").status == TxStatus::Ok);
    const TrainerGate gate = [&](std::string_view g) { return gateOpen(licensed, holdsSeal, g); };

    std::size_t learned = 0;
    for (const Trainer& t : trainers) {
        for (const TrainerOffer& offer : t.offers) {
            if (train(t, offer.capability, found, gate, unlocks, wallet, currencies).status == TrainStatus::Done) {
                ++learned;
            }
        }
    }
    return learned;
}

}

TEST_CASE("the Mysteries + Amphictyony flagship loads through the real loader as tier 1", "[synergy_mysteries_amphictyony]")
{
    Loaded l;
    load(l);
    const Synergy* s = find(l.defs, "mysteries_amphictyony");
    REQUIRE(s != nullptr);
    CHECK(s->tier == SynergyTier::LawPowers);
    CHECK(s->contentTier == ContentTier::Flagship);
    CHECK(s->faction == "telestai");
    CHECK(s->dungeonType == "sanctuary_tribunal");
    CHECK(s->mechanic == "capital_initiation");
    CHECK(s->worldName == "The Closed Rite");
    CHECK(s->axioms == std::vector<std::string>{"law.mysteries", "powers.amphictyony"});
}

TEST_CASE("the Mysteries + Amphictyony flagship has no dangling reference", "[synergy_mysteries_amphictyony]")
{
    Loaded l;
    load(l);
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("the Mysteries + Amphictyony flagship fires for its pair and for nothing less", "[synergy_mysteries_amphictyony]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies({"law.mysteries", "powers.amphictyony"}, l.defs);
    CHECK(std::find(fired.begin(), fired.end(), "mysteries_amphictyony") != fired.end());
    const SynergyOverride result = applyOverrides(fired, l.defs, {});
    CHECK(std::find(result.factions.begin(), result.factions.end(), "telestai") != result.factions.end());
    CHECK(std::find(result.dungeonTypes.begin(), result.dungeonTypes.end(), "sanctuary_tribunal") != result.dungeonTypes.end());

    for (const std::vector<std::string>& partial : {std::vector<std::string>{"law.mysteries"}, std::vector<std::string>{"powers.amphictyony"}, std::vector<std::string>{"law.mysteries", "powers.league"}, std::vector<std::string>{"law.hecatomb", "powers.amphictyony"}}) {
        const auto none = detectSynergies(partial, l.defs);
        CHECK(std::find(none.begin(), none.end(), "mysteries_amphictyony") == none.end());
    }
}

TEST_CASE("the Mysteries + Amphictyony makes unlicensed learning impossible and licensed learning possible", "[synergy_mysteries_amphictyony]")
{
    Loaded l;
    load(l);
    const std::vector<std::string> world = {"law.mysteries", "powers.amphictyony"};
    const std::vector<std::string> noSynergy = {"law.mysteries", "powers.league"};

    const std::size_t free = lessonsLearned(l, licensedBy(noSynergy, l.defs), false, false);
    REQUIRE(free >= 7);

    CHECK(lessonsLearned(l, licensedBy(world, l.defs), false, false) == 0);
    CHECK(lessonsLearned(l, licensedBy(world, l.defs), false, true) == 0);
    CHECK(lessonsLearned(l, licensedBy(world, l.defs), true, true) == free);
    CHECK(lessonsLearned(l, licensedBy(world, l.defs), true, false) == 0);

    std::vector<Synergy> removed;
    std::copy_if(l.defs.begin(), l.defs.end(), std::back_inserter(removed), [](const Synergy& s) { return s.id != "mysteries_amphictyony"; });
    CHECK(lessonsLearned(l, licensedBy(world, removed), false, false) == free);
}
