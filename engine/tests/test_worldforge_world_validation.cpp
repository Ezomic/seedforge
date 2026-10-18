#include <worldforge/world_validation.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace worldforge;

namespace {

constexpr int kSmall = 64;

ForgeConfig smallConfig()
{
    ForgeConfig c;
    c.settlements.minSpacing = 8;
    c.settlements.maxSettlements = 12;
    c.settlements.minScore = 24;
    c.pois.minSpacing = 8;
    c.itemIds = { "sword", "shield", "ring", "lantern" };
    c.npcArchetypes = { "smith", "healer" };
    c.dungeonArchetypes = { "crypt", "mine" };
    return c;
}

// Every gate lax, so one gate at a time can be tightened and only it may flip.
GateThresholds lax()
{
    GateThresholds t;
    t.minSettlementsPerKm2Milli = 0;
    t.maxSettlementsPerKm2Milli = 1000000;
    t.maxRoadComponents = 256;
    t.minSettlementReachPermille = 0;
    t.minPoiReachPermille = 0;
    t.minLandmarkCoveragePermille = 0;
    t.minQuestItems = 0;
    t.minQuestCast = 0;
    return t;
}

const GateResult& gate(const ValidationReport& r, std::string_view id)
{
    const auto it = std::find_if(r.gates.begin(), r.gates.end(), [&](const GateResult& g) { return g.id == id; });
    REQUIRE(it != r.gates.end());
    return *it;
}

std::vector<std::string> failedIds(const ValidationReport& r)
{
    std::vector<std::string> out;
    for (const GateResult& g : r.gates) {
        if (g.status == GateStatus::Fail) out.push_back(g.id);
    }
    return out;
}

// A 32 x 32 world built by hand: flat land at 10 m, three settlements, two POIs, a quest manifest.
// The walkable cells are the whole grid unless a test cuts them.
constexpr int kGrid = 32;

struct HandWorld {
    detail::ForgedParts p;

    HandWorld()
    {
        const auto cells = static_cast<std::size_t>(kGrid * kGrid);
        p.seed = 5;
        p.size = kGrid;
        p.completed = kForgePhaseCount;
        p.worldHash = 99;
        HydrologyMap h;
        h.width = kGrid;
        h.height = kGrid;
        h.lakeDepthM.assign(cells, 0);
        h.river.assign(cells, 0);
        p.hydrology = h;
        RoadMap r;
        r.width = kGrid;
        r.height = kGrid;
        r.heightM.assign(cells, 10);
        r.road.assign(cells, 0);
        r.components = 1;
        p.roads = r;
        SettlementMap s;
        s.width = kGrid;
        s.height = kGrid;
        s.sites = { { 2, 2, 100, 1, 0 }, { 4, 20, 100, 1, 0 }, { 28, 10, 100, 1, 0 } };
        p.settlements = s;
        PoiMap pm;
        pm.width = kGrid;
        pm.height = kGrid;
        pm.pois = { { 10, 10, 500, 3 }, { 29, 29, 500, 3 } };
        p.pois = pm;
        LandmarkCoverageReport lc;
        lc.coveragePermille = 950;
        lc.passed = true;
        p.landmarkCoverage = lc;
        ContentManifest m;
        m.items = { { "sword", "poi_0000" } };
        m.cast = { { "smith#0", "smith", "settlement_0000" } };
        p.manifest = m;
    }

    std::size_t at(int x, int y) const { return static_cast<std::size_t>(y * kGrid + x); }
    void wall(int x, std::uint8_t road = 0)
    {
        for (int y = 0; y < kGrid; ++y) {
            p.hydrology->river[at(x, y)] = 1;
            p.roads->road[at(x, y)] = road;
        }
    }
    ForgedWorld world() const { return ForgedWorld(p); }
};

}

