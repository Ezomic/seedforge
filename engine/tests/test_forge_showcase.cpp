#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>
#include <forgecli/showcase.h>

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using namespace forgecli;

namespace {

ShowcaseMetrics balanced(std::uint64_t seed = 1)
{
    ShowcaseMetrics m;
    m.seed = seed;
    m.size = 256;
    m.gatesPassed = 8;
    m.gatesEvaluated = 8;
    m.landSharePermille = 420;
    m.riverSystems = 12;
    m.lakes = 4;
    m.settlements = 20;
    m.tiersPresent = 4;
    m.walkMeasured = true;
    m.walkArrived = true;
    m.tripMetres = 400;
    m.walkedMetres = 430;
    m.spawnFound = true;
    m.pocketSpawn = false;
    m.biomesNearSpawn = 6;
    return m;
}

ShowcaseMetrics mostlyOcean(std::uint64_t seed = 2)
{
    ShowcaseMetrics m = balanced(seed);
    m.landSharePermille = 40;
    m.riverSystems = 0;
    m.lakes = 0;
    m.settlements = 2;
    m.tiersPresent = 1;
    m.walkMeasured = false;
    m.walkArrived = false;
    m.biomesNearSpawn = 1;
    return m;
}

std::vector<std::uint64_t> order(const std::vector<ShowcaseScore>& ranked)
{
    std::vector<std::uint64_t> seeds;
    for (const ShowcaseScore& s : ranked) seeds.push_back(s.seed);
    return seeds;
}

ShowcaseWeights without(ShowcaseTerm term)
{
    ShowcaseWeights w;
    w.weight[static_cast<std::size_t>(term)] = 0;
    return w;
}

}

TEST_CASE("the weights live in one place and add up to a hundred points", "[forge_cli][showcase]")
{
    const int sum = std::accumulate(kShowcaseWeights.weight.begin(), kShowcaseWeights.weight.end(), 0);
    CHECK(sum == 100);
    CHECK(scoreShowcase(balanced()).totalMilli == 100000);
}

TEST_CASE("scoring is deterministic and reads only the metrics", "[forge_cli][showcase]")
{
    const ShowcaseScore a = scoreShowcase(mostlyOcean());
    const ShowcaseScore b = scoreShowcase(mostlyOcean());
    CHECK(a.totalMilli == b.totalMilli);
    for (std::size_t i = 0; i < a.terms.size(); ++i) {
        CHECK(a.terms[i].pointsMilli == b.terms[i].pointsMilli);
        CHECK(a.terms[i].note == b.terms[i].note);
    }
    CHECK(pointsText(87400) == "87.4");
    CHECK(pointsText(0) == "0.0");
}

TEST_CASE("a mostly ocean world scores below a balanced one", "[forge_cli][showcase]")
{
    const ShowcaseScore ocean = scoreShowcase(mostlyOcean());
    const ShowcaseScore good = scoreShowcase(balanced());
    CHECK(ocean.totalMilli < good.totalMilli);
    CHECK(ocean.terms[static_cast<std::size_t>(ShowcaseTerm::LandShare)].permille == 0);
    CHECK(good.terms[static_cast<std::size_t>(ShowcaseTerm::LandShare)].permille == 1000);
}

TEST_CASE("land share is full inside the band and falls away on both sides", "[forge_cli][showcase]")
{
    const auto land = [](int permille) {
        ShowcaseMetrics m = balanced();
        m.landSharePermille = permille;
        return scoreShowcase(m).terms[static_cast<std::size_t>(ShowcaseTerm::LandShare)].permille;
    };
    CHECK(land(250) == 1000);
    CHECK(land(650) == 1000);
    CHECK(land(200) < 1000);
    CHECK(land(200) > land(100));
    CHECK(land(700) > land(800));
    CHECK(land(100) == 0);
    CHECK(land(1000) == 0);
    CHECK(land(-1) == 0);
}

