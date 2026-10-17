#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"
#include "ichor/progression/law_power_sources.h"
#include "ichor/traversal/climb_rules.h"
#include "ichor/world/temenos_field.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
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

struct Site {
    float x = 0.0f;
    float z = 0.0f;
    float dropMetres = 0.0f;
};

constexpr std::int64_t kBasePower = 1000;
constexpr std::int64_t kOffGroundFloorPermille = 300;

// Chasm rims of rising danger, and a valley hub well clear of all of them.
const std::vector<Site> kRims = {{0.0f, 0.0f, 2.0f}, {1000.0f, 0.0f, 10.0f}, {2000.0f, 0.0f, 25.0f}};
constexpr Site kValleyHub{1000.0f, 5000.0f, 0.0f};

bool edgePower(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs)
{
    const std::vector<std::string> fired = detectSynergies(axiomIds, defs);
    return std::any_of(fired.begin(), fired.end(), [&](const std::string& id) {
        const Synergy* s = find(defs, id);
        return s != nullptr && s->mechanic == "edge_power";
    });
}

// Where the sanctuaries stand. Under the edge_power mechanic each one sits on a chasm rim and is
// stronger the further the fall; otherwise the Temenos law keeps its sanctuaries in the valley.
std::vector<TemenosSanctuary> sanctuaries(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs)
{
    if (!edgePower(axiomIds, defs)) {
        return {TemenosSanctuary{kValleyHub.x, kValleyHub.z, 400.0f, 1.0f}};
    }
    std::vector<TemenosSanctuary> out;
    for (const Site& rim : kRims) {
        const FallOutcome fall = assessFall(ClimbLimits{}, rim.dropMetres);
        out.push_back(TemenosSanctuary{rim.x, rim.z, 300.0f, 0.4f + 0.6f * fall.damageFraction});
    }
    return out;
}

std::int64_t powerAt(const std::vector<TemenosSanctuary>& s, const Site& at)
{
    return temenosScaled(kBasePower, temenosField(s, at.x, at.z), kOffGroundFloorPermille);
}

std::int64_t meanRimPower(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& defs)
{
    const std::vector<TemenosSanctuary> s = sanctuaries(axiomIds, defs);
    std::int64_t total = 0;
    for (const Site& rim : kRims) {
        total += powerAt(s, rim);
    }
    return total / static_cast<std::int64_t>(kRims.size());
}

const std::vector<std::string> kWorld = {"cataclysm.unbinding", "law.temenos"};

}

TEST_CASE("the Unbinding + Temenos flagship loads through the real loader as tier 1", "[synergy_unbinding_temenos]")
{
    Loaded l;
    load(l);
    const Synergy* s = find(l.defs, "unbinding_temenos");
    REQUIRE(s != nullptr);
    CHECK(s->tier == SynergyTier::CataclysmLaw);
    CHECK(s->contentTier == ContentTier::Flagship);
    CHECK(s->faction == "horistai");
    CHECK(s->dungeonType == "chasm_sanctuary");
    CHECK(s->mechanic == "edge_power");
    CHECK(s->worldName == "The Sundered Ways");
    CHECK(s->axioms == kWorld);
}

TEST_CASE("the Unbinding + Temenos flagship has no dangling reference", "[synergy_unbinding_temenos]")
{
    Loaded l;
    load(l);
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("the Unbinding + Temenos flagship fires for its pair and for nothing less", "[synergy_unbinding_temenos]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies(kWorld, l.defs);
    CHECK(std::find(fired.begin(), fired.end(), "unbinding_temenos") != fired.end());
    const SynergyOverride result = applyOverrides(fired, l.defs, {});
    CHECK(std::find(result.factions.begin(), result.factions.end(), "horistai") != result.factions.end());
    CHECK(result.worldName == "The Sundered Ways");

    for (const std::vector<std::string>& partial : {std::vector<std::string>{"cataclysm.unbinding"}, std::vector<std::string>{"law.temenos"}, std::vector<std::string>{"cataclysm.unbinding", "law.hecatomb"}, std::vector<std::string>{"cataclysm.flood", "law.temenos"}}) {
        const auto none = detectSynergies(partial, l.defs);
        CHECK(std::find(none.begin(), none.end(), "unbinding_temenos") == none.end());
    }
}

TEST_CASE("the Unbinding + Temenos puts divine strength on the dangerous edges", "[synergy_unbinding_temenos]")
{
    Loaded l;
    load(l);
    const std::int64_t withSynergy = meanRimPower(kWorld, l.defs);
    const std::int64_t without = meanRimPower({"cataclysm.flood", "law.temenos"}, l.defs);
    CHECK(without == kBasePower * kOffGroundFloorPermille / 1000);
    CHECK(withSynergy >= 2 * without);

    std::vector<Synergy> removed;
    std::copy_if(l.defs.begin(), l.defs.end(), std::back_inserter(removed), [](const Synergy& s) { return s.id != "unbinding_temenos"; });
    CHECK(meanRimPower(kWorld, removed) == without);
}

TEST_CASE("the Unbinding + Temenos is strongest where the fall is longest", "[synergy_unbinding_temenos]")
{
    Loaded l;
    load(l);
    const std::vector<TemenosSanctuary> s = sanctuaries(kWorld, l.defs);
    REQUIRE(s.size() == kRims.size());
    const std::int64_t safe = powerAt(s, kRims[0]);
    const std::int64_t mid = powerAt(s, kRims[1]);
    const std::int64_t deep = powerAt(s, kRims[2]);
    CHECK(safe < mid);
    CHECK(mid < deep);
    CHECK(powerAt(s, kValleyHub) == kBasePower * kOffGroundFloorPermille / 1000);
}

TEST_CASE("the Unbinding + Temenos edge power is bounded for hostile drops", "[synergy_unbinding_temenos]")
{
    for (const float drop : {-5.0f, 0.0f, 1e30f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        const FallOutcome fall = assessFall(ClimbLimits{}, drop);
        const TemenosSanctuary s{0.0f, 0.0f, 300.0f, 0.4f + 0.6f * fall.damageFraction};
        const std::int64_t power = temenosScaled(kBasePower, temenosField({&s, 1}, 0.0f, 0.0f), kOffGroundFloorPermille);
        CHECK(power >= kBasePower * kOffGroundFloorPermille / 1000);
        CHECK(power <= kBasePower);
    }
}
