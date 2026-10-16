#include "ichor/traversal/lost_state.h"
#include "ichor/traversal/lost_visibility.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

int rangeAt(LostWeather kind, float intensity, float hour)
{
    return visibilityRangeMeters({kind, intensity}, hour);
}

LostState walk(LostState s, int seconds, int visibility, const LostSenses& senses)
{
    for (int i = 0; i < seconds * 10; ++i) {
        s = stepLostState(s, visibility, senses, 100);
    }
    return s;
}

}

TEST_CASE("clear noon gives the full range", "[lost]")
{
    CHECK(rangeAt(LostWeather::Clear, 0.0f, 12.0f) == kMaxVisibilityMeters);
    CHECK(rangeAt(LostWeather::Clear, 1.0f, 12.0f) == kMaxVisibilityMeters);
}

TEST_CASE("each weather cuts visibility more as it intensifies, compared with clear", "[lost]")
{
    const int clear = rangeAt(LostWeather::Clear, 0.0f, 12.0f);
    for (LostWeather w : {LostWeather::Rain, LostWeather::Fog, LostWeather::Storm}) {
        const int light = rangeAt(w, 0.25f, 12.0f);
        const int heavy = rangeAt(w, 1.0f, 12.0f);
        CHECK(light < clear);
        CHECK(heavy < light);
        CHECK(heavy >= kMinVisibilityMeters);
    }
    CHECK(rangeAt(LostWeather::Fog, 1.0f, 12.0f) < kLostVisibilityMeters);
    CHECK(rangeAt(LostWeather::Fog, 1.0f, 12.0f) < rangeAt(LostWeather::Rain, 1.0f, 12.0f));
}

TEST_CASE("night cuts visibility and dusk lies between", "[lost]")
{
    const int noon = rangeAt(LostWeather::Clear, 0.0f, 12.0f);
    const int dusk = rangeAt(LostWeather::Clear, 0.0f, 18.0f);
    const int night = rangeAt(LostWeather::Clear, 0.0f, 0.0f);
    CHECK(night < dusk);
    CHECK(dusk <= noon);
    CHECK(night < noon / 4);
    CHECK(night >= kMinVisibilityMeters);
}

TEST_CASE("hour wraps around the day", "[lost]")
{
    CHECK(rangeAt(LostWeather::Clear, 0.0f, 3.0f) == rangeAt(LostWeather::Clear, 0.0f, 27.0f));
    CHECK(rangeAt(LostWeather::Clear, 0.0f, 3.0f) == rangeAt(LostWeather::Clear, 0.0f, -21.0f));
}

TEST_CASE("visibility survives NaN, infinite and extreme inputs", "[lost]")
{
    for (float v : {kNaN, kInf, -kInf, 1e30f, -1e30f, 0.0f, -1.0f, 5.0f}) {
        for (int k = -1; k < 6; ++k) {
            const int r = visibilityRangeMeters({static_cast<LostWeather>(k), v}, v);
            CHECK(r >= kMinVisibilityMeters);
            CHECK(r <= kMaxVisibilityMeters);
        }
    }
    CHECK(rangeAt(LostWeather::Fog, kNaN, 12.0f) == rangeAt(LostWeather::Fog, 0.0f, 12.0f));
    CHECK(rangeAt(LostWeather::Fog, 7.0f, 12.0f) == rangeAt(LostWeather::Fog, 1.0f, 12.0f));
    CHECK(rangeAt(LostWeather::Clear, 0.0f, kNaN) == rangeAt(LostWeather::Clear, 0.0f, 12.0f));
}

TEST_CASE("low visibility with no landmark or compass gets the walker lost", "[lost]")
{
    const LostState s = walk({}, 30, 20, {});
    CHECK(s.lost);
    CHECK(s.disorientation == kMaxDisorientation);
}

TEST_CASE("a lower visibility loses the walker faster", "[lost]")
{
    const LostState dim = walk({}, 5, 100, {});
    const LostState blind = walk({}, 5, 10, {});
    CHECK(blind.disorientation > dim.disorientation * 3);
    CHECK(dim.disorientation > 0);
}

TEST_CASE("good visibility never gets the walker lost", "[lost]")
{
    const LostState s = walk({}, 600, kLostVisibilityMeters, {});
    CHECK(s.disorientation == 0);
    CHECK_FALSE(s.lost);
}

TEST_CASE("a landmark or a compass prevents getting lost in fog", "[lost]")
{
    CHECK(walk({}, 600, 5, {true, false, false}).disorientation == 0);
    CHECK(walk({}, 600, 5, {false, true, false}).disorientation == 0);
}

TEST_CASE("a lost walker recovers on a road, river or high ground", "[lost]")
{
    const LostState lost = walk({}, 30, 5, {});
    REQUIRE(lost.lost);
    const LostState found = walk(lost, 5, 5, {false, false, true});
    CHECK_FALSE(found.lost);
    CHECK(found.disorientation == 0);
}

TEST_CASE("recovery without a guide is slow and needs a landmark", "[lost]")
{
    const LostState lost = walk({}, 30, 5, {});
    const LostState stillLost = walk(lost, 2, 5, {true, false, false});
    CHECK(stillLost.lost);
    const LostState later = walk(lost, 60, 5, {true, false, false});
    CHECK_FALSE(later.lost);
    CHECK(walk(lost, 60, 5, {}).lost);
}

TEST_CASE("lost has hysteresis", "[lost]")
{
    LostState s = walk({}, 30, 5, {});
    REQUIRE(s.lost);
    int stillLostSteps = 0;
    for (int i = 0; i < 2000 && s.lost; ++i) {
        s = stepLostState(s, 5, {true, false, false}, 100);
        if (s.disorientation > kFoundThreshold) {
            CHECK(s.lost);
            ++stillLostSteps;
        }
    }
    CHECK(stillLostSteps > 0);
    CHECK_FALSE(s.lost);
}

TEST_CASE("lost state survives hostile inputs", "[lost]")
{
    for (int vis : {std::numeric_limits<int>::min(), -1, 0, std::numeric_limits<int>::max()}) {
        for (int dt : {std::numeric_limits<int>::min(), -1, 0, 1, std::numeric_limits<int>::max()}) {
            const LostState s = stepLostState({}, vis, {}, dt);
            CHECK(s.disorientation >= 0);
            CHECK(s.disorientation <= kMaxDisorientation);
        }
    }
    LostState bad{std::numeric_limits<int>::max(), true};
    bad = stepLostState(bad, 5, {}, 100);
    CHECK(bad.disorientation == kMaxDisorientation);
    bad = {std::numeric_limits<int>::min(), false};
    bad = stepLostState(bad, 5, {}, 100);
    CHECK(bad.disorientation >= 0);
    CHECK(bad.disorientation <= kMaxDisorientation);
}

TEST_CASE("zero elapsed time changes nothing and the step is deterministic", "[lost]")
{
    const LostState a = walk({}, 3, 30, {});
    CHECK(stepLostState(a, 5, {}, 0).disorientation == a.disorientation);
    CHECK(walk({}, 3, 30, {}).disorientation == a.disorientation);
}