TEST_CASE("every term scores what it measures", "[forge_cli][showcase]")
{
    const auto scoreOf = [](ShowcaseMetrics m, ShowcaseTerm t) { return scoreShowcase(m).terms[static_cast<std::size_t>(t)].permille; };
    ShowcaseMetrics m = balanced();
    m.gatesPassed = 7;
    CHECK(scoreOf(m, ShowcaseTerm::Gates) == 0);
    m = balanced();
    m.riverSystems = 6;
    CHECK(scoreOf(m, ShowcaseTerm::Rivers) == 500);
    m = balanced();
    m.lakes = 1;
    CHECK(scoreOf(m, ShowcaseTerm::Lakes) == 250);
    m = balanced();
    m.settlements = 10;
    CHECK(scoreOf(m, ShowcaseTerm::Settlements) == 500);
    m = balanced();
    m.tiersPresent = 1;
    CHECK(scoreOf(m, ShowcaseTerm::TierSpread) == 250);
    m = balanced();
    m.walkArrived = false;
    CHECK(scoreOf(m, ShowcaseTerm::WalkArrival) == 0);
    m = balanced();
    m.biomesNearSpawn = 3;
    CHECK(scoreOf(m, ShowcaseTerm::BiomeVariety) == 500);
    m = balanced();
    m.pocketSpawn = true;
    CHECK(scoreOf(m, ShowcaseTerm::Spawn) == 0);
    m = balanced();
    m.spawnFound = false;
    CHECK(scoreOf(m, ShowcaseTerm::Spawn) == 0);
    CHECK(scoreOf(m, ShowcaseTerm::BiomeVariety) == 0);
}

TEST_CASE("a world that did not forge scores nothing and a hostile count cannot overflow", "[forge_cli][showcase]")
{
    ShowcaseMetrics failed = balanced();
    failed.error = "the world did not forge";
    CHECK(scoreShowcase(failed).totalMilli == 0);

    ShowcaseMetrics hostile = balanced();
    hostile.riverSystems = 2000000000;
    hostile.settlements = -5;
    hostile.biomesNearSpawn = 2000000000;
    hostile.gatesEvaluated = 0;
    const ShowcaseScore score = scoreShowcase(hostile);
    CHECK(score.terms[static_cast<std::size_t>(ShowcaseTerm::Rivers)].permille == 1000);
    CHECK(score.terms[static_cast<std::size_t>(ShowcaseTerm::Settlements)].permille == 0);
    CHECK(score.terms[static_cast<std::size_t>(ShowcaseTerm::Gates)].permille == 0);
    CHECK(score.totalMilli <= 100000);
}

TEST_CASE("removing a term changes the ranking", "[forge_cli][showcase]")
{
    // X has the rivers but its walk is blocked; Y has no rivers but its walk arrives.
    ShowcaseMetrics x = balanced(10);
    x.walkArrived = false;
    ShowcaseMetrics y = balanced(20);
    y.riverSystems = 0;

    const auto rank = [&](const ShowcaseWeights& w) { return order(rankShowcase({scoreShowcase(x, w), scoreShowcase(y, w)})); };
    CHECK(rank(kShowcaseWeights) == std::vector<std::uint64_t>{20, 10});
    CHECK(rank(without(ShowcaseTerm::WalkArrival)) == std::vector<std::uint64_t>{10, 20});
    CHECK(rank(without(ShowcaseTerm::Rivers)) == std::vector<std::uint64_t>{20, 10});

    // Every term matters to some pair: zeroing it lowers the perfect world's score by exactly its weight.
    for (int t = 0; t < kShowcaseTermCount; ++t) {
        const ShowcaseWeights w = without(static_cast<ShowcaseTerm>(t));
        CHECK(scoreShowcase(balanced(), w).totalMilli == 100000 - kShowcaseWeights.weight[static_cast<std::size_t>(t)] * 1000);
    }
}

