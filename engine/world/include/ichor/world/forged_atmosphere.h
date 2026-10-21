#pragma once

#include "ichor/world/atmosphere.h"
#include "ichor/world/forged_biome.h"
#include "ichor/world/weather.h"

#include <glm/vec3.hpp>

namespace ichor {

struct Atmosphere;

// What a forged world looks like by default (doc 16, items 3 to 5): a fair-weather sky with no rain,
// thinner haze, warmer light, and a haze and ground colour that follow the biome the player stands in.
// Stand-in worlds never see any of it.
struct ForgedAtmosphereConfig {
    float maxOvercast = 0.12f;      // the cloud the weather mix may reach; rain is always dry
    float fogDensityScale = 0.3f;  // multiplier on the stand-in fog
    float ambientGain = 1.15f;     // multiplier on the sky light that fills the shadows
    float sunIntensityGain = 1.15f;
    glm::vec3 sunTint{1.0f, 0.97f, 0.9f}; // multiplier on the sun's colour
    SunPath sunPath{0.15f};               // the sun is about nine degrees up at 6 and 18, so dawn and dusk are lit
};

// The biome's own look: a multiplier on the horizon (and so the fog, which shares its colour so the
// land meets the sky) and the colour objects blend into at their base.
struct ForgedBiomeLook {
    glm::vec3 haze{1.0f};
    glm::vec3 ground{0.1f, 0.16f, 0.06f};
};

ForgedBiomeLook forgedBiomeLook(worldforge::BiomeId biome);

// Caps the cloud and removes the rain.
Weather forgedWeather(const ForgedAtmosphereConfig& config, Weather weather);

// Applied after applyWeather: thins the haze, tints it for the biome, and lifts the light.
void applyForgedAtmosphere(const ForgedAtmosphereConfig& config, const ForgedBiomeLook& look, Atmosphere& atmosphere);

}
