#include "ichor/world/forge_progress.h"
#include "ichor/world/phase_pipeline.h"
#include "ichor/world/validation.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;

namespace {

std::uint64_t digestOf(const std::vector<int>& v)
{
    std::uint64_t h = 1469598103934665603ull;
    for (int x : v) {
        h = (h ^ static_cast<std::uint64_t>(x)) * 1099511628211ull;
    }
    return h;
}

PhasePipeline three()
{
    return PhasePipeline({
        makePhase<std::vector<int>>("a", [](const PhaseView&) { return std::vector<int>{1}; }, digestOf),
        makePhase<std::vector<int>>("b", [](const PhaseView& e) { return e.get<std::vector<int>>("a"); }, digestOf),
        makePhase<std::vector<int>>("c", [](const PhaseView& e) { return e.get<std::vector<int>>("b"); }, digestOf),
    });
}

ForgeRequest smallRequest(std::uint64_t seed)
{
    ForgeRequest request;
    request.desc.seed = seed;
    request.desc.sizeMetres = 1024;
    request.desc.metresPerSample = 16;
    request.desc.cellMetres = 256;
    request.desc.waterHeight = 20.0f;
    request.carveRivers = true;
    request.rivers.riverThreshold = 40;
    return request;
}

}

TEST_CASE("an observer hears each phase start and finish, in order", "[forge_progress]")
{
    std::vector<PhaseProgress> events;
    three().run([&](const PhaseProgress& p) { events.push_back(p); });
    REQUIRE(events.size() == 6);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(events[2 * i].index == i);
        CHECK_FALSE(events[2 * i].finished);
        CHECK(events[2 * i + 1].index == i);
        CHECK(events[2 * i + 1].finished);
        CHECK(events[2 * i].count == 3);
    }
    CHECK(events[0].phase == "a");
    CHECK(events[5].phase == "c");
}

TEST_CASE("runFrom reports only the phases it runs", "[forge_progress]")
{
    const PhasePipeline pipeline = three();
    const PhaseRun first = pipeline.run();
    std::vector<PhaseProgress> events;
    pipeline.runFrom(first, "b", [&](const PhaseProgress& p) { events.push_back(p); });
    REQUIRE(events.size() == 4);
    CHECK(events.front().phase == "b");
    CHECK(events.front().count == 3);
}

TEST_CASE("watching a forge run does not change the forged world", "[forge_progress]")
{
    for (const std::uint64_t seed : {1ull, 5ull, 99ull}) {
        const PhaseRun plain = forgePipeline(smallRequest(seed)).run();
        ForgeProgress progress;
        const PhaseRun watched = forgePipeline(smallRequest(seed)).run(progress.observer());
        CHECK(watched.chainDigest() == plain.chainDigest());
        CHECK_FALSE(firstDivergence(plain, watched));
        CHECK(progress.snapshot().done);
        CHECK(progress.snapshot().total == plain.size());
    }
}

TEST_CASE("an observer that throws ends the run and reaches the caller", "[forge_progress]")
{
    CHECK_THROWS_AS(three().run([](const PhaseProgress&) { throw std::runtime_error("stop"); }), std::runtime_error);
}

TEST_CASE("progress starts empty and reads zero", "[forge_progress]")
{
    ForgeProgress progress;
    const ForgeProgressSnapshot s = progress.snapshot();
    CHECK_FALSE(s.started);
    CHECK_FALSE(s.done);
    CHECK(s.fraction() == 0.0f);
}

TEST_CASE("progress advances by finished phases and reset clears it", "[forge_progress]")
{
    ForgeProgress progress;
    std::vector<float> seen;
    const PhaseObserver inner = progress.observer();
    three().run([&](const PhaseProgress& p) {
        inner(p);
        seen.push_back(progress.snapshot().fraction());
    });
    REQUIRE(seen.size() == 6);
    CHECK(seen[0] == 0.0f);
    CHECK(seen[1] > seen[0]);
    CHECK(seen[2] == seen[1]); // starting b does not move the bar
    CHECK(seen[3] > seen[2]);
    CHECK(seen[5] == 1.0f);
    CHECK(progress.snapshot().done);
    CHECK(progress.snapshot().phase == "c");

    progress.reset();
    CHECK_FALSE(progress.snapshot().started);
    CHECK(progress.snapshot().fraction() == 0.0f);
}

TEST_CASE("another thread can read progress while the forge runs on a worker", "[forge_progress]")
{
    ForgeProgress progress;
    std::atomic<bool> finished{false};
    std::uint64_t digest = 0;
    std::thread worker([&] {
        digest = forgePipeline(smallRequest(5)).run(progress.observer()).chainDigest();
        finished = true;
    });
    float last = 0.0f;
    while (!finished) {
        const float f = progress.snapshot().fraction();
        CHECK(f >= last);
        CHECK(f <= 1.0f);
        last = f;
    }
    worker.join();
    CHECK(progress.snapshot().fraction() == 1.0f);
    CHECK(digest == forgePipeline(smallRequest(5)).run().chainDigest());
}

TEST_CASE("a second run, or a run from a later phase, does not show the last run's progress", "[forge_progress]")
{
    const PhasePipeline pipeline = three();
    ForgeProgress progress;
    const PhaseRun first = pipeline.run(progress.observer());
    REQUIRE(progress.snapshot().done);

    const PhaseObserver inner = progress.observer();
    std::vector<ForgeProgressSnapshot> seen;
    pipeline.runFrom(first, 1, [&](const PhaseProgress& p) {
        inner(p);
        seen.push_back(progress.snapshot());
    });
    REQUIRE(seen.size() == 4);
    CHECK_FALSE(seen[0].done);
    CHECK(seen[0].completed == 1); // phase a was reused, so it counts
    CHECK(seen[3].done);
}
