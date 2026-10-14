#include "ichor/world/palette.h"
#include "ichor/gfx/render_view.h"
#include "ichor/world/atmosphere.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <cmath>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr double kDt = 1.0 / 60.0;

float distance(const glm::vec3& a, const glm::vec3& b)
{
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

}

TEST_CASE("the hour follows the tick and wraps around the day")
{
    constexpr double day = 1200.0; // seconds of simulated time
    REQUIRE(hourOfDay(0, kDt, day, 9.0f) == Approx(9.0f));
    REQUIRE(hourOfDay(static_cast<std::uint64_t>(day / kDt / 2), kDt, day, 9.0f) == Approx(21.0f).margin(0.01));
    REQUIRE(hourOfDay(static_cast<std::uint64_t>(day / kDt), kDt, day, 9.0f) == Approx(9.0f).margin(0.01));
    REQUIRE(hourOfDay(static_cast<std::uint64_t>(day / kDt * 3), kDt, day, 9.0f) == Approx(9.0f).margin(0.01));
    const float late = hourOfDay(static_cast<std::uint64_t>(day / kDt * 0.9), kDt, day, 20.0f);
    REQUIRE(late >= 0.0f);
    REQUIRE(late < 24.0f);
    REQUIRE(late == Approx(20.0f + 21.6f - 24.0f).margin(0.01));
}

TEST_CASE("noon is bright and white, midnight is dark and lit by the moon")
{
    const Atmosphere noon = atmosphereAt(12.0f);
    REQUIRE(noon.sunElevation == Approx(1.0f).margin(0.01));
    REQUIRE(noon.sunDirection.y > 0.9f);
    REQUIRE(noon.sunIntensity == Approx(3.0f).margin(0.05));
    REQUIRE(noon.sunColor.b > 0.8f); // close to white

    const Atmosphere midnight = atmosphereAt(0.0f);
    REQUIRE(midnight.sunElevation < -0.9f);
    REQUIRE(midnight.sunIntensity > 0.0f);
    REQUIRE(midnight.sunIntensity < 1.0f);
    REQUIRE(midnight.sunDirection.y > 0.0f); // shadows come from the moon, above the horizon
    REQUIRE(midnight.sunColor.b > midnight.sunColor.r); // cold light
    REQUIRE(midnight.zenith.b < noon.zenith.b * 0.2f);
    REQUIRE(midnight.ambient.g < noon.ambient.g * 0.4f);
}

TEST_CASE("the sun rises in the east, sets in the west, and the horizon reddens near it")
{
    const Atmosphere dawn = atmosphereAt(6.0f);
    const Atmosphere dusk = atmosphereAt(18.0f);
    REQUIRE(dawn.sunDirection.x > 0.9f);
    REQUIRE(dusk.sunDirection.x < -0.9f);
    REQUIRE(dawn.sunElevation == Approx(0.0f).margin(0.01));

    const Atmosphere low = atmosphereAt(17.4f); // the sun just above the horizon
    const Atmosphere noon = atmosphereAt(12.0f);
    REQUIRE(low.horizon.r > low.horizon.b);
    REQUIRE(low.sunColor.r > low.sunColor.b * 1.3f);
    REQUIRE(noon.horizon.b > noon.horizon.r);
}

TEST_CASE("the sky and the light change smoothly through the whole day")
{
    Atmosphere previous = atmosphereAt(0.0f);
    for (float hour = 0.01f; hour <= 24.0f; hour += 0.01f) {
        const Atmosphere now = atmosphereAt(hour);
        INFO("hour " << hour);
        REQUIRE(distance(now.zenith, previous.zenith) < 0.03f);
        REQUIRE(distance(now.horizon, previous.horizon) < 0.03f);
        REQUIRE(distance(now.ambient, previous.ambient) < 0.03f);
        REQUIRE(std::abs(now.sunIntensity - previous.sunIntensity) < 0.08f);
        REQUIRE(now.fogDensity >= 0.0f);
        previous = now;
    }
    // The day is periodic.
    REQUIRE(distance(atmosphereAt(0.0f).zenith, atmosphereAt(24.0f).zenith) < 1e-4f);
    REQUIRE(distance(atmosphereAt(-6.0f).horizon, atmosphereAt(18.0f).horizon) < 1e-4f);
}

