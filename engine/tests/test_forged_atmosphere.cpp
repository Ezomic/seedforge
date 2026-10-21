#include "ichor/world/atmosphere.h"
#include "ichor/world/forged_atmosphere.h"
#include "ichor/world/weather.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace ichor;
using Catch::Approx;
using worldforge::BiomeId;

TEST_CASE("the forged defaults are the ones the owner approved")
{
    const ForgedAtmosphereConfig config;
    REQUIRE(config.maxOvercast == Approx(0.12f));
    REQUIRE(config.fogDensityScale == Approx(0.3f));
    REQUIRE(config.ambientGain == Approx(1.15f));
    REQUIRE(config.sunIntensityGain == Approx(1.15f));
}

TEST_CASE("a forged sky is never rainy and never heavily overcast")
{
    const ForgedAtmosphereConfig config;
    float stand = 0.0f;
    float forged = 0.0f;
    float standRain = 0.0f;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        for (double hours = 0.0; hours < 2000.0; hours += 7.0) {
            const Weather weather = weatherAt(seed, hours);
            const Weather clamped = forgedWeather(config, weather);
            REQUIRE(clamped.rain == 0.0f);
            REQUIRE(clamped.overcast <= config.maxOvercast);
            REQUIRE(clamped.overcast <= weather.overcast);
            stand += weather.overcast;
            forged += clamped.overcast;
            standRain += weather.rain;
        }
    }
    REQUIRE(standRain > 0.0f);
    REQUIRE(forged < stand * 0.5f);
}

TEST_CASE("a clear sky stays as it is and a bad value counts as clear")
{
    const ForgedAtmosphereConfig config;
    REQUIRE(forgedWeather(config, {0.05f, 0.0f}).overcast == Approx(0.05f));
    REQUIRE(forgedWeather(config, {std::numeric_limits<float>::quiet_NaN(), 1.0f}).overcast == 0.0f);
    REQUIRE(forgedWeather(config, {-1.0f, 0.0f}).overcast == 0.0f);
}

TEST_CASE("the forged atmosphere thins the haze and lifts the light compared with the stand-in one")
{
    const ForgedAtmosphereConfig config;
    for (const Weather weather : {Weather{0.0f, 0.0f}, Weather{1.0f, 1.0f}}) {
        Atmosphere stand = atmosphereAt(10.0f);
        applyWeather(weather, stand);
        Atmosphere forged = atmosphereAt(10.0f);
        applyWeather(forgedWeather(config, weather), forged);
        applyForgedAtmosphere(config, forgedBiomeLook(BiomeId::Grassland), forged);

        REQUIRE(forged.rain == 0.0f);
        REQUIRE(forged.fogDensity < stand.fogDensity * 0.4f);
        REQUIRE(forged.fogColor == forged.horizon);
        if (weather.overcast == 0.0f) {
            REQUIRE(forged.sunIntensity > stand.sunIntensity);
            REQUIRE(forged.ambient.g > stand.ambient.g);
        }
    }
}

TEST_CASE("without the forged functions the atmosphere is the stand-in one")
{
    Atmosphere a = atmosphereAt(10.0f);
    applyWeather(weatherAt(1, 10.0), a);
    Atmosphere b = atmosphereAt(10.0f);
    applyWeather(weatherAt(1, 10.0), b);
    REQUIRE(a.fogDensity == b.fogDensity);
    REQUIRE(a.rain == b.rain);
    REQUIRE(a.horizon == b.horizon);
}

TEST_CASE("each biome family has its own haze and the ground colour follows the biome")
{
    const glm::vec3 desert = forgedBiomeLook(BiomeId::Desert).haze;
    const glm::vec3 glacier = forgedBiomeLook(BiomeId::Glacier).haze;
    const glm::vec3 forest = forgedBiomeLook(BiomeId::Forest).haze;
    REQUIRE(desert.r > desert.b);
    REQUIRE(glacier.b > glacier.r);
    REQUIRE(forest.g > forest.r);
    REQUIRE(forgedBiomeLook(BiomeId::Savanna).haze == desert);
    REQUIRE(forgedBiomeLook(BiomeId::Desert).ground != forgedBiomeLook(BiomeId::Forest).ground);
    for (int id = 0; id < worldforge::kBiomeCount; ++id) {
        const ForgedBiomeLook look = forgedBiomeLook(static_cast<BiomeId>(id));
        REQUIRE(look.haze.r > 0.0f);
        REQUIRE(look.ground.g > 0.0f);
    }
}

