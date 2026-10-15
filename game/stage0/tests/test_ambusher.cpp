#include "ichor/stage0/ambusher.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>
#include <cstring>
#include <limits>

using namespace ichor;
using namespace ichor::stage0;

namespace {

const AmbusherTuning kTuning{};
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

struct World {
    AmbusherBrain brain;
    AmbusherSenses senses;
    glm::vec2 self{0.0f};
    int strikes = 0;
    int ambushStrikes = 0;

    World()
    {
        senses.cover = {0.0f, 0.0f};
        senses.target = {10.0f, 0.0f};
        senses.targetFacing = {0.0f, 1.0f};
    }

    AmbusherIntent tick()
    {
        senses.self = self;
        const AmbusherIntent intent = ambusherStep(brain, senses, kTuning);
        self += intent.move * kTuning.tickSeconds;
        if (intent.strike) {
            ++strikes;
            if (intent.ambush) ++ambushStrikes;
        }
        return intent;
    }

    bool tickUntil(AmbusherState state, int limit)
    {
        for (int i = 0; i < limit; ++i) {
            tick();
            if (brain.ambusherState() == state) return true;
        }
        return false;
    }

    int ticksToStrike(int limit)
    {
        int ticks = 0;
        while (strikes == 0 && ticks < limit) {
            tick();
            ++ticks;
        }
        return ticks;
    }
};

std::uint64_t hashBrain(const AmbusherBrain& b, glm::vec2 pos)
{
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    std::uint32_t bx = 0;
    std::uint32_t bz = 0;
    std::memcpy(&bx, &pos.x, 4);
    std::memcpy(&bz, &pos.y, 4);
    mix(bx);
    mix(bz);
    mix(b.state);
    mix(static_cast<std::uint64_t>(b.stateTicks));
    mix(static_cast<std::uint64_t>(b.cooldownTicks));
    mix(static_cast<std::uint64_t>(b.spotTicks));
    mix(b.concealed);
    mix(b.ambush);
    mix(static_cast<std::uint64_t>(b.strikes));
    return h;
}

}

TEST_CASE("a far target leaves the ambusher hidden and still")
{
    World w;
    for (int i = 0; i < 600; ++i) {
        const AmbusherIntent intent = w.tick();
        REQUIRE(intent.move == glm::vec2(0.0f));
        REQUIRE_FALSE(intent.strike);
    }
    REQUIRE(w.brain.ambusherState() == AmbusherState::Hidden);
    REQUIRE(w.brain.concealed);
}

TEST_CASE("a target inside trigger range springs the ambush")
{
    World w;
    w.senses.target = {kTuning.triggerRange - 0.5f, 0.0f};
    w.tick();
    REQUIRE(w.brain.ambusherState() == AmbusherState::Rush);
    REQUIRE(w.brain.ambush);
}

TEST_CASE("the rush closes in and strikes after the short ambush windup")
{
    World w;
    w.senses.target = {kTuning.triggerRange - 0.5f, 0.0f};
    REQUIRE(w.tickUntil(AmbusherState::Strike, 200));
    REQUIRE(w.ticksToStrike(200) == kTuning.ambushWindupTicks);
    REQUIRE(w.strikes == 1);
    REQUIRE(w.ambushStrikes == 1);
    REQUIRE(w.brain.ambusherState() == AmbusherState::Recover);
}

TEST_CASE("a spotted ambusher keeps its place but loses the fast windup")
{
    World w;
    w.senses.targetFacing = {-1.0f, 0.0f};
    w.senses.target = {kTuning.spotRange - 1.0f, 0.0f};
    for (int i = 0; i < kTuning.spotTicksNeeded + 2; ++i) {
        w.tick();
    }
    REQUIRE_FALSE(w.brain.concealed);
    REQUIRE(w.brain.ambusherState() == AmbusherState::Hidden);

    w.senses.target = {kTuning.triggerRange - 0.5f, 0.0f};
    w.tick();
    REQUIRE_FALSE(w.brain.ambush);
    REQUIRE(w.tickUntil(AmbusherState::Strike, 200));
    REQUIRE(w.ticksToStrike(200) == kTuning.windupTicks);
    REQUIRE(w.strikes == 1);
    REQUIRE(w.ambushStrikes == 0);
}

TEST_CASE("looking at the cover from outside spot range does not blow it")
{
    World w;
    w.senses.targetFacing = {-1.0f, 0.0f};
    w.senses.target = {kTuning.spotRange + 1.0f, 0.0f};
    for (int i = 0; i < 600; ++i) {
        w.tick();
    }
    REQUIRE(w.brain.concealed);
    REQUIRE(w.brain.spotTicks == 0);
}

TEST_CASE("looking away decays the spotting progress")
{
    World w;
    w.senses.target = {kTuning.spotRange - 1.0f, 0.0f};
    w.senses.targetFacing = {-1.0f, 0.0f};
    for (int i = 0; i < 20; ++i) {
        w.tick();
    }
    const int built = w.brain.spotTicks;
    REQUIRE(built > 0);
    w.senses.targetFacing = {1.0f, 0.0f};
    for (int i = 0; i < 5; ++i) {
        w.tick();
    }
    REQUIRE(w.brain.spotTicks == built - 5 * kTuning.spotDecayTicks);
}

TEST_CASE("the cone edge decides: just inside spots, just outside does not")
{
    const float edge = std::acos(kTuning.spotCosHalfCone);
    for (const float offset : {edge - 0.05f, edge + 0.05f}) {
        World w;
        w.senses.target = {kTuning.spotRange - 1.0f, 0.0f};
        w.senses.targetFacing = {-std::cos(offset), std::sin(offset)};
        for (int i = 0; i < kTuning.spotTicksNeeded + 2; ++i) {
            w.tick();
        }
        REQUIRE(w.brain.concealed == (offset > edge));
    }
}