TEST_CASE("validateWorld reports every gate in order and always skips the axiom gate", "[worldforge][validation]")
{
    const ValidationReport r = validateWorld(HandWorld().world());
    REQUIRE(r.gates.size() == 8);
    const std::vector<std::string> ids = { "settlement_density", "road_connected", "settlement_reach", "no_dead_ends", "landmark_coverage", "quest_items", "quest_cast", "axiom_directives_starved" };
    for (std::size_t i = 0; i < ids.size(); ++i) CHECK(r.gates[i].id == ids[i]);
    CHECK(r.passed());
    CHECK(failedIds(r).empty());
    const GateResult& axiom = gate(r, "axiom_directives_starved");
    CHECK(axiom.status == GateStatus::Skipped);
    CHECK(axiom.cause.find("not implemented") != std::string::npos);
    CHECK(r.seed == 5);
    CHECK(r.worldHash == 99);
}

TEST_CASE("each gate flips on its own when its threshold is tightened past the measured value", "[worldforge][validation]")
{
    const ForgedWorld w = HandWorld().world();
    const GateThresholds base = lax();
    REQUIRE(validateWorld(w, base).passed());

    const std::int64_t density = gate(validateWorld(w, base), "settlement_density").value;
    CHECK(density == 3 * 1000000000ll / (32 * 32 * 16 * 16));

    GateThresholds t = base;
    t.minSettlementsPerKm2Milli = static_cast<int>(density) + 1;
    t.maxSettlementsPerKm2Milli = 1000000;
    CHECK(failedIds(validateWorld(w, t)) == std::vector<std::string>{ "settlement_density" });
    t = base;
    t.maxSettlementsPerKm2Milli = static_cast<int>(density) - 1;
    CHECK(failedIds(validateWorld(w, t)) == std::vector<std::string>{ "settlement_density" });

    HandWorld split;
    split.p.roads->components = 3;
    t = base;
    t.maxRoadComponents = 2;
    CHECK(failedIds(validateWorld(split.world(), t)) == std::vector<std::string>{ "road_connected" });
    t.maxRoadComponents = 3;
    CHECK(validateWorld(split.world(), t).passed());

    t = base;
    t.minLandmarkCoveragePermille = 951;
    CHECK(failedIds(validateWorld(w, t)) == std::vector<std::string>{ "landmark_coverage" });
    t.minLandmarkCoveragePermille = 950;
    CHECK(validateWorld(w, t).passed());

    t = base;
    t.minQuestItems = 2;
    CHECK(failedIds(validateWorld(w, t)) == std::vector<std::string>{ "quest_items" });
    t = base;
    t.minQuestCast = 2;
    CHECK(failedIds(validateWorld(w, t)) == std::vector<std::string>{ "quest_cast" });
}

TEST_CASE("a river wall splits the settlements and POIs, and a road crossing rejoins them", "[worldforge][validation]")
{
    GateThresholds t;
    t.minSettlementReachPermille = 1000;
    t.minPoiReachPermille = 1000;

    HandWorld split;
    split.wall(16);
    const ValidationReport cut = validateWorld(split.world(), t);
    // Sites at x = 2, 4 are west, x = 28 east: the largest group holds 2 of 3.
    CHECK(gate(cut, "settlement_reach").value == 666);
    CHECK(gate(cut, "settlement_reach").status == GateStatus::Fail);
    // POI (10,10) is west with the main group, POI (29,29) east: 1 of 2.
    CHECK(gate(cut, "no_dead_ends").value == 500);
    CHECK(gate(cut, "no_dead_ends").status == GateStatus::Fail);
    CHECK_FALSE(cut.passed());

    HandWorld bridged;
    bridged.wall(16, 2);
    const ValidationReport joined = validateWorld(bridged.world(), t);
    CHECK(gate(joined, "settlement_reach").value == 1000);
    CHECK(gate(joined, "no_dead_ends").value == 1000);
    CHECK(joined.passed());
}

