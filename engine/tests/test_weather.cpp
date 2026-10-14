#include "ichor/gfx/render_view.h"
#include "ichor/world/atmosphere.h"
#include "ichor/world/weather.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

using namespace ichor;
using Catch::Approx;

TEST_CASE("weather is a pure function of the seed and the time")
{
    const Weather a = weatherAt(42, 123.456);
    const Weather b = weatherAt(42, 123.456);
    REQUIRE(a.overcast == b.overcast);
    REQUIRE(a.rain == b.rain);

    bool differs = false;
    for (double hours = 0.0; hours < 500.0; hours += 3.0) {
        differs = differs || weatherAt(42, hours).overcast != weatherAt(43, hours).overcast;
    }
    REQUIRE(differs);
}

TEST_CASE("weather stays in range, rain needs heavy cloud, and both clear skies and storms occur")
{
    int clear = 0;
    int stormy = 0;
    for (double hours = 0.0; hours < 4000.0; hours += 0.5) {
        const Weather w = weatherAt(7, hours);
        REQUIRE(w.overcast >= 0.0f);
        REQUIRE(w.overcast <= 1.0f);
        REQUIRE(w.rain >= 0.0f);
        REQUIRE(w.rain <= 1.0f);
        if (w.rain > 0.0f) {
            REQUIRE(w.overcast >= 0.78f);
        }
        clear += w.overcast < 0.05f ? 1 : 0;
        stormy += w.rain > 0.5f ? 1 : 0;
    }
    REQUIRE(clear > 200);
    REQUIRE(stormy > 100);
}

TEST_CASE("weather changes over hours, not between frames, and survives huge times")
{
    float previous = weatherAt(9, 0.0).overcast;
    for (double hours = 0.01; hours < 200.0; hours += 0.01) {
        const float now = weatherAt(9, hours).overcast;
        REQUIRE(std::abs(now - previous) < 0.01f);
        previous = now;
    }
    const Weather far = weatherAt(9, 1.0e9);
    REQUIRE(far.overcast >= 0.0f);
    REQUIRE(far.overcast <= 1.0f);
}

TEST_CASE("cloud dims the sun, greys the sky, thickens the fog and carries the rain")
{
    const Atmosphere clear = atmosphereAt(12.0f);
    Atmosphere none = clear;
    applyWeather({0.0f, 0.0f}, none);
    REQUIRE(none.sunIntensity == Approx(clear.sunIntensity));
    REQUIRE(none.fogDensity == Approx(clear.fogDensity));
    REQUIRE(none.zenith == clear.zenith);
    REQUIRE(none.rain == 0.0f);

    Atmosphere storm = clear;
    applyWeather({1.0f, 1.0f}, storm);
    REQUIRE(storm.sunIntensity < clear.sunIntensity * 0.2f);
    REQUIRE(storm.fogDensity > clear.fogDensity * 5.0f);
    REQUIRE(storm.rain == 1.0f);
    REQUIRE(storm.ambient.g > clear.ambient.g);
    // The sky loses its blue: the channels draw close together.
    const float clearSpread = clear.zenith.b - clear.zenith.r;
    const float stormSpread = storm.zenith.b - storm.zenith.r;
    REQUIRE(stormSpread < clearSpread * 0.4f);
    REQUIRE(storm.fogColor == storm.horizon);
    // Storms blow harder than calm days, and never past a gale.
    REQUIRE(storm.windStrength > clear.windStrength * 2.0f);
    REQUIRE(storm.windStrength <= 1.0f);
    REQUIRE(none.windStrength == Approx(clear.windStrength));
    REQUIRE(clear.lightShafts > 0.0f);
    REQUIRE(none.lightShafts == Approx(clear.lightShafts));
    REQUIRE(storm.lightShafts == 0.0f);
}

TEST_CASE("cloud takes the light shafts away in proportion")
{
    const Atmosphere clear = atmosphereAt(12.0f);
    Atmosphere half = clear;
    applyWeather({0.5f, 0.0f}, half);
    REQUIRE(half.lightShafts == Approx(clear.lightShafts * 0.5f));
}

TEST_CASE("cloud thickens the sky's cover towards solid, and clear weather leaves it as it was")
{
    const Atmosphere clear = atmosphereAt(12.0f);
    REQUIRE(clear.cloudCover > 0.0f);
    REQUIRE(clear.cloudCover < 0.5f); // a fair day has a few clouds
    Atmosphere none = clear;
    applyWeather({0.0f, 0.0f}, none);
    REQUIRE(none.cloudCover == Approx(clear.cloudCover));
    Atmosphere half = clear;
    applyWeather({0.5f, 0.0f}, half);
    REQUIRE(half.cloudCover > clear.cloudCover);
    REQUIRE(half.cloudCover < 1.0f);
    Atmosphere storm = clear;
    applyWeather({1.0f, 1.0f}, storm);
    REQUIRE(storm.cloudCover == Approx(1.0f));
    RenderView view;
    REQUIRE(view.cloudCover == 0.0f);
    applyAtmosphere(storm, view);
    REQUIRE(view.cloudCover == Approx(1.0f));
}

