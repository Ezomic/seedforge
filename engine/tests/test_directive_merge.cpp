#include "ichor/data/directive_merge.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

Axiom make(AxiomSlot slot, const std::string& name, std::map<std::string, AxiomBlock, std::less<>> blocks = {})
{
    Axiom a;
    a.slot = slot;
    a.id = std::string(axiomSlotName(slot)) + "." + name;
    a.title = name;
    a.blocks = std::move(blocks);
    return a;
}

const AxiomValue* field(const MergeResult& r, const std::string& block, const std::string& key)
{
    const auto b = r.directives.blocks.find(block);
    if (b == r.directives.blocks.end()) {
        return nullptr;
    }
    const auto k = b->second.find(key);
    return k == b->second.end() ? nullptr : &k->second;
}

const FieldSource* source(const MergeResult& r, const std::string& block, const std::string& key)
{
    for (const FieldSource& s : r.provenance) {
        if (s.block == block && s.key == key) {
            return &s;
        }
    }
    return nullptr;
}

using Words = std::vector<std::string>;
using Weights = std::map<std::string, double, std::less<>>;

}

TEST_CASE("merging nothing, or empty axioms, gives an empty neutral result", "[directive_merge]")
{
    const MergeResult none = mergeDirectives({});
    CHECK(none.directives.blocks.empty());
    CHECK(none.provenance.empty());
    CHECK(none.problems.empty());

    const std::vector<Axiom> empty = {make(AxiomSlot::Cataclysm, "a"), make(AxiomSlot::Law, "b"), make(AxiomSlot::Powers, "c"), make(AxiomSlot::Pressure, "d"), make(AxiomSlot::Anomaly, "e")};
    const MergeResult r = mergeDirectives(empty);
    CHECK(r.directives.blocks.empty());
    CHECK(r.provenance.empty());
    CHECK(r.problems.empty());
}