TEST_CASE("lakes and the sea block walking and a cliff blocks it until the slope limit allows it", "[worldforge][validation]")
{
    GateThresholds t;
    t.minSettlementReachPermille = 1000;
    t.minPoiReachPermille = 0;

    HandWorld lake;
    for (int y = 0; y < kGrid; ++y) lake.p.hydrology->lakeDepthM[lake.at(16, y)] = 5;
    CHECK(gate(validateWorld(lake.world(), t), "settlement_reach").value == 666);

    HandWorld sea;
    for (int y = 0; y < kGrid; ++y) sea.p.roads->heightM[sea.at(16, y)] = 0;
    CHECK(gate(validateWorld(sea.world(), t), "settlement_reach").value == 666);
    t.seaLevelM = -5;
    CHECK(gate(validateWorld(sea.world(), t), "settlement_reach").value == 1000);

    // A 60 m step is 3750 permille over a 16 m orthogonal run and 2728 over the 22 m diagonal run, which is the easier crossing.
    HandWorld cliff;
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 16; x < kGrid; ++x) cliff.p.roads->heightM[cliff.at(x, y)] = 70;
    }
    t = GateThresholds{};
    t.maxWalkSlopePermille = 2727;
    CHECK(gate(validateWorld(cliff.world(), t), "settlement_reach").value == 666);
    t.maxWalkSlopePermille = 2728;
    CHECK(gate(validateWorld(cliff.world(), t), "settlement_reach").value == 1000);
}

TEST_CASE("a world with no walkable settlement scores zero reach, and an empty world scores full", "[worldforge][validation]")
{
    HandWorld drowned;
    std::fill(drowned.p.roads->heightM.begin(), drowned.p.roads->heightM.end(), static_cast<std::int16_t>(-3));
    const ValidationReport r = validateWorld(drowned.world());
    CHECK(gate(r, "settlement_reach").value == 0);
    CHECK(gate(r, "no_dead_ends").value == 0);
    CHECK_FALSE(r.passed());

    HandWorld empty;
    empty.p.settlements->sites.clear();
    empty.p.pois->pois.clear();
    const ValidationReport e = validateWorld(empty.world());
    CHECK(gate(e, "settlement_density").status == GateStatus::Fail);
    CHECK(gate(e, "no_dead_ends").value == 1000);
}

TEST_CASE("an incomplete or failed world fails every evaluated gate with its cause", "[worldforge][validation]")
{
    const ForgedWorld failed = forgeWorld(1, 4, smallConfig());
    REQUIRE_FALSE(failed.ok());
    const ValidationReport r = validateWorld(failed);
    CHECK_FALSE(r.passed());
    REQUIRE(r.gates.size() == 8);
    for (const GateResult& g : r.gates) {
        if (g.id == "axiom_directives_starved") continue;
        CHECK(g.status == GateStatus::Fail);
        CHECK(g.cause.find(failed.error()) != std::string::npos);
    }

    const ForgedWorld partial = forgeWorld(1, kSmall, smallConfig(), ForgePhase::Roads);
    REQUIRE(partial.complete());
    const ValidationReport p = validateWorld(partial);
    CHECK_FALSE(p.passed());
    CHECK(gate(p, "quest_items").cause.find("every phase") != std::string::npos);
}

