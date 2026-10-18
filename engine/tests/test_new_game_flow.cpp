#include "ichor/ui/new_game_flow.h"
#include "ichor/world/forge_job.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace ichor;

namespace {

constexpr int kGrid = 32;

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

template <class Pred>
bool waitFor(Pred pred)
{
    for (int i = 0; i < 6000; ++i) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

}

TEST_CASE("a forge job reports every phase in order, each starting before it finishes", "[forge_job]")
{
    std::mutex m;
    std::vector<PhaseProgress> events;
    ForgeJob job(descFor(7), kGrid, [&](const PhaseProgress& p) {
        const std::lock_guard lock(m);
        events.push_back(p);
    });
    REQUIRE(job.start());
    REQUIRE_FALSE(job.start());
    REQUIRE(waitFor([&] { return job.state() != ForgeJobState::Running; }));
    REQUIRE(job.state() == ForgeJobState::Done);
    REQUIRE(events.size() == 22);
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].index == i / 2);
        CHECK(events[i].finished == (i % 2 == 1));
        CHECK(events[i].count == 11);
    }
    CHECK(events.front().phase == "tectonics");
    CHECK(events.back().phase == "content");
    CHECK(job.progress().done);
    CHECK(job.progress().fraction() == 1.0f);
}

TEST_CASE("a seed forges the same world and terrain however the run is watched", "[forge_job]")
{
    ForgeJob a(descFor(11), kGrid);
    ForgeJob b(descFor(11), kGrid, [](const PhaseProgress&) {});
    ForgeJob c(descFor(12), kGrid);
    a.start();
    b.start();
    c.start();
    REQUIRE(waitFor([&] { return a.result() && b.result() && c.result(); }));
    CHECK(a.result()->world->worldHash() == b.result()->world->worldHash());
    CHECK(a.result()->heightfield.samples() == b.result()->heightfield.samples());
    CHECK_FALSE(a.result()->heightfield.empty());
    CHECK(a.result()->world->worldHash() != c.result()->world->worldHash());
    CHECK(a.result()->heightfield.samples() != c.result()->heightfield.samples());
}

TEST_CASE("cancelling a forge job stops it with no result and no thread left", "[forge_job]")
{
    std::atomic<bool> seenFirst{false};
    ForgeJob job(descFor(3), 64, [&](const PhaseProgress&) { seenFirst = true; });
    job.start();
    REQUIRE(waitFor([&] { return seenFirst.load(); }));
    job.cancel();
    CHECK((job.state() == ForgeJobState::Cancelled || job.state() == ForgeJobState::Done));
    if (job.state() == ForgeJobState::Cancelled) {
        CHECK(job.result() == nullptr);
        CHECK_FALSE(job.progress().done);
    }
}

TEST_CASE("cancelling before the start and destroying a running job are safe", "[forge_job]")
{
    ForgeJob idle(descFor(1), kGrid);
    idle.cancel();
    CHECK(idle.state() == ForgeJobState::Cancelled);
    CHECK_FALSE(idle.start());
    {
        ForgeJob running(descFor(2), 64);
        running.start();
    }
    SUCCEED();
}

TEST_CASE("a world the forge refuses fails the job with a reason", "[forge_job]")
{
    ForgeJob job(descFor(5), 4);
    job.start();
    REQUIRE(waitFor([&] { return job.state() != ForgeJobState::Running; }));
    CHECK(job.state() == ForgeJobState::Failed);
    CHECK_FALSE(job.error().empty());
    CHECK(job.result() == nullptr);
    CHECK_FALSE(job.progress().done);
    CHECK(job.progress().fraction() < 1.0f);
}

TEST_CASE("the flow with the forged flag off goes straight to ready with no world", "[new_game_flow]")
{
    NewGameFlow flow(false);
    flow.begin(descFor(9));
    CHECK(flow.stage() == NewGameStage::Ready);
    CHECK(flow.takeJob() == nullptr);

    NewGameFlow strict(false);
    WorldDesc bad = descFor(9);
    bad.sizeMetres = 0;
    strict.begin(bad);
    CHECK(strict.stage() == NewGameStage::Failed);
}

TEST_CASE("the flow forges the chosen seed and hands over the job", "[new_game_flow]")
{
    NewGameFlow flow(true, kGrid);
    CHECK(flow.stage() == NewGameStage::Choosing);
    CHECK(flow.takeJob() == nullptr);
    flow.begin(descFor(21));
    CHECK(flow.stage() == NewGameStage::Forging);
    REQUIRE(waitFor([&] {
        flow.update();
        return flow.stage() != NewGameStage::Forging;
    }));
    REQUIRE(flow.stage() == NewGameStage::Ready);
    CHECK(flow.loadingView().percent == 100);
    const std::unique_ptr<ForgeJob> job = flow.takeJob();
    REQUIRE(job);
    REQUIRE(job->result());
    CHECK(job->result()->heightfield.desc().seed == 21);
}

TEST_CASE("cancel in the flow returns to choosing and a second begin works", "[new_game_flow]")
{
    NewGameFlow flow(true, 64);
    flow.begin(descFor(4));
    flow.cancel();
    CHECK(flow.stage() == NewGameStage::Choosing);
    CHECK(flow.result() == nullptr);
    flow.begin(descFor(4));
    CHECK(flow.stage() == NewGameStage::Forging);
    flow.cancel();
    CHECK(flow.stage() == NewGameStage::Choosing);
}

TEST_CASE("a refused world or an invalid desc fails the flow and begin can retry", "[new_game_flow]")
{
    NewGameFlow flow(true, 4);
    flow.begin(descFor(1));
    REQUIRE(waitFor([&] {
        flow.update();
        return flow.stage() != NewGameStage::Forging;
    }));
    CHECK(flow.stage() == NewGameStage::Failed);
    CHECK_FALSE(flow.error().empty());
    CHECK(flow.takeJob() == nullptr);
    WorldDesc bad = descFor(1);
    bad.sizeMetres = 0;
    flow.begin(bad);
    CHECK(flow.stage() == NewGameStage::Failed);
}
