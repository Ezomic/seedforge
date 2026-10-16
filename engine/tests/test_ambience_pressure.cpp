#include "ichor/audio/ambience_pressure.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

}  // namespace

TEST_CASE("calm pressure leaves the ambience untouched", "[audio][pressure]")
{
    const auto p = ambiencePressure(0.0f);
    CHECK(p.livingGain == 1.0f);
    CHECK(p.droneGain == 0.0f);
    CHECK(p.lowpassHz == kAmbienceCalmLowpassHz);
    CHECK(p.detuneCents == 0.0f);
    CHECK(p.livingDropout == 0.0f);
}

TEST_CASE("full pressure silences the living layers and raises the drone", "[audio][pressure]")
{
    const auto p = ambiencePressure(1.0f);
    CHECK(p.livingGain == 0.0f);
    CHECK(p.droneGain == 1.0f);
    CHECK(p.lowpassHz == kAmbienceFullLowpassHz);
    CHECK(p.detuneCents == kAmbienceMaxDetuneCents);
    CHECK(p.livingDropout == 1.0f);
}

TEST_CASE("birds thin before they vanish, and the drone is absent at first", "[audio][pressure]")
{
    const auto early = ambiencePressure(0.2f);
    CHECK(early.livingGain > 0.0f);
    CHECK(early.livingGain < 1.0f);
    CHECK(early.droneGain > 0.0f);
    CHECK(ambiencePressure(0.1f).droneGain == 0.0f);
    CHECK(ambiencePressure(0.6f).livingGain == 0.0f);
}

TEST_CASE("bad and extreme levels are clamped", "[audio][pressure]")
{
    const auto calm = ambiencePressure(0.0f);
    const auto full = ambiencePressure(1.0f);
    for (float bad : {-1.0f, -1e30f, -kInf, kNan, -0.0f}) {
        const auto p = ambiencePressure(bad);
        CHECK(p.livingGain == calm.livingGain);
        CHECK(p.droneGain == calm.droneGain);
        CHECK(p.lowpassHz == calm.lowpassHz);
        CHECK(p.detuneCents == calm.detuneCents);
        CHECK(p.livingDropout == calm.livingDropout);
    }
    for (float big : {1.5f, 1e30f, kInf, std::numeric_limits<float>::max()}) {
        const auto p = ambiencePressure(big);
        CHECK(p.livingGain == full.livingGain);
        CHECK(p.droneGain == full.droneGain);
        CHECK(p.lowpassHz == full.lowpassHz);
        CHECK(p.detuneCents == full.detuneCents);
        CHECK(p.livingDropout == full.livingDropout);
    }
}

TEST_CASE("every parameter is monotonic and in range across the whole level", "[audio][pressure]")
{
    auto prev = ambiencePressure(0.0f);
    for (int i = 1; i <= 2000; ++i) {
        const float level = static_cast<float>(i) / 2000.0f;
        const auto p = ambiencePressure(level);
        REQUIRE(p.livingGain <= prev.livingGain);
        REQUIRE(p.droneGain >= prev.droneGain);
        REQUIRE(p.lowpassHz <= prev.lowpassHz);
        REQUIRE(p.detuneCents >= prev.detuneCents);
        REQUIRE(p.livingDropout >= prev.livingDropout);
        REQUIRE(p.livingGain >= 0.0f);
        REQUIRE(p.livingGain <= 1.0f);
        REQUIRE(p.droneGain <= 1.0f);
        REQUIRE(p.lowpassHz >= kAmbienceFullLowpassHz);
        REQUIRE(p.livingDropout <= 1.0f);
        prev = p;
    }
}

TEST_CASE("the one-shot pool loses living entries as pressure rises", "[audio][pressure]")
{
    constexpr int kDraws = 1000;
    auto survivors = [](float level) {
        int n = 0;
        for (int i = 0; i < kDraws; ++i) {
            if (livingOneShotSurvives(level, static_cast<float>(i) / kDraws)) ++n;
        }
        return n;
    };
    CHECK(survivors(0.0f) == kDraws);
    CHECK(survivors(0.4f) < kDraws);
    CHECK(survivors(0.4f) > 0);
    CHECK(survivors(0.4f) > survivors(0.6f));
    CHECK(survivors(1.0f) == 0);

    for (int r = 0; r < kDraws; ++r) {
        const float roll = static_cast<float>(r) / kDraws;
        bool wasDead = false;
        for (int i = 0; i <= 100; ++i) {
            const bool alive = livingOneShotSurvives(static_cast<float>(i) / 100.0f, roll);
            if (wasDead) REQUIRE_FALSE(alive);
            wasDead = !alive;
        }
    }
}

TEST_CASE("one-shot survival is deterministic and tolerates hostile input", "[audio][pressure]")
{
    CHECK(livingOneShotSurvives(0.5f, 0.9f) == livingOneShotSurvives(0.5f, 0.9f));
    CHECK(livingOneShotSurvives(kNan, kNan));
    CHECK(livingOneShotSurvives(-3.0f, 0.0f));
    CHECK_FALSE(livingOneShotSurvives(kInf, 0.5f));
    CHECK_FALSE(livingOneShotSurvives(1.0f, -kInf));
    CHECK(livingOneShotSurvives(0.5f, kInf));
}