TEST_CASE("after the strike it withdraws to cover, hides, and ignores triggers while rearming")
{
    World w;
    w.senses.target = {kTuning.triggerRange - 0.5f, 0.0f};
    REQUIRE(w.tickUntil(AmbusherState::Withdraw, 400));
    REQUIRE(w.tickUntil(AmbusherState::Hidden, 400));
    REQUIRE(glm::length(w.self - w.senses.cover) <= kTuning.arriveRadius + 1e-3f);
    REQUIRE(w.brain.concealed);
    REQUIRE(w.brain.cooldownTicks == kTuning.rearmTicks);

    const int before = w.strikes;
    for (int i = 0; i < kTuning.rearmTicks - 2; ++i) {
        w.tick();
        REQUIRE(w.brain.ambusherState() == AmbusherState::Hidden);
    }
    for (int i = 0; i < 4; ++i) {
        w.tick();
    }
    REQUIRE(w.brain.ambusherState() != AmbusherState::Hidden);
    REQUIRE(w.strikes == before);
}

TEST_CASE("a target that outruns the rush makes it give up and withdraw")
{
    World w;
    w.senses.target = {kTuning.triggerRange - 0.5f, 0.0f};
    w.tick();
    REQUIRE(w.brain.ambusherState() == AmbusherState::Rush);
    w.senses.target = {500.0f, 0.0f};
    REQUIRE(w.tickUntil(AmbusherState::Withdraw, kTuning.rushMaxTicks + 5));
    REQUIRE(w.strikes == 0);
}

TEST_CASE("a dead target ends the wind-up and sends it home")
{
    World w;
    w.senses.target = {kTuning.triggerRange - 0.5f, 0.0f};
    REQUIRE(w.tickUntil(AmbusherState::Strike, 200));
    w.senses.targetAlive = false;
    for (int i = 0; i < 400; ++i) {
        w.tick();
    }
    REQUIRE(w.strikes == 0);
    REQUIRE(w.brain.ambusherState() == AmbusherState::Hidden);
}

TEST_CASE("a target standing on the cover point yields no NaN")
{
    World w;
    w.senses.target = w.senses.cover;
    w.self = w.senses.cover;
    for (int i = 0; i < 300; ++i) {
        const AmbusherIntent intent = w.tick();
        REQUIRE(std::isfinite(intent.move.x));
        REQUIRE(std::isfinite(intent.move.y));
    }
    REQUIRE(std::isfinite(w.self.x));
    REQUIRE(w.strikes >= 1);
}

TEST_CASE("non-finite positions are rejected before any state changes")
{
    for (int field = 0; field < 3; ++field) {
        for (const float bad : {kNaN, kInf, -kInf}) {
            World w;
            w.senses.target = {kTuning.triggerRange - 0.5f, 0.0f};
            AmbusherSenses s = w.senses;
            glm::vec2* slots[] = {&s.self, &s.cover, &s.target};
            slots[field]->x = bad;
            const AmbusherBrain before = w.brain;
            const AmbusherIntent intent = ambusherStep(w.brain, s, kTuning);
            REQUIRE(intent.move == glm::vec2(0.0f));
            REQUIRE_FALSE(intent.strike);
            REQUIRE(hashBrain(w.brain, {}) == hashBrain(before, {}));
        }
    }
}

TEST_CASE("a zero or non-finite facing never spots")
{
    for (const glm::vec2 facing : {glm::vec2(0.0f), glm::vec2(kNaN, 1.0f), glm::vec2(kInf, 0.0f)}) {
        World w;
        w.senses.target = {kTuning.spotRange - 1.0f, 0.0f};
        w.senses.targetFacing = facing;
        for (int i = 0; i < kTuning.spotTicksNeeded * 3; ++i) {
            w.tick();
        }
        REQUIRE(w.brain.concealed);
        REQUIRE(w.brain.spotTicks == 0);
    }
}

TEST_CASE("a loaded brain with an unknown state or wild counters is pulled back in")
{
    World w;
    w.brain.state = 200;
    w.brain.stateTicks = -5;
    w.brain.cooldownTicks = -9;
    w.brain.spotTicks = std::numeric_limits<std::int32_t>::max();
    w.tick();
    REQUIRE(w.brain.ambusherState() == AmbusherState::Hidden);
    REQUIRE(w.brain.stateTicks >= 0);
    REQUIRE(w.brain.cooldownTicks >= 0);
    REQUIRE(w.brain.spotTicks <= kTuning.spotTicksNeeded);
}

TEST_CASE("degenerate tuning cannot divide by zero or stall")
{
    AmbusherTuning t;
    t.tickSeconds = 0.0f;
    t.rushSpeed = -3.0f;
    t.windupTicks = -4;
    t.ambushWindupTicks = -4;
    AmbusherBrain brain;
    AmbusherSenses s;
    s.target = {1.0f, 0.0f};
    for (int i = 0; i < 100; ++i) {
        const AmbusherIntent intent = ambusherStep(brain, s, t);
        REQUIRE(std::isfinite(intent.move.x));
        REQUIRE(std::isfinite(intent.move.y));
    }
}

TEST_CASE("the same inputs step to the same bits every run")
{
    const auto run = [] {
        World w;
        for (int i = 0; i < 1500; ++i) {
            const float phase = static_cast<float>(i % 400) / 400.0f;
            w.senses.target = {12.0f - 12.0f * phase, 0.0f};
            w.senses.targetFacing = {i % 200 < 100 ? -1.0f : 1.0f, 0.1f};
            w.tick();
        }
        return hashBrain(w.brain, w.self);
    };
    REQUIRE(run() == run());
    REQUIRE(run() != 0);
}