TEST_CASE("an atmosphere is copied into a view and turns the sky on")
{
    RenderView view;
    REQUIRE_FALSE(view.skyEnabled);
    const Atmosphere dusk = atmosphereAt(18.5f);
    applyAtmosphere(dusk, view);
    REQUIRE(view.skyEnabled);
    REQUIRE(view.sunDirection == dusk.sunDirection);
    REQUIRE(view.skyZenith == dusk.zenith);
    REQUIRE(view.skyHorizon == dusk.horizon);
    REQUIRE(view.fogColor == dusk.fogColor);
    REQUIRE(view.fogDensity == Approx(dusk.fogDensity));
    REQUIRE(view.sunIntensity == Approx(dusk.sunIntensity));
}

TEST_CASE("the wind has a unit direction that veers slowly and a moderate strength")
{
    glm::vec2 previous = atmosphereAt(0.0f).windDirection;
    for (float hour = 0.01f; hour <= 24.0f; hour += 0.01f) {
        const Atmosphere a = atmosphereAt(hour);
        REQUIRE(glm::length(a.windDirection) == Approx(1.0f).margin(1e-4));
        REQUIRE(glm::length(a.windDirection - previous) < 0.01f);
        REQUIRE(a.windStrength > 0.0f);
        REQUIRE(a.windStrength < 0.5f);
        REQUIRE(a.fogHeightFalloff > 0.0f);
        previous = a.windDirection;
    }
}

TEST_CASE("the wind and the height fog are copied into the view")
{
    RenderView view;
    REQUIRE(view.windStrength == 0.0f);
    REQUIRE(view.fogHeightFalloff == 0.0f);
    REQUIRE(view.lightShafts == 0.0f);
    const Atmosphere a = atmosphereAt(10.0f);
    applyAtmosphere(a, view);
    REQUIRE(view.windDirection == a.windDirection);
    REQUIRE(view.windStrength == Approx(a.windStrength));
    REQUIRE(view.fogHeightFalloff == Approx(a.fogHeightFalloff));
    REQUIRE(view.lightShafts == Approx(a.lightShafts));
}

TEST_CASE("a view starts with a neutral grade and an open material response")
{
    RenderView view;
    REQUIRE(view.grade.saturation == 1.0f);
    REQUIRE(view.grade.contrast == 1.0f);
    REQUIRE(view.grade.exposure == 1.0f);
    REQUIRE(view.grade.shadows == glm::vec3(1.0f));
    REQUIRE(view.grade.highlights == glm::vec3(1.0f));
    REQUIRE(view.response.roughnessMin <= 0.04f);
    REQUIRE(view.response.roughnessMax == 1.0f);
    REQUIRE(view.response.albedoMin == 0.0f);
    REQUIRE(view.response.albedoMax >= 1.0f);
    REQUIRE(view.groundBlendHeight == 0.0f);
}

TEST_CASE("the default palette is a real grade over a narrowed response, and applying it copies both")
{
    const Palette palette = defaultPalette();
    REQUIRE(palette.grade.saturation > 0.0f);
    REQUIRE(palette.grade.saturation < 1.0f);
    REQUIRE(palette.grade.contrast > 1.0f);
    REQUIRE(palette.grade.exposure > 0.0f);
    // Cool shadows, warm highlights.
    REQUIRE(palette.grade.shadows.b > palette.grade.shadows.r);
    REQUIRE(palette.grade.highlights.r > palette.grade.highlights.b);
    REQUIRE(palette.response.roughnessMin > 0.04f);
    REQUIRE(palette.response.roughnessMin < palette.response.roughnessMax);
    REQUIRE(palette.response.roughnessMax <= 1.0f);
    REQUIRE(palette.response.albedoMin > 0.0f);
    REQUIRE(palette.response.albedoMin < palette.response.albedoMax);
    REQUIRE(palette.response.albedoMax < 1.0f);

    RenderView view;
    applyPalette(palette, view);
    REQUIRE(view.grade.saturation == palette.grade.saturation);
    REQUIRE(view.grade.shadows == palette.grade.shadows);
    REQUIRE(view.response.roughnessMin == palette.response.roughnessMin);
    REQUIRE(view.response.albedoMax == palette.response.albedoMax);
    REQUIRE(view.groundBlendHeight == palette.groundBlendHeight);
    REQUIRE(view.groundColor == palette.groundColor);
    REQUIRE(palette.groundBlendHeight > 0.0f);
}