TEST_CASE("out of range thresholds produce an error report that never passes", "[worldforge][validation]")
{
    const ForgedWorld w = HandWorld().world();
    const auto bad = [&](auto mutate) {
        GateThresholds t = lax();
        mutate(t);
        const ValidationReport r = validateWorld(w, t);
        CHECK_FALSE(r.error.empty());
        CHECK(r.gates.empty());
        CHECK_FALSE(r.passed());
    };
    bad([](GateThresholds& t) { t.cellSizeM = 0; });
    bad([](GateThresholds& t) { t.cellSizeM = 1025; });
    bad([](GateThresholds& t) { t.maxWalkSlopePermille = -1; });
    bad([](GateThresholds& t) { t.maxWalkSlopePermille = 100001; });
    bad([](GateThresholds& t) { t.minSettlementsPerKm2Milli = -1; });
    bad([](GateThresholds& t) {
        t.minSettlementsPerKm2Milli = 10;
        t.maxSettlementsPerKm2Milli = 9;
    });
    bad([](GateThresholds& t) { t.maxRoadComponents = 0; });
    bad([](GateThresholds& t) { t.minLandmarkCoveragePermille = 1001; });
    bad([](GateThresholds& t) { t.minPoiReachPermille = -1; });
    bad([](GateThresholds& t) { t.minQuestItems = 70000; });
    bad([](GateThresholds& t) { t.seaLevelM = 30000; });
}

TEST_CASE("validating a real forged world never changes its hash and is repeatable", "[worldforge][validation]")
{
    const ForgedWorld w = forgeWorld(7, kSmall, smallConfig());
    REQUIRE(w.complete());
    const std::uint64_t before = w.worldHash();
    const ValidationReport a = validateWorld(w);
    const ValidationReport b = validateWorld(w);
    CHECK(w.worldHash() == before);
    CHECK(w.worldHash() == forgeWorld(7, kSmall, smallConfig()).worldHash());
    CHECK(validationText(a) == validationText(b));
    CHECK(validationJson(a) == validationJson(b));
    CHECK(a.worldHash == before);

    const GateResult& coverage = gate(a, "landmark_coverage");
    CHECK(coverage.value == w.landmarkCoverage()->coveragePermille);
    CHECK(gate(a, "road_connected").value == w.roads()->components);
    CHECK(gate(a, "quest_items").value == static_cast<std::int64_t>(w.manifest()->items.size()));
    CHECK(gate(a, "quest_cast").value == static_cast<std::int64_t>(w.manifest()->cast.size()));
}

TEST_CASE("the text and JSON reports carry the same gates and parse back", "[worldforge][validation]")
{
    HandWorld h;
    h.wall(16);
    GateThresholds t;
    t.minSettlementReachPermille = 1000;
    const ValidationReport r = validateWorld(h.world(), t);

    const std::string text = validationText(r);
    for (const GateResult& g : r.gates) CHECK(text.find(g.id) != std::string::npos);
    CHECK(text.find("FAIL") != std::string::npos);
    CHECK(text.find("settlements are split across separate walkable areas") != std::string::npos);

    const nlohmann::json j = nlohmann::json::parse(validationJson(r));
    CHECK(j["seed"] == 5);
    CHECK(j["passed"] == false);
    REQUIRE(j["gates"].size() == r.gates.size());
    for (std::size_t i = 0; i < r.gates.size(); ++i) {
        CHECK(j["gates"][i]["id"] == r.gates[i].id);
        CHECK(j["gates"][i]["status"] == std::string(gateStatusName(r.gates[i].status)));
        CHECK(j["gates"][i]["value"] == r.gates[i].value);
        CHECK(j["gates"][i]["cause"] == r.gates[i].cause);
    }
    CHECK(j["gates"][2]["min"] == 1000);
    CHECK(j["gates"][2]["max"].is_null());
    CHECK(j["gates"][0]["max"] == t.maxSettlementsPerKm2Milli);
    CHECK(j["gates"][7]["status"] == "skipped");
}

TEST_CASE("hostile gate text is escaped so the JSON stays valid", "[worldforge][validation]")
{
    ValidationReport r;
    r.error = "bad \"quote\" \\ and \n newline \x01";
    GateResult g;
    g.id = "x\"y";
    g.cause = "line\nbreak\ttab \x1f";
    r.gates.push_back(g);
    const nlohmann::json j = nlohmann::json::parse(validationJson(r));
    CHECK(j["error"] == r.error);
    CHECK(j["gates"][0]["id"] == g.id);
    CHECK(j["gates"][0]["cause"] == g.cause);

    ValidatedForge v;
    v.attempts.push_back({ 0, 1, r });
    CHECK_NOTHROW(nlohmann::json::parse(validatedForgeJson(v)));
}

