#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>
#include <forgecli/showcase.h>
#include <forgecli/walk_probe.h>

#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walking.h"
#include "ichor/world/heightfield.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <cstdlib>
#include <utility>
#include <string>
#include <vector>

using namespace forgecli;

namespace {

// A square heightfield n samples to a side, one metre apart, heights in tenths of a metre.
ichor::Heightfield gridOf(std::uint32_t n, const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& tenths)
{
    ichor::WorldDesc desc;
    desc.sizeMetres = n - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = n - 1;
    desc.heightRange = 6553.5f;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) samples[static_cast<std::size_t>(z) * n + x] = tenths(x, z);
    }
    return ichor::Heightfield(desc, std::move(samples));
}

constexpr std::uint16_t kBase = 10000;

// A ridge across the map at x = 60 to 64, 2.5 m of rise a metre: past the walk limit. It runs the full
// length except for a gap at z >= gapFrom.
ichor::Heightfield ridgeWithGap(std::uint32_t gapFrom)
{
    return gridOf(129, [gapFrom](std::uint32_t x, std::uint32_t z) {
        if (z >= gapFrom || x < 60 || x > 64) return kBase;
        const std::uint32_t up = std::min(x - 60, 64 - x);
        return static_cast<std::uint16_t>(kBase + up * 25);
    });
}

// A pit: a flat floor 20 m across ringed by a wall too steep to walk.
ichor::Heightfield pit()
{
    return gridOf(257, [](std::uint32_t x, std::uint32_t z) {
        const int dx = static_cast<int>(x) - 128;
        const int dz = static_cast<int>(z) - 128;
        const int d = std::max(std::abs(dx), std::abs(dz));
        if (d <= 10) return kBase;
        if (d >= 14) return static_cast<std::uint16_t>(kBase + 40 * 25);
        return static_cast<std::uint16_t>(kBase + (d - 10) * 25 * 3);
    });
}

}

TEST_CASE("on open ground the walker reaches as far as it is allowed to look", "[forge_cli][showcase][walk]")
{
    const ichor::Heightfield flat = gridOf(257, [](std::uint32_t, std::uint32_t) { return kBase; });
    const ichor::ForgedWalking walking(flat, nullptr, ichor::CapabilitySet{});
    CHECK(walkReachMetres(walking, 128.0, 128.0, 100.0) == 100.0);
    CHECK(walkReachMetres(walking, 128.0, 128.0, 40.0) == 40.0);
}

TEST_CASE("a pit ringed by steep ground is a pocket and a start off the map or not a number reaches nothing", "[forge_cli][showcase][walk]")
{
    const ichor::Heightfield walled = pit();
    const ichor::ForgedWalking walking(walled, nullptr, ichor::CapabilitySet{});
    const double inside = walkReachMetres(walking, 128.0, 128.0, 100.0, 2.0);
    CHECK(inside > 4.0);
    CHECK(inside < 25.0);

    CHECK(walkReachMetres(walking, -500.0, 128.0, 100.0) == 0.0);
    CHECK(walkReachMetres(walking, std::numeric_limits<double>::quiet_NaN(), 128.0, 100.0) == 0.0);
    CHECK(walkReachMetres(walking, 128.0, 128.0, 100.0, 0.0) == 0.0);
}

TEST_CASE("the route goes round a ridge the straight walk cannot cross, and there is no route when the ridge is whole", "[forge_cli][showcase][walk]")
{
    const glm::dvec3 from{20.0, 0.0, 20.0};
    const glm::dvec3 to{100.0, 0.0, 20.0};

    const ichor::Heightfield gapped = ridgeWithGap(100);
    const ichor::ForgedWalking walking(gapped, nullptr, ichor::CapabilitySet{});

    // Heading straight is stopped by the ridge: this is what the route has to beat.
    const ichor::WalkResult straight = ichor::walkTo(walking, from, to, {});
    CHECK(straight.outcome == ichor::WalkOutcome::Blocked);
    CHECK(straight.blocker == ichor::WalkBlock::TooSteep);

    std::vector<glm::dvec3> path;
    REQUIRE(routeOnFoot(walking, from, to, path));
    CHECK(path.size() >= 2);
    CHECK(std::any_of(path.begin(), path.end(), [](const glm::dvec3& p) { return p.z >= 100.0; }));

    ichor::WalkBotParams bot;
    bot.arriveMetres = 4.0;
    const ichor::WalkResult routed = ichor::walkTo(walking, from, to, path, bot);
    CHECK(routed.outcome == ichor::WalkOutcome::Arrived);
    CHECK(routed.metresWalked > 160.0);

    const ichor::Heightfield whole = ridgeWithGap(10000);
    const ichor::ForgedWalking blocked(whole, nullptr, ichor::CapabilitySet{});
    CHECK_FALSE(routeOnFoot(blocked, from, to, path));
    CHECK(path.empty());
}