TEST_CASE("ranking is best first with ties going to the lower seed", "[forge_cli][showcase]")
{
    std::vector<ShowcaseScore> scores = {scoreShowcase(mostlyOcean(7)), scoreShowcase(balanced(9)), scoreShowcase(balanced(3)), scoreShowcase(mostlyOcean(5))};
    CHECK(order(rankShowcase(scores)) == std::vector<std::uint64_t>{3, 9, 5, 7});
    std::reverse(scores.begin(), scores.end());
    CHECK(order(rankShowcase(scores)) == std::vector<std::uint64_t>{3, 9, 5, 7});
    CHECK(rankShowcase({}).empty());
}

TEST_CASE("the tables carry one row per seed and the top records are a data file", "[forge_cli][showcase]")
{
    const std::vector<ShowcaseMetrics> metrics = {mostlyOcean(2), balanced(1)};
    const std::vector<ShowcaseScore> ranked = rankShowcase({scoreShowcase(metrics[0]), scoreShowcase(metrics[1])});

    const std::string csv = showcaseCsv(ranked, metrics);
    CHECK(std::count(csv.begin(), csv.end(), '\n') == 3);
    CHECK(csv.rfind("rank,seed,size,score,gates_points", 0) == 0);
    CHECK(csv.find("\n1,1,256,100.0,") != std::string::npos);

    const std::string md = showcaseMarkdown(ranked, metrics, 1);
    CHECK(md.find("| 1 | 1 | 100.0 |") != std::string::npos);
    CHECK(md.find("## Top 1") != std::string::npos);
    CHECK(md.find("seed 2, ") == std::string::npos);

    const std::string json = showcaseSeedsJson(ranked, 1);
    CHECK(json.find("\"id\": \"showcase_1\"") != std::string::npos);
    CHECK(json.find("\"seed\": 1,") != std::string::npos);
    CHECK(json.find("\"size\": 256") != std::string::npos);
    CHECK(json.find("\"score\": 100.0") != std::string::npos);
    CHECK(json.find("showcase_2") == std::string::npos);
    CHECK(showcaseSeedsJson({}, 5) == "[]\n");
    CHECK(showcaseCsv({}, {}).find('\n') != std::string::npos);
}

TEST_CASE("reasons name the strong terms first and mark the ones that cost points", "[forge_cli][showcase]")
{
    const std::vector<std::string> reasons = showcaseReasons(scoreShowcase(mostlyOcean()));
    REQUIRE(reasons.size() == static_cast<std::size_t>(kShowcaseTermCount));
    CHECK(reasons.front().find("weak:") == std::string::npos);
    CHECK(reasons.back().find("weak:") == 0);
    CHECK(std::any_of(reasons.begin(), reasons.end(), [](const std::string& r) { return r.find("land 4.0%") != std::string::npos; }));
}

TEST_CASE("river and lake counts come from connected components of the hydrology masks", "[forge_cli][showcase]")
{
    worldforge::HydrologyMap h;
    h.width = 16;
    h.height = 16;
    h.river.assign(256, 0);
    h.lakeDepthM.assign(256, 0);
    for (int x = 0; x < 12; ++x) h.river[static_cast<std::size_t>(2 * 16 + x)] = 1;      // one river of 12 cells
    for (int x = 0; x < 12; ++x) h.river[static_cast<std::size_t>(8 * 16 + x)] = 1;      // a second one
    for (int x = 0; x < 3; ++x) h.river[static_cast<std::size_t>(14 * 16 + x)] = 1;      // a trickle, too short to count
    for (int y = 4; y < 6; ++y) {
        for (int x = 13; x < 15; ++x) h.lakeDepthM[static_cast<std::size_t>(y * 16 + x)] = 3;    // a 4 cell lake
    }
    h.lakeDepthM[static_cast<std::size_t>(11 * 16 + 1)] = 2;                                      // a puddle
    CHECK(countRiverSystems(h) == 2);
    CHECK(countRiverSystems(h, 3) == 3);
    CHECK(countLakes(h) == 1);
    CHECK(countLakes(h, 1) == 2);

    worldforge::HydrologyMap empty;
    CHECK(countRiverSystems(empty) == 0);
    CHECK(countLakes(empty) == 0);
}

