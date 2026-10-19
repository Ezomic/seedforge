#include "ichor/world/forged_atmosphere.h"

#include "ichor/world/atmosphere.h"

#include <algorithm>
#include <cmath>

namespace ichor {

ForgedBiomeLook forgedBiomeLook(worldforge::BiomeId biome)
{
    using worldforge::BiomeId;
    ForgedBiomeLook look;
    look.ground = biomeSurface(biome).groundColour * 0.5f;
    switch (biome) {
    case BiomeId::Desert:
    case BiomeId::Savanna:
    case BiomeId::SaltBlight:
        look.haze = {1.12f, 1.0f, 0.82f};
        break;
    case BiomeId::Glacier:
    case BiomeId::Alpine:
    case BiomeId::Tundra:
        look.haze = {0.94f, 1.0f, 1.1f};
        break;
    case BiomeId::Forest:
    case BiomeId::Taiga:
    case BiomeId::Rainforest:
    case BiomeId::Marsh:
        look.haze = {0.95f, 1.05f, 0.98f};
        break;
    default:
        look.haze = {1.06f, 1.0f, 0.92f};
        break;
    }
    return look;
}

Weather forgedWeather(const ForgedAtmosphereConfig& config, Weather weather)
{
    weather.overcast = std::clamp(std::isfinite(weather.overcast) ? weather.overcast : 0.0f, 0.0f, config.maxOvercast);
    weather.rain = 0.0f;
    return weather;
}

void applyForgedAtmosphere(const ForgedAtmosphereConfig& config, const ForgedBiomeLook& look, Atmosphere& atmosphere)
{
    atmosphere.fogDensity *= config.fogDensityScale;
    atmosphere.horizon *= look.haze;
    atmosphere.fogColor = atmosphere.horizon;
    atmosphere.ambient *= config.ambientGain;
    atmosphere.sunIntensity *= config.sunIntensityGain;
    atmosphere.sunColor *= config.sunTint;
}

}
