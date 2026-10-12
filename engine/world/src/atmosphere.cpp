#include "ichor/world/atmosphere.h"

#include "ichor/gfx/render_view.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kTwoPi = 6.28318530718f;

float smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}

float hourOfDay(std::uint64_t tick, double fixedDt, double dayLengthSeconds, float startHour)
{
    const double days = static_cast<double>(tick) * fixedDt / dayLengthSeconds;
    const double hour = static_cast<double>(startHour) + (days - std::floor(days)) * 24.0;
    return static_cast<float>(hour - 24.0 * std::floor(hour / 24.0));
}

Atmosphere atmosphereAt(float hour)
{
    hour -= 24.0f * std::floor(hour / 24.0f);
    // The sun climbs from the east at 6, peaks at noon, and sets in the west at 18.
    const float angle = (hour - 6.0f) / 24.0f * kTwoPi;
    const float elevation = std::sin(angle);
    const glm::vec3 sun = glm::normalize(glm::vec3(std::cos(angle), elevation, 0.3f));

    // How much of the day's colour is up: zero below about five degrees under the horizon.
    const float day = smooth(-0.1f, 0.15f, elevation);
    // Warm light near the horizon, white at noon.
    const float low = 1.0f - smooth(0.0f, 0.5f, elevation);
    const glm::vec3 white(1.0f, 0.96f, 0.9f);
    const glm::vec3 warm(1.0f, 0.55f, 0.28f);
    const glm::vec3 moon(0.55f, 0.65f, 0.9f);

    Atmosphere a;
    a.sunElevation = elevation;
    if (elevation > -0.05f) {
        a.sunDirection = sun;
        a.sunColor = glm::mix(white, warm, low * 0.9f);
        // Zero where the moon takes over, so the handover never jumps.
        a.sunIntensity = 3.0f * smooth(-0.05f, 0.12f, elevation);
    } else {
        // The moon takes the shadows once the sun is down; it sits opposite the sun.
        a.sunDirection = glm::normalize(glm::vec3(-sun.x, std::max(0.35f, -sun.y), -sun.z));
        a.sunColor = moon;
        a.sunIntensity = 0.35f * smooth(-0.05f, -0.2f, elevation);
    }

    const glm::vec3 nightZenith(0.012f, 0.02f, 0.06f);
    const glm::vec3 nightHorizon(0.03f, 0.04f, 0.09f);
    const glm::vec3 dayZenith(0.12f, 0.3f, 0.65f);
    const glm::vec3 dayHorizon(0.55f, 0.7f, 0.85f);
    const glm::vec3 duskHorizon(0.95f, 0.45f, 0.25f);
    a.zenith = glm::mix(nightZenith, dayZenith, day);
    // The horizon reddens as the sun nears it, whether rising or setting.
    const float glow = smooth(-0.15f, 0.0f, elevation) * (1.0f - smooth(0.0f, 0.35f, elevation));
    a.horizon = glm::mix(glm::mix(nightHorizon, dayHorizon, day), duskHorizon, glow * 0.85f);

    a.ambient = glm::mix(glm::vec3(0.025f, 0.03f, 0.06f), glm::vec3(0.18f, 0.2f, 0.26f), day);
    a.fogColor = a.horizon;
    a.fogDensity = glm::mix(0.0006f, 0.0009f, 1.0f - day);
    return a;
}

void applyAtmosphere(const Atmosphere& atmosphere, RenderView& view)
{
    view.sunDirection = atmosphere.sunDirection;
    view.sunColor = atmosphere.sunColor;
    view.sunIntensity = atmosphere.sunIntensity;
    view.ambient = atmosphere.ambient;
    view.skyEnabled = true;
    view.skyZenith = atmosphere.zenith;
    view.skyHorizon = atmosphere.horizon;
    view.fogColor = atmosphere.fogColor;
    view.fogDensity = atmosphere.fogDensity;
    view.rainIntensity = atmosphere.rain;
}

}