TEST_CASE("live worlds: a mostly ocean seed found by search scores below a balanced one", "[forge_cli][showcase]")
{
    constexpr int kSize = 128;
    const worldforge::ForgeConfig config = defaultForgeConfig(kSize);
    std::optional<ShowcaseScore> ocean;
    std::optional<ShowcaseScore> good;
    std::string log;
    for (std::uint64_t seed = 1; seed <= 40 && !(ocean && good); ++seed) {
        const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, kSize, config, worldforge::ForgePhase::Content);
        if (!world.ok()) continue;
        const worldforge::ValidationReport report = worldforge::validateWorld(world, sanityThresholds());
        WalkProbeResult walk;
        walk.spawnFound = true;
        walk.metresPerCell = 32.0;
        walk.spawnX = world.size() * 16.0;
        walk.spawnZ = world.size() * 16.0;
        const ShowcaseMetrics m = measureShowcase(world, report, walk);
        log += "seed " + std::to_string(seed) + " land " + std::to_string(m.landSharePermille) + " gates " + std::to_string(m.gatesPassed) + "/" + std::to_string(m.gatesEvaluated) + "\n";
        if (!ocean && m.landSharePermille >= 0 && m.landSharePermille < 150) ocean = scoreShowcase(m);
        if (!good && m.landSharePermille >= 250 && m.landSharePermille <= 650 && m.gatesPassed == m.gatesEvaluated) good = scoreShowcase(m);
    }
    INFO(log);
    REQUIRE(ocean.has_value());
    REQUIRE(good.has_value());
    CHECK(ocean->totalMilli < good->totalMilli);
}

TEST_CASE("measuring a live world is repeatable and reads its rivers, settlements and gates", "[forge_cli][showcase]")
{
    constexpr int kSize = 128;
    const worldforge::ForgedWorld world = worldforge::forgeWorld(3, kSize, defaultForgeConfig(kSize), worldforge::ForgePhase::Content);
    REQUIRE(world.ok());
    const worldforge::ValidationReport report = worldforge::validateWorld(world, sanityThresholds());
    const ShowcaseMetrics a = measureShowcase(world, report, WalkProbeResult{});
    const ShowcaseMetrics b = measureShowcase(world, report, WalkProbeResult{});
    CHECK(a.error.empty());
    CHECK(a.gatesEvaluated > 0);
    CHECK(a.settlements == static_cast<int>(world.settlements()->sites.size()));
    CHECK(a.landSharePermille == b.landSharePermille);
    CHECK(a.riverSystems == b.riverSystems);
    CHECK(a.lakes == b.lakes);
    CHECK_FALSE(a.spawnFound);
    CHECK(scoreShowcase(a).totalMilli == scoreShowcase(b).totalMilli);
    CHECK(scoreShowcase(a).terms[static_cast<std::size_t>(ShowcaseTerm::Spawn)].permille == 0);
}

TEST_CASE("the run rejects an empty range, a bad size and a bad top", "[forge_cli][showcase]")
{
    ShowcaseOptions o;
    o.seedBegin = 5;
    o.seedEnd = 4;
    CHECK_FALSE(runShowcase(o, {}).error.empty());
    o.seedEnd = 5 + kMaxShowcaseSeeds;
    CHECK_FALSE(runShowcase(o, {}).error.empty());
    o.seedBegin = std::numeric_limits<std::uint64_t>::max() - 1;
    o.seedEnd = std::numeric_limits<std::uint64_t>::max();
    o.size = 64;
    const ShowcaseResult edge = runShowcase(o, {});
    CHECK(edge.error.empty());
    CHECK(edge.metrics.size() == 2);
    o.seedBegin = 5;
    o.seedEnd = 5;
    o.size = 1;
    CHECK_FALSE(runShowcase(o, {}).error.empty());
    o.size = 64;
    o.top = 0;
    CHECK_FALSE(runShowcase(o, {}).error.empty());
}