TEST_CASE("the route refuses points that are not numbers or lie off the map", "[forge_cli][showcase][walk]")
{
    const ichor::Heightfield gapped = ridgeWithGap(100);
    const ichor::ForgedWalking walking(gapped, nullptr, ichor::CapabilitySet{});
    std::vector<glm::dvec3> path;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(routeOnFoot(walking, {nan, 0.0, 20.0}, {100.0, 0.0, 20.0}, path));
    CHECK_FALSE(routeOnFoot(walking, {20.0, 0.0, 20.0}, {100.0, 0.0, nan}, path));
    CHECK_FALSE(routeOnFoot(walking, {20.0, 0.0, 20.0}, {1.0e9, 0.0, 20.0}, path));
    CHECK(path.empty());
}

namespace {

struct LiveWalk {
    int visited = 0;
    int spawned = 0;
    int withTarget = 0;
    int arrived = 0;
    int routed = 0;
    std::string log;
};

// Probes seeds 1 to lastSeed on a 128 grid. Every probe is a forge, a heightfield bake and a route search, so the
// PR suite visits as few seeds as show the walk working and stops once `enough` is met; the repeat check (a second
// probe of the same world) runs on the first seed only. The route search is the expensive part, hence `params`.
LiveWalk probeLiveSeeds(std::uint64_t lastSeed, const WalkProbeParams& params, bool stopWhenEnough)
{
    constexpr int kSize = 128;
    const worldforge::ForgeConfig config = defaultForgeConfig(kSize);
    LiveWalk live;
    for (std::uint64_t seed = 1; seed <= lastSeed; ++seed) {
        const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, kSize, config, worldforge::ForgePhase::Content);
        REQUIRE(world.ok());
        const WalkProbeResult a = probeWalk(world, params);
        ++live.visited;
        if (seed == 1 || !stopWhenEnough) {
            const WalkProbeResult b = probeWalk(world, params);
            CHECK(a.spawnFound == b.spawnFound);
            CHECK(a.spawnX == b.spawnX);
            CHECK(a.spawnZ == b.spawnZ);
            CHECK(a.arrived == b.arrived);
            CHECK(a.walkedMetres == b.walkedMetres);
        }
        CHECK(a.error.empty());
        live.log += "seed " + std::to_string(seed) + " spawn " + (a.spawnFound ? "yes" : "no") + " reach " + std::to_string(a.reachMetres) + " target " + (a.hasTarget ? "yes" : "no")
            + " trip " + std::to_string(a.tripMetres) + " route " + (a.routeFound ? "yes" : "no") + " arrived " + (a.arrived ? "yes" : "no") + " " + a.blocker + '\n';
        if (!a.spawnFound) continue;
        ++live.spawned;
        CHECK(a.metresPerCell > 0.0);
        if (a.hasTarget) {
            ++live.withTarget;
            CHECK(a.tripMetres >= WalkProbeParams{}.minTripMetres);
            CHECK(a.walkedMetres > 0.0);
            CHECK((a.arrived || !a.blocker.empty()));
        }
        if (a.routeFound) ++live.routed;
        if (a.arrived) ++live.arrived;
        if (stopWhenEnough && live.spawned >= 3 && live.arrived >= 2) break;
    }
    return live;
}

}

TEST_CASE("live worlds: the probe finds a spawn, repeats itself, and the walk reaches a settlement on some seed", "[forge_cli][showcase][walk]")
{
    WalkProbeParams cheap;
    cheap.route.stepMetres = 8.0;
    const LiveWalk live = probeLiveSeeds(6, cheap, true);
    INFO(live.log);
    // Every visited seed must have a spawn, and the walk must get somewhere on two of them: the metric goes red
    // if the spawn search, the route or the walk bot stops working.
    CHECK(live.spawned == live.visited);
    CHECK(live.withTarget >= 3);
    CHECK(live.routed >= 2);
    CHECK(live.arrived >= 2);
}

