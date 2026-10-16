#include "ichor/data/axiom.h"
#include "ichor/progression/law_progression.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const AxiomSet& shipped()
{
    static const AxiomSet set = [] {
        AxiomSet loaded;
        REQUIRE(loaded.loadDirectory(ICHOR_REPO_DATA));
        return loaded;
    }();
    return set;
}

}

TEST_CASE("every shipped Law axiom has a progression meaning", "[law][progression]")
{
    int laws = 0;
    for (const auto& [id, axiom] : shipped().axioms()) {
        if (axiom.slot != AxiomSlot::Law) continue;
        ++laws;
        INFO(id);
        const LawProgressionDef* def = lawProgression(id);
        REQUIRE(def != nullptr);
        CHECK(def->axiomId == id);
    }
    CHECK(laws >= 3);
}

TEST_CASE("the six doc 07 Laws each have a distinct meaning", "[law][progression]")
{
    const auto all = allLawProgressions();
    REQUIRE(all.size() == 6);
    std::set<LawProgressionKind> kinds;
    std::set<std::string> ids;
    for (const LawProgressionDef& def : all) {
        kinds.insert(def.kind);
        ids.insert(std::string(def.axiomId));
        CHECK_FALSE(def.advancedBy.empty());
        CHECK_FALSE(def.summary.empty());
        CHECK(def.axiomId.rfind("law.", 0) == 0);
    }
    CHECK(kinds.size() == 6);
    CHECK(ids.size() == 6);
}

TEST_CASE("each Law maps to the kind and measure of doc 07", "[law][progression]")
{
    CHECK(lawProgression("law.orichalcum")->kind == LawProgressionKind::Logistics);
    CHECK(lawProgression("law.hecatomb")->kind == LawProgressionKind::Vitality);
    CHECK(lawProgression("law.mysteries")->kind == LawProgressionKind::Initiation);
    CHECK(lawProgression("law.temenos")->kind == LawProgressionKind::Cartography);
    CHECK(lawProgression("law.last_favour")->kind == LawProgressionKind::Conservation);
    CHECK(lawProgression("law.oath")->kind == LawProgressionKind::DebtCeiling);

    CHECK(lawProgression("law.mysteries")->measure == ProgressionMeasure::Count);
    CHECK(lawProgression("law.oath")->measure == ProgressionMeasure::Count);
    CHECK(lawProgression("law.orichalcum")->measure == ProgressionMeasure::GrowthPermille);
    CHECK(lawProgression("law.last_favour")->measure == ProgressionMeasure::CostPermille);
}

TEST_CASE("only Conservation does not raise raw power", "[law][progression]")
{
    for (const LawProgressionDef& def : allLawProgressions())
        CHECK(def.raisesRawPower == (def.kind != LawProgressionKind::Conservation));
}

TEST_CASE("Hecatomb shares one pool between survival and casting", "[law][progression]")
{
    const LawProgressionDef* hecatomb = lawProgression("law.hecatomb");
    REQUIRE(hecatomb != nullptr);
    CHECK(hecatomb->sharedPool);
    CHECK(hecatomb->advancedBy.size() == 1);
    for (const LawProgressionDef& def : allLawProgressions())
        if (def.kind != LawProgressionKind::Vitality) CHECK_FALSE(def.sharedPool);
}

TEST_CASE("Logistics names carry, refining and supply lines", "[law][progression]")
{
    const LawProgressionDef* def = lawProgression("law.orichalcum");
    REQUIRE(def != nullptr);
    CHECK(def->advancedBy.size() == 3);
}

TEST_CASE("unknown, missing and non-Law axioms have no meaning", "[law][progression]")
{
    CHECK(lawProgression("") == nullptr);
    CHECK(lawProgression("law.nonexistent") == nullptr);
    CHECK(lawProgression("cataclysm.flood") == nullptr);
    CHECK(lawProgression("anomaly.labyrinth") == nullptr);
    CHECK(lawProgression("law.") == nullptr);
    CHECK(lawProgression("law") == nullptr);
}

TEST_CASE("hostile ids never match or crash", "[law][progression]")
{
    CHECK(lawProgression("LAW.HECATOMB") == nullptr);
    CHECK(lawProgression(" law.hecatomb") == nullptr);
    CHECK(lawProgression("law.hecatomb ") == nullptr);
    CHECK(lawProgression("law.hecatomb\n") == nullptr);
    CHECK(lawProgression(std::string_view("law.hecatomb\0x", 14)) == nullptr);
    CHECK(lawProgression(std::string_view("law.hecatomb\0", 13)) == nullptr);
    CHECK(lawProgression("law.hecatomb/../law.oath") == nullptr);
    CHECK(lawProgression(std::string(1 << 20, 'a')) == nullptr);
    CHECK(lawProgression("law.h\xff\xfe") == nullptr);
    CHECK(lawProgression("law.%s%n") == nullptr);
}

TEST_CASE("mixed axiom lists keep only Laws, in order, once each", "[law][progression]")
{
    const std::vector<std::string> ids = {"cataclysm.flood", "law.oath", "bogus", "law.hecatomb", "law.oath", "anomaly.labyrinth", "", "law.orichalcum"};
    const auto found = lawProgressionsFor(ids);
    REQUIRE(found.size() == 3);
    CHECK(found[0]->axiomId == "law.oath");
    CHECK(found[1]->axiomId == "law.hecatomb");
    CHECK(found[2]->axiomId == "law.orichalcum");
}

TEST_CASE("empty and all-unknown lists give nothing", "[law][progression]")
{
    CHECK(lawProgressionsFor({}).empty());
    CHECK(lawProgressionsFor({"", "x", "cataclysm.flood"}).empty());
}

TEST_CASE("a huge hostile list stays bounded by the table", "[law][progression]")
{
    std::vector<std::string> ids(100000, "law.temenos");
    ids.push_back("law.mysteries");
    const auto found = lawProgressionsFor(ids);
    REQUIRE(found.size() == 2);
    CHECK(found[0]->axiomId == "law.temenos");
    CHECK(found[1]->axiomId == "law.mysteries");
}

TEST_CASE("kind names are stable and distinct", "[law][progression]")
{
    std::set<std::string> names;
    for (const LawProgressionDef& def : allLawProgressions()) names.insert(std::string(lawProgressionKindName(def.kind)));
    CHECK(names.size() == 6);
    CHECK(lawProgressionKindName(static_cast<LawProgressionKind>(200)) == "unknown");
}