TEST_CASE("a scalar set by two axioms goes to the higher priority slot", "[directive_merge]")
{
    const Axiom low = make(AxiomSlot::Pressure, "d", {{"terrain", {{"sea_level_offset_m", 10.0}}}, {"palette", {{"grade", std::string("dusk")}}}});
    const Axiom high = make(AxiomSlot::Cataclysm, "a", {{"terrain", {{"sea_level_offset_m", 200.0}}}, {"palette", {{"grade", std::string("flood")}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{low, high});
    REQUIRE(field(r, "terrain", "sea_level_offset_m") != nullptr);
    CHECK(std::get<double>(*field(r, "terrain", "sea_level_offset_m")) == 200.0);
    CHECK(std::get<std::string>(*field(r, "palette", "grade")) == "flood");
    REQUIRE(source(r, "terrain", "sea_level_offset_m") != nullptr);
    CHECK(source(r, "terrain", "sea_level_offset_m")->slots == std::vector<AxiomSlot>{AxiomSlot::Cataclysm});
}

TEST_CASE("a field only a lower slot sets still comes through, with its slot as source", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"terrain", {{"coastal_erosion", 2.4}}}});
    const Axiom e = make(AxiomSlot::Anomaly, "e", {{"terrain", {{"sea_level_offset_m", -5.0}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{e, a});
    CHECK(std::get<double>(*field(r, "terrain", "coastal_erosion")) == 2.4);
    CHECK(std::get<double>(*field(r, "terrain", "sea_level_offset_m")) == -5.0);
    CHECK(source(r, "terrain", "sea_level_offset_m")->slots == std::vector<AxiomSlot>{AxiomSlot::Anomaly});
}

TEST_CASE("plain lists union, sorted and without duplicates", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"traversal", {{"unlock", Words{"swim_deep", "boat"}}}}});
    const Axiom c = make(AxiomSlot::Powers, "c", {{"traversal", {{"unlock", Words{"boat", "climb"}}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{c, a});
    CHECK(std::get<Words>(*field(r, "traversal", "unlock")) == Words{"boat", "climb", "swim_deep"});
    CHECK(source(r, "traversal", "unlock")->slots == std::vector<AxiomSlot>{AxiomSlot::Cataclysm, AxiomSlot::Powers});
}

TEST_CASE("weight maps merge per key and the higher slot wins a shared key", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"poi", {{"weights", Weights{{"pharos", 2.0}, {"farmstead", 0.2}}}}}});
    const Axiom c = make(AxiomSlot::Powers, "c", {{"poi", {{"weights", Weights{{"pharos", 9.0}, {"agora", 1.5}}}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{a, c});
    const Weights& w = std::get<Weights>(*field(r, "poi", "weights"));
    CHECK(w == Weights{{"agora", 1.5}, {"farmstead", 0.2}, {"pharos", 2.0}});
}

TEST_CASE("inject and suppress: the highest slot that mentions a word decides it", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"biomes", {{"inject", Words{"salt_blight"}}, {"suppress", Words{"lowland_farm"}}}}});
    const Axiom c = make(AxiomSlot::Powers, "c", {{"biomes", {{"inject", Words{"lowland_farm", "vineyard"}}, {"suppress", Words{"salt_blight"}}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{a, c});
    CHECK(std::get<Words>(*field(r, "biomes", "inject")) == Words{"salt_blight", "vineyard"});
    CHECK(std::get<Words>(*field(r, "biomes", "suppress")) == Words{"lowland_farm"});
    CHECK(source(r, "biomes", "inject")->slots == std::vector<AxiomSlot>{AxiomSlot::Cataclysm, AxiomSlot::Powers});
    CHECK(source(r, "biomes", "suppress")->slots == std::vector<AxiomSlot>{AxiomSlot::Cataclysm});
}

TEST_CASE("a word in both lists of one axiom is suppressed", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"enemies", {{"roster_add", Words{"drowned", "salt_kin"}}, {"roster_remove", Words{"drowned"}}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{a});
    CHECK(std::get<Words>(*field(r, "enemies", "roster_add")) == Words{"salt_kin"});
    CHECK(std::get<Words>(*field(r, "enemies", "roster_remove")) == Words{"drowned"});
}

TEST_CASE("a list emptied by overrides is left out", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"biomes", {{"suppress", Words{"x"}}}}});
    const Axiom b = make(AxiomSlot::Law, "b", {{"biomes", {{"inject", Words{"x"}}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{a, b});
    CHECK(field(r, "biomes", "inject") == nullptr);
    CHECK(source(r, "biomes", "inject") == nullptr);
    CHECK(std::get<Words>(*field(r, "biomes", "suppress")) == Words{"x"});
}

TEST_CASE("numbers are clamped, and non-finite numbers are ignored", "[directive_merge]")
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Axiom a = make(AxiomSlot::Cataclysm, "a",
        {{"terrain", {{"sea_level_offset_m", nan}, {"coastal_erosion", 1e300}}},
            {"poi", {{"weights", Weights{{"huge", inf}, {"neg", -4.0}, {"big", 1e30}, {"ok", 2.0}}}}},
            {"settlement", {{"min_elevation_m", -inf}}}});
    const Axiom b = make(AxiomSlot::Law, "b", {{"terrain", {{"sea_level_offset_m", 7.0}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{a, b});
    CHECK(std::get<double>(*field(r, "terrain", "sea_level_offset_m")) == 7.0);
    CHECK(source(r, "terrain", "sea_level_offset_m")->slots == std::vector<AxiomSlot>{AxiomSlot::Law});
    CHECK(std::get<double>(*field(r, "terrain", "coastal_erosion")) == kDirectiveMaxMagnitude);
    CHECK(field(r, "settlement", "min_elevation_m") == nullptr);
    const Weights& w = std::get<Weights>(*field(r, "poi", "weights"));
    CHECK(w == Weights{{"big", kDirectiveMaxMagnitude}, {"neg", 0.0}, {"ok", 2.0}});
}

TEST_CASE("a mismatched value type from a lower slot is ignored", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"palette", {{"grade", std::string("flood")}}}});
    const Axiom b = make(AxiomSlot::Law, "b", {{"palette", {{"grade", 3.0}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{b, a});
    CHECK(std::get<std::string>(*field(r, "palette", "grade")) == "flood");
}

TEST_CASE("fewer than five slots merge the same as if the others were empty", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"terrain", {{"coastal_erosion", 2.4}}}});
    const Axiom c = make(AxiomSlot::Powers, "c", {{"settlement", {{"density_mult", 0.7}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{a, c});
    CHECK(r.problems.empty());
    CHECK(std::get<double>(*field(r, "settlement", "density_mult")) == 0.7);
    CHECK(mergedDigest(r) == mergedDigest(mergeDirectives(std::vector<Axiom>{a, c, make(AxiomSlot::Law, "b")})));
}

TEST_CASE("two axioms in one slot are reported and the lower id is kept", "[directive_merge]")
{
    const Axiom x = make(AxiomSlot::Law, "x", {{"terrain", {{"coastal_erosion", 1.0}}}});
    const Axiom y = make(AxiomSlot::Law, "y", {{"terrain", {{"coastal_erosion", 2.0}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{y, x});
    REQUIRE(r.problems.size() == 1);
    CHECK(r.problems[0].find("law.y") != std::string::npos);
    CHECK(std::get<double>(*field(r, "terrain", "coastal_erosion")) == 1.0);
}

TEST_CASE("the result and its digest do not depend on input order", "[directive_merge]")
{
    std::vector<Axiom> axioms = {
        make(AxiomSlot::Cataclysm, "a", {{"terrain", {{"sea_level_offset_m", 200.0}}}, {"biomes", {{"inject", Words{"salt_blight"}}}}}),
        make(AxiomSlot::Law, "b", {{"terrain", {{"sea_level_offset_m", 1.0}}}, {"economy", {{"scarce", Words{"grain"}}}}}),
        make(AxiomSlot::Powers, "c", {{"poi", {{"weights", Weights{{"agora", 1.5}}}}}, {"biomes", {{"suppress", Words{"salt_blight"}}}}}),
        make(AxiomSlot::Pressure, "d", {{"audio", {{"music_palette", std::string("dirge")}}}}),
        make(AxiomSlot::Anomaly, "e", {{"traversal", {{"unlock", Words{"door"}}}}}),
    };
    const MergeResult first = mergeDirectives(axioms);
    const std::uint64_t digest = mergedDigest(first);
    const std::string text = mergedCanonicalText(first);
    std::sort(axioms.begin(), axioms.end(), [](const Axiom& l, const Axiom& r) { return l.id > r.id; });
    do {
        const MergeResult again = mergeDirectives(axioms);
        REQUIRE(mergedDigest(again) == digest);
        REQUIRE(mergedCanonicalText(again) == text);
    } while (std::next_permutation(axioms.begin(), axioms.end(), [](const Axiom& l, const Axiom& r) { return l.id > r.id; }));
}

TEST_CASE("the digest changes when a directive or its source slot changes", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"terrain", {{"coastal_erosion", 2.4}}}});
    const Axiom b = make(AxiomSlot::Law, "b", {{"terrain", {{"coastal_erosion", 2.4}}}});
    const Axiom changed = make(AxiomSlot::Cataclysm, "a", {{"terrain", {{"coastal_erosion", 2.5}}}});
    const std::uint64_t base = mergedDigest(mergeDirectives(std::vector<Axiom>{a}));
    CHECK(base != mergedDigest(mergeDirectives(std::vector<Axiom>{b})));
    CHECK(base != mergedDigest(mergeDirectives(std::vector<Axiom>{changed})));
}

TEST_CASE("the canonical text is pinned", "[directive_merge]")
{
    const Axiom a = make(AxiomSlot::Cataclysm, "a", {{"terrain", {{"coastal_erosion", 2.4}, {"sea_level_offset_m", -5.0}}}, {"biomes", {{"inject", Words{"b", "a"}}}}});
    const MergeResult r = mergeDirectives(std::vector<Axiom>{a});
    CHECK(mergedCanonicalText(r) == R"({"biomes":{"inject":["a","b"]},"terrain":{"coastal_erosion":2.400000,"sea_level_offset_m":-5.000000}}|biomes.inject:A;terrain.coastal_erosion:A;terrain.sea_level_offset_m:A;)");
}