TEST_CASE("live worlds, all eight seeds at the default route step", "[.showcase_heavy]")
{
    const LiveWalk live = probeLiveSeeds(8, WalkProbeParams{}, false);
    INFO(live.log);
    CHECK(live.spawned >= 6);
    CHECK(live.withTarget >= 4);
    CHECK(live.arrived >= 1);
}

TEST_CASE("the probe says why it could not run on a world that stopped early", "[forge_cli][showcase][walk]")
{
    const worldforge::ForgedWorld early = worldforge::forgeWorld(1, 64, defaultForgeConfig(64), worldforge::ForgePhase::Hydrology);
    const WalkProbeResult r = probeWalk(early);
    CHECK_FALSE(r.error.empty());
    CHECK_FALSE(r.spawnFound);
    CHECK(makeWalkProbe()(early).error == r.error);
}

namespace {

// A run forges, probes and ranks every seed, then draws a gallery cell per top seed, and the test runs it twice: the PR
// suite runs it with a stand-in probe (the real probe is covered by the live worlds test above) so it stays cheap under
// the sanitizers; the hidden variant runs the real probe over six seeds.
void checkShowcaseRun(std::uint64_t lastSeed, int top, const WalkProbe& probe)
{
    const std::filesystem::path dir = ichor::test::tempPath("showcase_run");
    const std::filesystem::path json = ichor::test::tempPath("showcase_seeds.json");
    std::filesystem::remove_all(dir);

    ShowcaseOptions options;
    options.seedBegin = 1;
    options.seedEnd = lastSeed;
    options.size = 128;
    options.top = top;
    options.outDir = dir.string();
    options.seedsJson = json.string();

    const ShowcaseResult first = runShowcase(options, probe);
    REQUIRE(first.error.empty());
    REQUIRE(first.metrics.size() == lastSeed);
    REQUIRE(first.ranked.size() == lastSeed);
    for (std::size_t i = 1; i < first.ranked.size(); ++i) CHECK(first.ranked[i - 1].totalMilli >= first.ranked[i].totalMilli);

    std::size_t thumbnails = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir / "top")) {
        std::ifstream f(entry.path(), std::ios::binary);
        char magic[4] = {};
        f.read(magic, 4);
        CHECK(std::string(magic + 1, 3) == "PNG");
        ++thumbnails;
    }
    CHECK(thumbnails == static_cast<std::size_t>(top));
    CHECK(std::filesystem::exists(dir / "ranking.csv"));
    CHECK(std::filesystem::exists(dir / "ranking.md"));

    std::ifstream written(json);
    const std::string text((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    CHECK(text == first.json);
    CHECK(text.find("seed_" + std::to_string(first.ranked.front().seed) + "_size_128_top") != std::string::npos);

    const ShowcaseResult second = runShowcase(options, probe);
    CHECK(second.csv == first.csv);
    CHECK(second.json == first.json);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::remove(json, ec);
}


}

TEST_CASE("a showcase run ranks the seeds, writes the tables, thumbnails and the data file, and is repeatable", "[forge_cli][showcase][walk]")
{
    // A probe that answers from the world alone: it differs per seed, so the ranking has something to order.
    const WalkProbe standIn = [](const worldforge::ForgedWorld& world) {
        WalkProbeResult r;
        r.spawnFound = true;
        r.metresPerCell = 4096.0 / (world.size() - 1);
        r.spawnX = 2048.0;
        r.spawnZ = 2048.0 + static_cast<double>(world.seed()) * 100.0;
        r.hasTarget = true;
        r.tripMetres = 300.0;
        r.arrived = world.seed() % 2 == 1;
        r.walkedMetres = 300.0;
        r.reachMetres = 160.0;
        return r;
    };
    checkShowcaseRun(3, 2, standIn);
}

TEST_CASE("a showcase run over six seeds at the default route step", "[.showcase_heavy]")
{
    checkShowcaseRun(6, 3, makeWalkProbe());
}

TEST_CASE("with no probe the walk and spawn terms score nothing and the run still ranks", "[forge_cli][showcase][walk]")
{
    ShowcaseOptions options;
    options.seedBegin = 1;
    options.seedEnd = 3;
    options.size = 128;
    const ShowcaseResult r = runShowcase(options, {});
    REQUIRE(r.error.empty());
    for (const ShowcaseScore& s : r.ranked) {
        CHECK(s.terms[static_cast<std::size_t>(ShowcaseTerm::WalkArrival)].permille == 0);
        CHECK(s.terms[static_cast<std::size_t>(ShowcaseTerm::Spawn)].permille == 0);
    }
}
