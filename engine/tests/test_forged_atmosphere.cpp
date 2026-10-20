#include "ichor/world/atmosphere.h"
#include "ichor/world/forged_atmosphere.h"
#include "ichor/world/weather.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
