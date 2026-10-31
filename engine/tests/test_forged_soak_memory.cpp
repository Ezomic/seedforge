#include "forged_world_fixture.h"
#include "resident_memory.h"

#include "ichor/sim/scene.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walking.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ichor;

// SEED-711: a long headless session over the forged world, with the resident memory sampled along the way. The walk bot
// (forged_walk_bot.h) walks a serpentine over every cell of the 4 km world, round and round, at the player's pace; the
// streamer follows it with the app's full cell payloads (terrain, trees, water, the placed objects) and the cells' collision
// heights go in and out of a physics world as in the app, so every cell crossing loads and frees what a real session does.
// The verdict is on the shape of the curve, not its height: once the ring is warm the resident set must stop growing, so the
// mean of the last quarter of the samples stays within a small margin of the mean of the third quarter. A leak that keeps a
// cell's payload per crossing grows the whole run and fails this (the hidden probe below proves it).
//
// Nightly only: it needs minutes of wall time, so it is skipped unless ICHOR_SOAK_MEMORY_SECONDS gives the budget. Run it
// locally with `bin/build ichor_tests_world`, then `ICHOR_SOAK_MEMORY_SECONDS=120 engine/build/windows-ci/tests/ichor_tests_world "soak: *"`.

namespace {

constexpr int kGrid = 129;
constexpr std::uint64_t kSeed = 1;
constexpr double kSampleSeconds = 1.0;      // wall time between two resident-memory samples
constexpr double kLegSeconds = 4.0;         // simulated seconds the bot walks before the streamer sees where it got to
constexpr double kGrowthFraction = 0.05;    // the last quarter's mean may sit this far above the third quarter's...
constexpr double kGrowthFloorBytes = 16.0 * 1024.0 * 1024.0; // ...or this many bytes above it, whichever is more: quarter means of a 90 MB process differ by up to 6 MB from allocator noise alone
constexpr int kMinCrossings = 40;           // fewer and the session did not stream enough to say anything

double budgetFromEnv()
{
    const char* text = std::getenv("ICHOR_SOAK_MEMORY_SECONDS");
    return text != nullptr ? std::atof(text) : 0.0;
}

void say(const std::string& line)
{
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

double mean(const std::vector<double>& values, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        sum += values[i];
    }
    return to > from ? sum / static_cast<double>(to - from) : 0.0;
}

// Least-squares slope of the samples in [from, to) against their wall time, in megabytes a minute.
double slope(const std::vector<double>& at, const std::vector<double>& megabytes, std::size_t from, std::size_t to)
{
    if (to - from < 2) {
        return 0.0;
    }
    const double t0 = mean(at, from, to);
    const double m0 = mean(megabytes, from, to);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        num += (at[i] - t0) * (megabytes[i] - m0);
        den += (at[i] - t0) * (at[i] - t0);
    }
    return den > 0.0 ? num / den * 60.0 : 0.0;
}

struct SoakCurve {
    std::vector<double> at;        // wall seconds of each sample
    std::vector<double> megabytes; // resident set at each sample
    double quarterMean[4] = {0.0, 0.0, 0.0, 0.0};
    double firstHalfSlope = 0.0;  // MB a minute
    double secondHalfSlope = 0.0;
    double allowedBytes = 0.0;    // how far above the third quarter the last quarter may sit
    bool flat = false;            // the verdict
    int crossings = 0;            // cell boundaries the bot crossed
    int teleports = 0;            // legs it could not walk and was moved past
    std::uint64_t loads = 0;
    std::uint64_t unloads = 0;
    std::size_t resident = 0;
    double simSeconds = 0.0;
    double metres = 0.0;
};

std::string describe(const SoakCurve& c)
{
    return std::format("SOAK memory: {} samples over {:.0f} s wall, quarter means {:.1f} {:.1f} {:.1f} {:.1f} MB, slope first half {:+.2f} second half {:+.2f} MB/min, "
                       "last quarter is {:+.1f} MB from the third (allowed {:.1f}), {} crossings, {} teleports, {} loads, {} unloads, {} resident, {:.0f} s sim, {:.0f} m",
        c.at.size(), c.at.empty() ? 0.0 : c.at.back(), c.quarterMean[0], c.quarterMean[1], c.quarterMean[2], c.quarterMean[3], c.firstHalfSlope, c.secondHalfSlope,
        c.quarterMean[3] - c.quarterMean[2], c.allowedBytes / (1024.0 * 1024.0), c.crossings, c.teleports, c.loads, c.unloads, c.resident, c.simSeconds, c.metres);
}

void judge(SoakCurve& c)
{
    const std::size_t n = c.at.size();
    for (std::size_t q = 0; q < 4; ++q) {
        c.quarterMean[q] = mean(c.megabytes, n * q / 4, n * (q + 1) / 4);
    }
    c.firstHalfSlope = slope(c.at, c.megabytes, 0, n / 2);
    c.secondHalfSlope = slope(c.at, c.megabytes, n / 2, n);
    c.allowedBytes = std::max(kGrowthFloorBytes, c.quarterMean[2] * 1024.0 * 1024.0 * kGrowthFraction);
    c.flat = n >= 8 && (c.quarterMean[3] - c.quarterMean[2]) * 1024.0 * 1024.0 <= c.allowedBytes;
}

// The cell centres in a serpentine, so a lap crosses every interior boundary in both axes.
std::vector<glm::dvec3> serpentine(const ForgedGame& game)
{
    const Heightfield& field = game.heightfield;
    const double cell = game.desc.cellMetres;
    const int side = static_cast<int>(game.desc.cellsPerSide());
    std::vector<glm::dvec3> points;
    for (int z = 0; z < side; ++z) {
        for (int i = 0; i < side; ++i) {
            const int x = z % 2 == 0 ? i : side - 1 - i;
            const double px = x * cell + cell * 0.5;
            const double pz = z * cell + cell * 0.5;
            points.push_back({px, field.heightAt(px, pz), pz});
        }
    }
    return points;
}

