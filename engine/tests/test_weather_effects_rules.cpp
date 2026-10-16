#include "ichor/traversal/weather_effects_rules.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

constexpr LostWeather kKinds[] = {LostWeather::Clear, LostWeather::Rain, LostWeather::Fog, LostWeather::Storm};

WeatherEffects at(LostWeather kind, float intensity)
{
    return weatherEffects({kind, intensity});
}

bool atLeastAsBad(const WeatherEffects& a, const WeatherEffects& b)
{
    return a.moveSpeedScale <= b.moveSpeedScale && a.climbGripScale <= b.climbGripScale &&
           a.boatStabilityScale <= b.boatStabilityScale && a.staminaDrainScale >= b.staminaDrainScale &&
           a.fireSurvivalScale <= b.fireSurvivalScale;
}

bool wellFormed(const WeatherEffects& e)
{
    const float scales[] = {e.moveSpeedScale, e.climbGripScale, e.boatStabilityScale, e.fireSurvivalScale};
    for (const float s : scales) {
        if (!std::isfinite(s) || !(s > 0.0f) || s > 1.0f) {
            return false;
        }
    }
    return std::isfinite(e.staminaDrainScale) && e.staminaDrainScale >= 1.0f;
}
}

TEST_CASE("clear weather and zero intensity change nothing", "[weather_effects]")
{
    for (const auto kind : kKinds) {
        const auto e = at(kind, 0.0f);
        CHECK(e.moveSpeedScale == 1.0f);
        CHECK(e.climbGripScale == 1.0f);
        CHECK(e.boatStabilityScale == 1.0f);
        CHECK(e.staminaDrainScale == 1.0f);
        CHECK(e.fireSurvivalScale == 1.0f);
    }
    CHECK(at(LostWeather::Clear, 1.0f).climbGripScale == 1.0f);
}

TEST_CASE("each non-clear kind hurts at full intensity, measured against clear", "[weather_effects]")
{
    const auto clear = at(LostWeather::Clear, 1.0f);
    for (const auto kind : {LostWeather::Rain, LostWeather::Fog, LostWeather::Storm}) {
        const auto e = at(kind, 1.0f);
        CHECK(e.moveSpeedScale < clear.moveSpeedScale);
        CHECK(e.climbGripScale < clear.climbGripScale);
        CHECK(e.boatStabilityScale < clear.boatStabilityScale);
        CHECK(e.staminaDrainScale > clear.staminaDrainScale);
        CHECK(e.fireSurvivalScale < clear.fireSurvivalScale);
    }
}

TEST_CASE("rain hurts grip and fire more than fog, fog slows more than rain", "[weather_effects]")
{
    const auto rain = at(LostWeather::Rain, 1.0f);
    const auto fog = at(LostWeather::Fog, 1.0f);
    CHECK(rain.climbGripScale < fog.climbGripScale);
    CHECK(rain.fireSurvivalScale < fog.fireSurvivalScale);
    CHECK(fog.moveSpeedScale < rain.moveSpeedScale);
}

TEST_CASE("storm is at least as bad as rain and fog at every intensity", "[weather_effects]")
{
    for (int i = 0; i <= 20; ++i) {
        const float x = static_cast<float>(i) / 20.0f;
        CHECK(atLeastAsBad(at(LostWeather::Storm, x), at(LostWeather::Rain, x)));
        CHECK(atLeastAsBad(at(LostWeather::Storm, x), at(LostWeather::Fog, x)));
    }
}

TEST_CASE("worse weather never helps: monotone in intensity", "[weather_effects]")
{
    for (const auto kind : kKinds) {
        auto prev = at(kind, 0.0f);
        for (int i = 1; i <= 100; ++i) {
            const auto cur = at(kind, static_cast<float>(i) / 100.0f);
            CHECK(atLeastAsBad(cur, prev));
            prev = cur;
        }
    }
}

TEST_CASE("intensity boundaries are exact and clamped", "[weather_effects]")
{
    for (const auto kind : kKinds) {
        const auto one = at(kind, 1.0f);
        CHECK(at(kind, 2.0f).moveSpeedScale == one.moveSpeedScale);
        CHECK(at(kind, 1e30f).staminaDrainScale == one.staminaDrainScale);
        CHECK(at(kind, kInf).fireSurvivalScale == one.fireSurvivalScale);
        const auto zero = at(kind, 0.0f);
        CHECK(at(kind, -1.0f).climbGripScale == zero.climbGripScale);
        CHECK(at(kind, -kInf).boatStabilityScale == zero.boatStabilityScale);
    }
}

TEST_CASE("NaN intensity counts as no weather", "[weather_effects]")
{
    for (const auto kind : kKinds) {
        const auto e = at(kind, kNaN);
        CHECK(e.moveSpeedScale == 1.0f);
        CHECK(e.staminaDrainScale == 1.0f);
        CHECK(wellFormed(e));
    }
}

TEST_CASE("unknown kind counts as clear", "[weather_effects]")
{
    for (const int raw : {4, 99, -1, 255}) {
        const auto e = at(static_cast<LostWeather>(raw), 1.0f);
        CHECK(e.moveSpeedScale == 1.0f);
        CHECK(e.climbGripScale == 1.0f);
        CHECK(e.boatStabilityScale == 1.0f);
        CHECK(e.staminaDrainScale == 1.0f);
        CHECK(e.fireSurvivalScale == 1.0f);
    }
}

TEST_CASE("output is always well formed and deterministic", "[weather_effects]")
{
    const float inputs[] = {-kInf, -1e30f, -1.0f, 0.0f, 1e-30f, 0.25f, 0.5f, 1.0f, 7.0f, 1e30f, kInf, kNaN};
    for (const auto kind : kKinds) {
        for (const float x : inputs) {
            const auto a = at(kind, x);
            const auto b = at(kind, x);
            CHECK(wellFormed(a));
            CHECK(a.moveSpeedScale == b.moveSpeedScale);
            CHECK(a.fireSurvivalScale == b.fireSurvivalScale);
        }
    }
}