TEST_CASE("attempt seeds are the world seed first, then the reroll hash", "[worldforge][validation]")
{
    CHECK(attemptSeed(42, 0) == 42);
    CHECK(attemptSeed(42, -3) == 42);
    for (int n = 1; n < kValidateMaxAttempts; ++n) {
        CHECK(attemptSeed(42, n) == rerollSeed(42, static_cast<std::uint32_t>(n)));
        CHECK(attemptSeed(42, n) != attemptSeed(42, n - 1));
    }
}

TEST_CASE("forgeValidated accepts a passing seed on the first attempt", "[worldforge][validation]")
{
    const ValidatedForge v = forgeValidated(7, kSmall, smallConfig(), lax());
    REQUIRE(v.attempts.size() == 1);
    CHECK(v.accepted);
    REQUIRE(v.world.has_value());
    CHECK(v.world->seed() == 7);
    CHECK(v.world->worldHash() == forgeWorld(7, kSmall, smallConfig()).worldHash());
    CHECK(v.attempts[0].seed == 7);
    CHECK(v.attempts[0].report.passed());
}

TEST_CASE("forgeValidated rerolls to the first attempt that passes and logs each cause", "[worldforge][validation]")
{
    const ForgeConfig cfg = smallConfig();
    std::vector<int> coverage;
    for (int n = 0; n < kValidateMaxAttempts; ++n) coverage.push_back(forgeWorld(attemptSeed(11, n), kSmall, cfg).landmarkCoverage()->coveragePermille);
    const int best = *std::max_element(coverage.begin(), coverage.end());
    const int first = static_cast<int>(std::find_if(coverage.begin(), coverage.end(), [&](int c) { return c >= best; }) - coverage.begin());
    REQUIRE(first > 0);

    GateThresholds t = lax();
    t.minLandmarkCoveragePermille = best;
    const ValidatedForge v = forgeValidated(11, kSmall, cfg, t);
    REQUIRE(v.attempts.size() == static_cast<std::size_t>(first) + 1);
    CHECK(v.accepted);
    CHECK(v.world->seed() == attemptSeed(11, first));
    for (int n = 0; n < first; ++n) {
        const ValidationAttempt& a = v.attempts[static_cast<std::size_t>(n)];
        CHECK(a.index == n);
        CHECK(a.seed == attemptSeed(11, n));
        CHECK_FALSE(a.report.passed());
        CHECK(failedIds(a.report) == std::vector<std::string>{ "landmark_coverage" });
        CHECK_FALSE(gate(a.report, "landmark_coverage").cause.empty());
    }
    CHECK(v.attempts.back().report.passed());
}

TEST_CASE("forgeValidated stops at eight attempts and returns the last world marked rejected", "[worldforge][validation]")
{
    GateThresholds t = lax();
    t.minQuestItems = 65536;
    const ValidatedForge v = forgeValidated(3, kSmall, smallConfig(), t, 100);
    CHECK(v.attempts.size() == static_cast<std::size_t>(kValidateMaxAttempts));
    CHECK_FALSE(v.accepted);
    REQUIRE(v.world.has_value());
    CHECK(v.world->seed() == attemptSeed(3, kValidateMaxAttempts - 1));
    CHECK(v.world->worldHash() == forgeWorld(attemptSeed(3, kValidateMaxAttempts - 1), kSmall, smallConfig()).worldHash());
    for (const ValidationAttempt& a : v.attempts) CHECK(gate(a.report, "quest_items").status == GateStatus::Fail);

    CHECK(forgeValidated(3, kSmall, smallConfig(), t, 0).attempts.size() == 1);
    CHECK(forgeValidated(3, kSmall, smallConfig(), t, -5).attempts.size() == 1);
    CHECK(forgeValidated(3, kSmall, smallConfig(), t, 3).attempts.size() == 3);

    const nlohmann::json j = nlohmann::json::parse(validatedForgeJson(v));
    CHECK(j["rejected"] == true);
    CHECK(j["world_seed"] == 3);
    REQUIRE(j["attempts"].size() == static_cast<std::size_t>(kValidateMaxAttempts));
    CHECK(j["attempts"][1]["seed"] == attemptSeed(3, 1));
    CHECK(j["attempts"][1]["report"]["passed"] == false);
}