// `leak` keeps every unloaded cell's payload alive, the way a resident map that is never pruned would: the test seam the
// hidden probe uses to show the verdict goes red.
SoakCurve soak(double budgetSeconds, bool leak)
{
    SoakCurve curve;
    WorldDesc desc;
    desc.seed = kSeed;
    const test::ForgedFixture fixture = test::forgedFixture(desc, kGrid);
    REQUIRE(fixture.ok());
    const std::unique_ptr<ForgedGame> game = buildForgedGame(fixture.result, desc);
    REQUIRE(game->ok());
    REQUIRE(game->waterField != nullptr);

    StreamerConfig config;
    config.synchronous = true;
    config.vegetation = true;
    config.water = true;
    config.rivers = game->water.rivers;
    config.hydrology = game->water.hydrology;
    config.biomes = game->biomes;
    config.scenery = forgedSceneryGuard(*game);
    config.pois = game->pois;
    config.buildings = game->buildings;
    config.streets = game->streets;
    config.loot = game->loot;
    config.cast = game->cast;

    Scene scene(kSeed);
    std::map<CellId, CellPayload> resident;
    std::vector<CellPayload> leaked;
    WorldStreamer streamer(game->heightfield, config);
    streamer.onLoaded([&](const CellPayload& payload) {
        const glm::dvec3 origin{game->heightfield.cellOriginX(payload.cell.id), 0.0, game->heightfield.cellOriginZ(payload.cell.id)};
        scene.physics().addHeightField(StableId::of("cell", cellKey(payload.cell.id)), payload.cell.heights.data(), payload.cell.samplesPerSide, origin,
            static_cast<float>(game->desc.metresPerSample));
        resident[payload.cell.id] = payload;
    });
    streamer.onUnloaded([&](CellId id) {
        scene.physics().remove(StableId::of("cell", cellKey(id)));
        if (const auto found = resident.find(id); found != resident.end()) {
            if (leak) {
                leaked.push_back(std::move(found->second));
            }
            resident.erase(found);
        }
    });

    const ForgedWalking walking(game->heightfield, game->waterField.get(), CapabilitySet{});
    WalkBotParams params;
    params.maxSeconds = kLegSeconds;
    const std::vector<glm::dvec3> route = serpentine(*game);
    REQUIRE(route.size() >= 4);
    glm::dvec3 position{game->spawn.x, game->spawn.groundY, game->spawn.z};
    std::size_t next = 0;
    CellId lastCell = game->heightfield.cellAt(position.x, position.z);
    streamer.update(position);

    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    double nextSample = 0.0;
    while (true) {
        const double now = elapsed();
        if (now >= nextSample) {
            const std::uint64_t bytes = test::residentBytes();
            REQUIRE(bytes > 0);
            curve.at.push_back(now);
            curve.megabytes.push_back(static_cast<double>(bytes) / (1024.0 * 1024.0));
            nextSample += kSampleSeconds;
        }
        if (now >= budgetSeconds) {
            break;
        }
        const glm::dvec3& goal = route[next];
        const WalkResult leg = walkTo(walking, position, goal, {}, params);
        curve.simSeconds += leg.seconds;
        curve.metres += leg.metresWalked;
        position = leg.end;
        if (leg.outcome == WalkOutcome::Arrived) {
            next = (next + 1) % route.size();
        } else if (leg.outcome == WalkOutcome::Blocked) {
            // Honest blockers are the walk tests' business (test_forged_walk_nightly.cpp); here the session goes on past them.
            position = goal;
            next = (next + 1) % route.size();
            ++curve.teleports;
        }
        position.y = game->heightfield.heightAt(position.x, position.z);
        const CellId cell = game->heightfield.cellAt(position.x, position.z);
        if (cell != lastCell) {
            ++curve.crossings;
            lastCell = cell;
        }
        streamer.update(position);
    }
    curve.loads = streamer.loads();
    curve.unloads = streamer.unloads();
    curve.resident = streamer.resident().size();
    judge(curve);
    say(describe(curve));
    for (std::size_t i = 0; i < curve.at.size(); ++i) {
        say(std::format("SOAK sample {:>5.0f} s {:>8.1f} MB", curve.at[i], curve.megabytes[i]));
    }
    return curve;
}

}

TEST_CASE("soak: a long headless session over the forged world stops growing once the streaming ring is warm", "[soak][nightly]")
{
    const double budget = budgetFromEnv();
    if (budget <= 0.0) {
        SKIP("set ICHOR_SOAK_MEMORY_SECONDS (for example 300) to run the memory soak");
    }
    const SoakCurve curve = soak(budget, false);
    CHECK(curve.crossings >= kMinCrossings);
    CHECK(curve.loads == curve.unloads + curve.resident);
    CHECK(curve.flat);
}

// Hidden: the same session with a resident map that is never pruned must fail the verdict, so the metric is known to go red.
TEST_CASE("soak: a session that keeps every unloaded cell fails the memory verdict", "[.soak_memory_leak_probe]")
{
    const double budget = budgetFromEnv();
    if (budget <= 0.0) {
        SKIP("set ICHOR_SOAK_MEMORY_SECONDS (for example 120) to run the leak probe");
    }
    const SoakCurve curve = soak(budget, true);
    CHECK(curve.crossings >= kMinCrossings);
    CHECK_FALSE(curve.flat);
}