TEST_CASE("surfaces are wet in proportion to the rain, and dry with none")
{
    REQUIRE(wetnessTarget(0.0f) == 0.0f);
    REQUIRE(wetnessTarget(1.0f) == 1.0f);
    REQUIRE(wetnessTarget(0.1f) > 0.0f);
    REQUIRE(wetnessTarget(0.1f) < wetnessTarget(0.2f));
    REQUIRE(wetnessTarget(5.0f) == 1.0f);
    REQUIRE(wetnessTarget(-3.0f) == 0.0f);
    REQUIRE(wetnessTarget(std::nanf("")) == 0.0f);
    REQUIRE(wetnessTarget(INFINITY) == 1.0f);
}

TEST_CASE("wetness rises faster than it dries and never leaves 0 to 1")
{
    const float wetting = wetnessStep(0.0f, 1.0f, 10.0) - 0.0f;
    const float drying = 1.0f - wetnessStep(1.0f, 0.0f, 10.0);
    REQUIRE(wetting > drying);
    REQUIRE(wetting > 0.0f);
    REQUIRE(wetting < 1.0f);

    REQUIRE(wetnessStep(0.0f, 1.0f, 1.0e12) == Approx(1.0f));
    REQUIRE(wetnessStep(1.0f, 0.0f, 1.0e12) == 0.0f);
    REQUIRE(wetnessStep(0.3f, 0.5f, 0.0) == 0.3f);
    REQUIRE(wetnessStep(0.3f, 0.5f, -5.0) == 0.3f);
    REQUIRE(wetnessStep(0.3f, 0.5f, std::nan("")) == 0.3f);
    REQUIRE(wetnessStep(std::nanf(""), 1.0f, 10.0) > 0.0f);
    REQUIRE(wetnessStep(7.0f, 1.0f, 10.0) <= 1.0f);
    REQUIRE(wetnessStep(-7.0f, 0.0f, 10.0) == 0.0f);
    REQUIRE(wetnessStep(0.5f, std::nanf(""), 10.0) < 0.5f);
}

TEST_CASE("wetness follows the rain through the fixed ticks and is deterministic")
{
    const double dt = 1.0 / 60.0;
    SurfaceWetness first;
    REQUIRE(first.update(1.0f, 1000, dt) == 1.0f); // no previous state: the ground starts as wet as the rain says
    SurfaceWetness dry;
    REQUIRE(dry.update(0.0f, 1000, dt) == 0.0f);

    SurfaceWetness a;
    SurfaceWetness b;
    a.update(0.0f, 0, dt);
    b.update(0.0f, 0, dt);
    float last = 0.0f;
    for (std::uint64_t tick = 1; tick <= 600; ++tick) {
        const float value = a.update(1.0f, tick, dt);
        REQUIRE(value >= last);
        REQUIRE(value <= 1.0f);
        last = value;
    }
    REQUIRE(last == Approx(1.0f - std::exp(-10.0f / 20.0f)).margin(1e-3));
    for (std::uint64_t tick = 1; tick <= 600; ++tick) {
        b.update(1.0f, tick, dt);
    }
    REQUIRE(a.value == b.value);
}

TEST_CASE("light rain wets the ground a little at the fixed tick rate")
{
    const double dt = 1.0 / 60.0;
    SurfaceWetness w;
    w.update(0.0f, 0, dt);
    for (std::uint64_t tick = 1; tick <= 600; ++tick) {
        w.update(0.03f, tick, dt);
    }
    REQUIRE(w.value > 0.0f);
    REQUIRE(w.value < wetnessTarget(0.03f));
}

TEST_CASE("wetness stays bounded when the rain flips every tick, across a jump in time, or going back")
{
    const double dt = 1.0 / 60.0;
    SurfaceWetness w;
    w.update(0.0f, 0, dt);
    for (std::uint64_t tick = 1; tick <= 5000; ++tick) {
        const float value = w.update(tick % 2 == 0 ? 1.0f : 0.0f, tick, dt);
        REQUIRE(value >= 0.0f);
        REQUIRE(value <= 1.0f);
    }
    REQUIRE(w.update(1.0f, std::numeric_limits<std::uint64_t>::max(), dt) == Approx(1.0f));
    REQUIRE(w.update(0.0f, 10, dt) == 0.0f); // a restore to an earlier tick starts over
    REQUIRE(w.update(std::nanf(""), 11, dt) == 0.0f);
}

TEST_CASE("the render view is dry by default and the weather alone does not wet it")
{
    RenderView view;
    REQUIRE(view.wetness == 0.0f);
    Atmosphere storm = atmosphereAt(12.0f);
    applyWeather({1.0f, 1.0f}, storm);
    applyAtmosphere(storm, view);
    REQUIRE(view.wetness == 0.0f);
}