TEST_CASE("forgeValidated with invalid thresholds stops after one attempt", "[worldforge][validation]")
{
    GateThresholds t;
    t.cellSizeM = 0;
    const ValidatedForge v = forgeValidated(1, kSmall, smallConfig(), t);
    CHECK(v.attempts.size() == 1);
    CHECK_FALSE(v.accepted);
    CHECK_FALSE(v.attempts[0].report.error.empty());
}

TEST_CASE("forgeValidated on a failing forge keeps the forge error as the cause", "[worldforge][validation]")
{
    const ValidatedForge v = forgeValidated(1, 4, smallConfig(), lax(), 2);
    CHECK(v.attempts.size() == 2);
    CHECK_FALSE(v.accepted);
    CHECK_FALSE(v.world->ok());
    CHECK(gate(v.attempts[0].report, "quest_items").cause.find("did not complete") != std::string::npos);
}

TEST_CASE("hand-built hostile worlds fail every gate instead of crashing", "[worldforge][validation]")
{
    const auto allFail = [](const ForgedWorld& w) {
        const ValidationReport r = validateWorld(w);
        CHECK_FALSE(r.passed());
        REQUIRE(r.gates.size() == 8);
        for (const GateResult& g : r.gates) {
            if (g.id == "axiom_directives_starved") continue;
            CHECK(g.status == GateStatus::Fail);
            CHECK_FALSE(g.cause.empty());
        }
    };
    HandWorld zero;
    zero.p.size = 0;
    allFail(zero.world());
    HandWorld huge;
    huge.p.size = 2147483647;
    allFail(huge.world());
    HandWorld noRoads;
    noRoads.p.roads.reset();
    allFail(noRoads.world());
    HandWorld noManifest;
    noManifest.p.manifest.reset();
    allFail(noManifest.world());
    HandWorld shortLayer;
    shortLayer.p.hydrology->river.pop_back();
    allFail(shortLayer.world());
    HandWorld shortRoad;
    shortRoad.p.roads->road.clear();
    allFail(shortRoad.world());
    HandWorld wrongWidth;
    wrongWidth.p.roads->width = 16;
    allFail(wrongWidth.world());
    HandWorld outside;
    outside.p.settlements->sites[1].x = -1;
    allFail(outside.world());
    HandWorld past;
    past.p.pois->pois[0].y = kGrid;
    allFail(past.world());
}

TEST_CASE("points of interest with no settlement to reach them are dead ends", "[worldforge][validation]")
{
    HandWorld h;
    h.p.settlements->sites.clear();
    const ValidationReport r = validateWorld(h.world(), lax());
    CHECK(gate(r, "no_dead_ends").value == 0);
    CHECK(gate(r, "settlement_density").value == 0);
}

TEST_CASE("a gate that could not be evaluated still reports its thresholds", "[worldforge][validation]")
{
    const ValidationReport r = validateWorld(forgeWorld(1, 4, smallConfig()));
    const GateThresholds t;
    CHECK(gate(r, "settlement_density").min == t.minSettlementsPerKm2Milli);
    CHECK(gate(r, "settlement_density").max == t.maxSettlementsPerKm2Milli);
    CHECK(gate(r, "road_connected").max == t.maxRoadComponents);
    CHECK(gate(r, "landmark_coverage").min == t.minLandmarkCoveragePermille);
}
