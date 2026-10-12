#include "ichor/world/atmosphere.h"
#include "ichor/world/weather.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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
}
