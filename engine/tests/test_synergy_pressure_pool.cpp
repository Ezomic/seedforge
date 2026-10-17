#include "ichor/data/axiom.h"
#include "ichor/data/data_set.h"
#include "ichor/data/synergy.h"
#include "ichor/data/synergy_content.h"
#include "ichor/data/xref.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::vector<std::string> kPoolIds = {"artemis_unburied", "mysteries_miasma", "orichalcum_foretold"};

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

std::vector<const Synergy*> pool(const std::vector<Synergy>& defs)
{
    std::vector<const Synergy*> out;
    for (const std::string& id : kPoolIds) {
        const Synergy* s = find(defs, id);
        REQUIRE(s != nullptr);
        out.push_back(s);
    }
    return out;
}

const Axiom* pressureAxiomOf(const Loaded& l, const Synergy& s)
{
    for (const std::string& id : s.axioms) {
        const Axiom* a = l.axioms.find(id);
        if (a != nullptr && a->slot == AxiomSlot::Pressure) {
            return a;
        }
    }
    return nullptr;
}

using Weights = std::map<std::string, double, std::less<>>;

const AxiomValue* value(const Axiom& a, const std::string& block, const std::string& key)
{
    const auto b = a.blocks.find(block);
    if (b == a.blocks.end()) {
        return nullptr;
    }
    const auto v = b->second.find(key);
    return v == b->second.end() ? nullptr : &v->second;
}

// How far the pressure axiom pulls fog density, settlement density and POI weights away from
// neutral (1.0). A pressure synergy over an axiom with no such pull would be prose over nothing.
double footprint(const Axiom& a)
{
    double total = 0.0;
    if (const AxiomValue* fog = value(a, "palette", "fog_density")) {
        total += std::abs(std::get<double>(*fog) - 1.0);
    }
    if (const AxiomValue* density = value(a, "settlement", "density_mult")) {
        total += std::abs(std::get<double>(*density) - 1.0);
    }
    if (const AxiomValue* weights = value(a, "poi", "weights")) {
        for (const auto& [name, weight] : std::get<Weights>(*weights)) {
            total += std::abs(weight - 1.0);
        }
    }
    return total;
}

}

TEST_CASE("every Pressure pool record loads as a text-only pressure synergy over existing axioms", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    for (const Synergy* s : pool(l.defs)) {
        INFO(s->id);
        CHECK(s->tier == SynergyTier::Pressure);
        CHECK(s->contentTier == ContentTier::TextOnly);
        CHECK(s->axioms.size() == 2);
        CHECK_FALSE(s->worldName.empty());
        CHECK(s->flavour.count("world_description") == 1);
        for (const std::string& id : s->axioms) {
            CHECK(l.axioms.find(id) != nullptr);
        }
        CHECK(pressureAxiomOf(l, *s) != nullptr);
    }
}

TEST_CASE("the synergy set has no duplicate id, axiom pair or world name", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    std::set<std::string> ids;
    std::set<std::vector<std::string>> pairs;
    std::set<std::string> names;
    for (const Synergy& s : l.defs) {
        INFO(s.id);
        CHECK(ids.insert(s.id).second);
        CHECK(pairs.insert(s.axioms).second);
        CHECK(names.insert(s.worldName).second);
    }
}

TEST_CASE("the Pressure pool has no dangling reference", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    const XrefReport report = checkReferences(l.data, l.axioms, std::filesystem::path(ICHOR_GAME_DATA_DIR));
    for (const DanglingRef& ref : report.dangling) {
        UNSCOPED_INFO(describe(ref));
    }
    CHECK(report.ok());
}

TEST_CASE("no Pressure pool pair conflicts", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    for (const Synergy* s : pool(l.defs)) {
        INFO(s->id);
        for (const std::string& a : s->axioms) {
            const Axiom* axiom = l.axioms.find(a);
            REQUIRE(axiom != nullptr);
            for (const std::string& b : s->axioms) {
                CHECK(std::find(axiom->conflicts.begin(), axiom->conflicts.end(), b) == axiom->conflicts.end());
            }
        }
    }
}

TEST_CASE("each Pressure pool pair fires for its pair and for nothing less", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    for (const Synergy* s : pool(l.defs)) {
        INFO(s->id);
        const auto fired = detectSynergies(s->axioms, l.defs);
        REQUIRE(std::find(fired.begin(), fired.end(), s->id) != fired.end());
        CHECK(applyOverrides(fired, l.defs, {}).worldName == s->worldName);

        for (const std::string& dropped : s->axioms) {
            std::vector<std::string> partial;
            std::copy_if(s->axioms.begin(), s->axioms.end(), std::back_inserter(partial), [&](const std::string& a) { return a != dropped; });
            const auto none = detectSynergies(partial, l.defs);
            CHECK(std::find(none.begin(), none.end(), s->id) == none.end());
        }
    }
}

TEST_CASE("each Pressure pool pair has a measurable and bounded pressure footprint", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    for (const Synergy* s : pool(l.defs)) {
        INFO(s->id);
        const Axiom* pressure = pressureAxiomOf(l, *s);
        REQUIRE(pressure != nullptr);
        CHECK(footprint(*pressure) > 0.5);
        CHECK(footprint(*pressure) < 20.0);

        const AxiomValue* fog = value(*pressure, "palette", "fog_density");
        REQUIRE(fog != nullptr);
        CHECK(std::get<double>(*fog) >= 0.5);
        CHECK(std::get<double>(*fog) <= 2.0);

        if (const AxiomValue* density = value(*pressure, "settlement", "density_mult")) {
            CHECK(std::get<double>(*density) > 0.0);
            CHECK(std::get<double>(*density) <= 1.0);
        }
        const AxiomValue* weights = value(*pressure, "poi", "weights");
        REQUIRE(weights != nullptr);
        for (const auto& [name, weight] : std::get<Weights>(*weights)) {
            INFO(name);
            CHECK(std::isfinite(weight));
            CHECK(weight >= 0.0);
            CHECK(weight <= 5.0);
        }
    }
}

TEST_CASE("removing the Pressure pool removes the synergies and the world names", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    const std::vector<std::vector<std::string>> worlds = {
        {"law.mysteries", "pressure.miasma"},
        {"law.orichalcum", "pressure.foretold"},
        {"cataclysm.artemis_withdrew", "pressure.unburied"},
    };
    std::vector<Synergy> removed;
    std::copy_if(l.defs.begin(), l.defs.end(), std::back_inserter(removed), [](const Synergy& s) { return std::find(kPoolIds.begin(), kPoolIds.end(), s.id) == kPoolIds.end(); });

    for (const auto& world : worlds) {
        const auto withPool = detectSynergies(world, l.defs);
        const auto withoutPool = detectSynergies(world, removed);
        CHECK(withPool.size() == 1);
        CHECK(withoutPool.empty());
        CHECK_FALSE(applyOverrides(withPool, l.defs, {}).worldName.empty());
        CHECK(applyOverrides(withoutPool, removed, {}).worldName.empty());
    }
}

TEST_CASE("a world holding every Pressure pool axiom still respects the active synergy cap", "[synergy_pressure_pool]")
{
    Loaded l;
    load(l);
    const auto fired = detectSynergies({"law.mysteries", "pressure.miasma", "law.orichalcum", "pressure.foretold", "cataclysm.artemis_withdrew", "pressure.unburied"}, l.defs);
    CHECK_FALSE(fired.empty());
    CHECK(fired.size() <= kMaxActiveSynergies);
}
