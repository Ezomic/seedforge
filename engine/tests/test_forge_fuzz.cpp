#include <catch2/catch_test_macros.hpp>

#include <forgecli/fuzz.h>

#include <algorithm>

using namespace forgecli;

TEST_CASE("fuzz: a modest seed range over every phase has no violations", "[forgefuzz]")
{
    FuzzOptions o;
    o.seedBegin = 1;
    o.seedEnd = 64;
    o.size = 128;
    const FuzzResult r = runFuzz(o);
    REQUIRE(r.error.empty());
    CHECK(r.seedsRun == 64);
    CHECK(r.violations.empty());
    if (!r.violations.empty()) FAIL(fuzzSummary(o, r));
}

TEST_CASE("fuzz: invalid options are rejected", "[forgefuzz]")
{
    FuzzOptions o;
    o.seedBegin = 5;
    o.seedEnd = 4;
    CHECK_FALSE(runFuzz(o).error.empty());
    o.seedEnd = 5;
    o.phases = {"nope"};
    CHECK_FALSE(runFuzz(o).error.empty());
    o.phases = {"tectonics", "tectonics"};
    CHECK_FALSE(runFuzz(o).error.empty());
    o.phases.clear();
    o.size = 0;
    CHECK_FALSE(runFuzz(o).error.empty());
    o.size = 1025;
    CHECK_FALSE(runFuzz(o).error.empty());
    o.size = 128;
    o.seedEnd = 5000000;
    CHECK_FALSE(runFuzz(o).error.empty());
}

TEST_CASE("fuzz: an unsupported grid size is reported per seed, not crashed", "[forgefuzz]")
{
    FuzzOptions o;
    o.seedEnd = 1;
    o.size = 4;
    o.phases = {"tectonics"};
    const FuzzResult r = runFuzz(o);
    REQUIRE(r.error.empty());
    CHECK(r.violations.size() == 2);
    CHECK(std::all_of(r.violations.begin(), r.violations.end(), [](const Violation& v) { return v.rule == "no_error"; }));
}

TEST_CASE("fuzz: budgets are enforced in cell passes and bytes", "[forgefuzz]")
{
    FuzzOptions o;
    o.seedEnd = 0;
    o.size = 128;
    o.maxCellPasses = 1;
    o.maxLayerBytes = 1;
    const FuzzResult r = runFuzz(o);
    const auto has = [&](std::string_view rule) {
        return std::any_of(r.violations.begin(), r.violations.end(), [&](const Violation& v) { return v.rule == rule; });
    };
    CHECK(has("cell_pass_budget"));
    CHECK(has("layer_byte_budget"));
}

TEST_CASE("fuzz: phase rules go red on a degenerate layer", "[forgefuzz]")
{
    Layer flat;
    flat.name = "height_carved";
    flat.width = flat.height = 32;
    flat.pixels.assign(32 * 32, 0);
    const auto& rules = phaseRules();
    const auto it = std::find_if(rules.begin(), rules.end(), [](const PhaseRule& r) { return r.phase == "hydrology"; });
    REQUIRE(it != rules.end());
    CHECK_FALSE(it->check({flat}, 32).empty());
}

TEST_CASE("fuzz: summary lists violating seeds", "[forgefuzz]")
{
    FuzzOptions o;
    FuzzResult r;
    r.seedsRun = 3;
    r.violations.push_back({7, "biomes", "varied_biomes", "x"});
    const std::string s = fuzzSummary(o, r);
    CHECK(s.find("1 violating") != std::string::npos);
    CHECK(s.find("seed 7 [biomes]") != std::string::npos);
}