namespace {

Atmosphere forgedDay(float hour)
{
    const ForgedAtmosphereConfig config;
    Atmosphere a = atmosphereAt(hour, config.sunPath);
    applyWeather(forgedWeather(config, Weather{0.0f, 0.0f}), a);
    applyForgedAtmosphere(config, forgedBiomeLook(BiomeId::Grassland), a);
    return a;
}

float luma(const glm::vec3& c)
{
    return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float spread(const glm::vec3& a, const glm::vec3& b)
{
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

float change(const Atmosphere& a, const Atmosphere& b)
{
    return std::max({std::abs(a.sunElevation - b.sunElevation), spread(a.sunColor * a.sunIntensity, b.sunColor * b.sunIntensity) * 0.15f, spread(a.zenith, b.zenith),
                     spread(a.horizon, b.horizon), spread(a.ambient, b.ambient), spread(a.fogColor, b.fogColor)});
}

}

TEST_CASE("the forged sun and sky change continuously from one minute to the next, and really do change")
{
    constexpr float minute = 1.0f / 60.0f;
    Atmosphere previous = forgedDay(0.0f);
    double total = 0.0;
    float largest = 0.0f;
    int steps = 0;
    for (float hour = minute; hour <= 24.0f; hour += minute) {
        const Atmosphere now = forgedDay(hour);
        const float step = change(previous, now);
        INFO("hour " << hour);
        REQUIRE(step < 0.035f);
        total += step;
        largest = std::max(largest, step);
        ++steps;
        previous = now;
    }
    const double mean = total / steps;
    REQUIRE(mean > 0.0005);
    REQUIRE(largest < mean * 12.0);
}

TEST_CASE("the forged sun rises in the east, climbs high at noon, and sets in the west")
{
    REQUIRE(forgedDay(8.0f).sunDirection.x > 0.3f);
    REQUIRE(forgedDay(16.0f).sunDirection.x < -0.3f);
    REQUIRE(forgedDay(12.0f).sunElevation == Approx(1.0f).margin(0.01));
    REQUIRE(forgedDay(12.0f).sunDirection.y > 0.9f);
    REQUIRE(forgedDay(9.0f).sunDirection.y < forgedDay(11.0f).sunDirection.y);
    REQUIRE(forgedDay(15.0f).sunDirection.y > forgedDay(17.0f).sunDirection.y);
}

TEST_CASE("the forged sun is already up and golden at six and still up at eighteen, where the stand-in one is on the horizon")
{
    for (const float hour : {6.0f, 18.0f}) {
        const Atmosphere forged = forgedDay(hour);
        const Atmosphere stand = atmosphereAt(hour);
        INFO("hour " << hour);
        REQUIRE(forged.sunElevation > 0.1f);
        REQUIRE(stand.sunElevation == Approx(0.0f).margin(0.01));
        REQUIRE(forged.sunIntensity > stand.sunIntensity + 1.0f);
        REQUIRE(forged.sunColor.r > forged.sunColor.b * 1.3f);
    }
    for (float hour = 0.0f; hour < 24.0f; hour += 0.7f) {
        const float angle = (hour - 6.0f) / 24.0f * 6.28318530718f;
        const glm::vec3 expected = glm::normalize(glm::vec3(std::cos(angle), std::sin(angle), 0.3f));
        INFO("hour " << hour);
        if (std::sin(angle) > 0.0f) {
            REQUIRE(glm::length(atmosphereAt(hour).sunDirection - expected) < 1e-5f);
        }
        REQUIRE(atmosphereAt(hour, SunPath{}).sunDirection == atmosphereAt(hour).sunDirection);
    }
    REQUIRE(ForgedAtmosphereConfig{}.sunPath.elevationLift > 0.0f);
}

TEST_CASE("a forged night is dark but never black, and the stars have a dark sky to show on")
{
    const Atmosphere noon = forgedDay(12.0f);
    for (const float hour : {23.0f, 0.0f, 1.5f}) {
        const Atmosphere night = forgedDay(hour);
        INFO("hour " << hour);
        REQUIRE(night.sunElevation < -0.3f);
        REQUIRE(luma(night.ambient) > 0.02f);
        REQUIRE(luma(night.ambient) < luma(noon.ambient) * 0.4f);
        REQUIRE(luma(night.zenith) > 0.004f);
        REQUIRE(luma(night.zenith) < 0.05f);
        REQUIRE(luma(night.horizon) > 0.01f);
        REQUIRE(night.sunIntensity > 0.1f);
        REQUIRE(night.sunIntensity < noon.sunIntensity * 0.3f);
    }
}

TEST_CASE("a hostile sun path is clamped into a sane arc")
{
    for (const float lift : {-5.0f, 0.99f, 7.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        for (float hour = 0.0f; hour < 24.0f; hour += 0.5f) {
            const Atmosphere a = atmosphereAt(hour, SunPath{lift});
            INFO("lift " << lift << " hour " << hour);
            REQUIRE(std::isfinite(a.sunElevation));
            REQUIRE(std::abs(a.sunElevation) <= 1.0f + 1e-6f);
            REQUIRE(std::isfinite(a.sunDirection.x + a.sunDirection.y + a.sunDirection.z));
            REQUIRE(glm::length(a.sunDirection) == Approx(1.0f).margin(1e-4));
        }
    }
}
