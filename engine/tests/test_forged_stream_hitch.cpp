#include "forged_world_fixture.h"

#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walk_route.h"
#include "ichor/world/forged_walking.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;

// SEED-710: a streamer hitch guard. test_forged_frame_cost.cpp counts what a frame costs; a hitch is a frame that takes
// long, so this one measures time. The walk bot walks the forged world of seed 1 from the spawn to the nearest settlements
// over ground routes, and every step is a tick: the walk step and the streamer's update, as the app's "stream" stage runs
// it, with the simulation held while the required ring is not resident (main.cpp's tick gate). The ticks judged are those of
// the crossing steps: the tick that crosses, where a cell built on the tick thread lands, and the frames held after it, where
// a finished payload is applied. Their worst must stay within a budget stated against the walk's own median tick, a multiple
// plus a floor, so a slow or noisy runner moves both sides while a streamer that blocks the tick blows the worst tick alone.
// The same walk with synchronous streaming is that broken streamer, and the budget must turn it red.
//
// Calibration (five local Release runs and a CI run, the PR of SEED-710 has the table): the median tick is about 0.01 ms,
// the asynchronous worst crossing tick 0.4 to 1.8 ms, the synchronous one 146 to 193 ms (the five cells of a new column).

namespace {

constexpr int kGrid = 129;
constexpr std::uint64_t kSeed = 1;
constexpr std::size_t kLegs = 3;        // the spawn, then this many settlements in turn
constexpr std::size_t kMinCrossings = 3; // cell boundaries the walk must cross for the measurement to mean anything
constexpr double kWorstOverMedian = 500.0;
constexpr double kFloorMs = 40.0;
constexpr double kArriveMetres = 12.0;
constexpr auto kWaitSlice = std::chrono::milliseconds(1); // the frame the held simulation would have spent waiting

using Clock = std::chrono::steady_clock;

struct Leg {
    glm::dvec3 goal{0.0};
    std::vector<glm::dvec3> waypoints;
};

struct Crossing {
    std::size_t tick = 0; // the tick on which the walker's cell changed
    std::size_t end = 0;  // one past the last tick of that step: the crossing tick and the held frames until the ring was ready
};

struct Walk {
    std::vector<double> ticksMs;            // every tick of the walk after the ring around the spawn was resident
    std::vector<Crossing> crossings;
    std::uint64_t loads = 0;
    std::size_t legsArrived = 0;
    std::string stopped;                    // why a leg ended short, when one did
};

double budgetMs(double medianMs)
{
    return kWorstOverMedian * medianMs + kFloorMs;
}

double median(std::vector<double> values)
{
    REQUIRE_FALSE(values.empty());
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

double worst(const std::vector<double>& values, std::size_t begin, std::size_t end)
{
    REQUIRE(begin < end);
    REQUIRE(end <= values.size());
    return *std::max_element(values.begin() + static_cast<std::ptrdiff_t>(begin), values.begin() + static_cast<std::ptrdiff_t>(end));
}

double worst(const std::vector<double>& values)
{
    return worst(values, 0, values.size());
}

// The worst tick of the crossing steps: a cell built on the tick thread lands on the crossing tick, a payload applied
// on one of the frames held after it. The rest of the walk is reported, not judged, so a runner's hiccup elsewhere is not a hitch.
double worstCrossing(const Walk& walk)
{
    double out = 0.0;
    for (const Crossing& crossing : walk.crossings) {
        out = std::max(out, worst(walk.ticksMs, crossing.tick, crossing.end));
    }
    return out;
}

std::unique_ptr<ForgedGame> forgeGame()
{
    WorldDesc desc;
    desc.seed = kSeed;
    const test::ForgedFixture forged = test::forgedFixture(desc, kGrid);
    INFO(forged.error);
    REQUIRE(forged.ok());
    std::unique_ptr<ForgedGame> game = buildForgedGame(forged.result, desc);
    INFO(game->error);
    REQUIRE(game->ok());
    return game;
}

// The streamer as the windowed app configures it for a forged world, except the navmesh (nothing consumes tiles headless).
StreamerConfig streamingFor(const ForgedGame& game, bool synchronous)
{
    StreamerConfig config;
    config.synchronous = synchronous;
    config.vegetation = true;
    config.water = true;
    config.rivers = game.water.rivers;
    config.hydrology = game.water.hydrology;
    config.biomes = game.biomes;
    config.scenery = forgedSceneryGuard(game);
    config.pois = game.pois;
    config.buildings = game.buildings;
    config.streets = game.streets;
    return config;
}

// From the spawn to the nearest settlement a ground route reaches, then on to the next nearest, kLegs times.
std::vector<Leg> planLegs(const ForgedGame& game, const ForgedWalking& walking)
{
    std::vector<Leg> legs;
    std::vector<bool> visited(game.sites.size(), false);
    glm::dvec3 from{game.spawn.x, game.heightfield.heightAt(game.spawn.x, game.spawn.z), game.spawn.z};
    WalkRouteParams params;
    params.goalRadiusMetres = kArriveMetres - 2.0;
    WalkRouteMemo memo;
    while (legs.size() < kLegs) {
        std::vector<std::pair<double, std::size_t>> ranked;
        for (std::size_t i = 0; i < game.sites.size(); ++i) {
            if (!visited[i]) {
                ranked.emplace_back(std::hypot(game.sites[i].x - from.x, game.sites[i].z - from.z), i);
            }
        }
        std::sort(ranked.begin(), ranked.end());
        bool found = false;
        for (const auto& [distance, index] : ranked) {
            visited[index] = true;
            if (distance < kArriveMetres) {
                continue;
            }
            const glm::dvec3 goal{game.sites[index].x, game.heightfield.heightAt(game.sites[index].x, game.sites[index].z), game.sites[index].z};
            const WalkRoute route = findWalkRoute(walking, from, goal, params, &memo);
            if (!route.found) {
                continue;
            }
            legs.push_back({goal, route.waypoints});
            from = goal;
            found = true;
            break;
        }
        if (!found) {
            break;
        }
    }
    return legs;
}

Walk walkLegs(const ForgedGame& game, const ForgedWalking& walking, const std::vector<Leg>& legs, bool synchronous)
{
    Walk walk;
    WorldStreamer streamer(game.heightfield, streamingFor(game, synchronous));
    streamer.onLoaded([&](const CellPayload&) { ++walk.loads; });

    glm::dvec3 position{game.spawn.x, game.heightfield.heightAt(game.spawn.x, game.spawn.z), game.spawn.z};
    // The ring around the spawn is the loading screen, not a tick of play.
    streamer.update(position);
    while (!streamer.ready(position)) {
        std::this_thread::sleep_for(kWaitSlice);
        streamer.update(position);
    }

    CellId cell = game.heightfield.cellAt(position.x, position.z);
    bool crossing = false;
    Clock::time_point last = Clock::now();
    const auto tick = [&](const glm::dvec3& where) {
        const CellId now = game.heightfield.cellAt(where.x, where.z);
        if (now != cell) {
            walk.crossings.push_back({.tick = walk.ticksMs.size(), .end = walk.ticksMs.size() + 1});
            crossing = true;
            cell = now;
        }
        streamer.update(where);
        const Clock::time_point end = Clock::now();
        walk.ticksMs.push_back(std::chrono::duration<double, std::milli>(end - last).count());
        last = end;
    };

    WalkBotParams bot;
    bot.arriveMetres = kArriveMetres;
    bot.onStep = [&](const glm::dvec3& where, double) {
        tick(where);
        // The simulation is held until the required ring is resident; the frames spent waiting are ticks too.
        while (!streamer.ready(where)) {
            std::this_thread::sleep_for(kWaitSlice);
            last = Clock::now();
            tick(where);
        }
        if (crossing) {
            walk.crossings.back().end = walk.ticksMs.size();
            crossing = false;
        }
    };
    for (const Leg& leg : legs) {
        const WalkResult result = walkTo(walking, position, leg.goal, leg.waypoints, bot);
        position = result.end;
        if (result.outcome != WalkOutcome::Arrived) {
            walk.stopped = std::format("leg {} ended {:.0f} m short: {}", walk.legsArrived + 1, result.metresLeft,
                result.outcome == WalkOutcome::Blocked ? result.report : std::string("timed out"));
            break;
        }
        ++walk.legsArrived;
    }
    return walk;
}

std::string describe(const char* name, const Walk& walk)
{
    std::string text = std::format("{}: {} ticks, {} crossings, {} cell loads, {} legs arrived, median {:.4f} ms, worst crossing tick {:.3f} ms, budget {:.3f} ms, worst tick anywhere {:.3f} ms",
        name, walk.ticksMs.size(), walk.crossings.size(), walk.loads, walk.legsArrived, median(walk.ticksMs), worstCrossing(walk), budgetMs(median(walk.ticksMs)), worst(walk.ticksMs));
    if (!walk.stopped.empty()) {
        text += "; " + walk.stopped;
    }
    for (std::size_t i = 0; i < walk.crossings.size(); ++i) {
        const Crossing& crossing = walk.crossings[i];
        const std::size_t next = i + 1 < walk.crossings.size() ? walk.crossings[i + 1].tick : walk.ticksMs.size();
        text += std::format("\n  crossing {} at tick {}: {} held frames, worst {:.3f} ms; then {} ticks, worst {:.3f} ms", i + 1, crossing.tick, crossing.end - crossing.tick - 1,
            worst(walk.ticksMs, crossing.tick, crossing.end), next - crossing.end, next > crossing.end ? worst(walk.ticksMs, crossing.end, next) : 0.0);
    }
    return text;
}

}

TEST_CASE("walking across streaming cell boundaries never stalls a tick, and a streamer that builds on the tick thread does", "[forged_stream_hitch]")
{
    const std::unique_ptr<ForgedGame> game = forgeGame();
    const ForgedWalking walking(game->heightfield, game->waterField.get(), CapabilitySet{});
    const std::vector<Leg> legs = planLegs(*game, walking);
    REQUIRE_FALSE(legs.empty());

    const Walk streamed = walkLegs(*game, walking, legs, false);
    INFO(describe("asynchronous", streamed));
    REQUIRE(streamed.crossings.size() >= kMinCrossings);
    CHECK(streamed.loads > 0);
    CHECK(worstCrossing(streamed) <= budgetMs(median(streamed.ticksMs)));

    // The seam: the same walk with every cell built on the tick thread is what a blocking streamer looks like.
    const Walk blocking = walkLegs(*game, walking, legs, true);
    INFO(describe("synchronous", blocking));
    REQUIRE(blocking.crossings.size() >= kMinCrossings);
    CHECK(worstCrossing(blocking) > budgetMs(median(blocking.ticksMs)));
}
