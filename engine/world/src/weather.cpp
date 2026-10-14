#include "ichor/world/weather.h"

#include "ichor/core/dmath.h"
#include "ichor/core/hash.h"
#include "ichor/world/atmosphere.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

float smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// A phase in [0, 2 pi) from the seed and a stream number.
float phase(std::uint64_t seed, std::uint64_t stream)
{
    // hashCombine alone leaves the top bits nearly unchanged between neighbouring
    // seeds, so the result is finished with a murmur-style mix.
    std::uint64_t h = hashCombine(seed, stream);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return static_cast<float>(h >> 40) / 16777216.0f * 6.28318530718f;
}

}

Weather weatherAt(std::uint64_t worldSeed, double totalHours)
{
    // Keep the angle small: float sines lose precision on large arguments, and the
    // fractional part of the wave count carries all the information.
    const auto wave = [&](double periodHours, std::uint64_t stream) {
        const double turns = totalHours / periodHours;
        const auto angle = static_cast<float>((turns - std::floor(turns)) * 6.28318530718) + phase(worldSeed, stream);
        return dmath::sin(angle);
    };
    // Weeks, days and half-days of cloud, the slow ones weighing most.
    const float mix = 0.5f * wave(150.0, 1) + 0.3f * wave(37.0, 2) + 0.2f * wave(11.0, 3);
    Weather weather;
    weather.overcast = std::clamp(smooth(-0.15f, 0.7f, mix), 0.0f, 1.0f);
    weather.rain = smooth(0.78f, 0.97f, weather.overcast);
    return weather;
}

void applyWeather(const Weather& weather, Atmosphere& atmosphere)
{
    const float cloud = weather.overcast;
    // Cloud scatters the sun: weaker, greyer light, with more of the sky's own light about.
    atmosphere.sunIntensity *= 1.0f - 0.85f * cloud;
    const float sunLuma = glm::dot(atmosphere.sunColor, glm::vec3(0.3f, 0.59f, 0.11f));
    atmosphere.sunColor = glm::mix(atmosphere.sunColor, glm::vec3(sunLuma), cloud * 0.8f);
    atmosphere.ambient *= 1.0f + 0.4f * cloud;

    const auto grey = [&](glm::vec3 colour, float darken) {
        const float luma = glm::dot(colour, glm::vec3(0.3f, 0.59f, 0.11f));
        return glm::mix(colour, glm::vec3(luma), cloud * 0.85f) * (1.0f - darken * cloud);
    };
    atmosphere.zenith = grey(atmosphere.zenith, 0.25f);
    atmosphere.horizon = grey(atmosphere.horizon, 0.15f);
    atmosphere.fogColor = atmosphere.horizon;
    atmosphere.fogDensity *= 1.0f + 3.0f * cloud + 5.0f * weather.rain;
    atmosphere.rain = weather.rain;
    atmosphere.lightShafts *= 1.0f - cloud;
    atmosphere.cloudCover += (1.0f - atmosphere.cloudCover) * cloud;
    // Storms blow: the breeze of a clear day grows to a gale under solid cloud.
    atmosphere.windStrength = std::clamp(atmosphere.windStrength + (1.0f - atmosphere.windStrength) * cloud * cloud, 0.0f, 1.0f);
}

}
