#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace ichor {

struct RenderView;

// What the sky and the light look like at one hour of the day. A pure function
// of the hour, so the same hour always looks the same and nothing needs saving.
struct Atmosphere {
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f}; // towards the light that casts shadows: the sun by day, the moon by night
    glm::vec3 sunColor{1.0f};
    float sunIntensity = 0.0f;
    glm::vec3 ambient{0.0f};
    glm::vec3 zenith{0.0f};  // sky colour straight up, linear
    glm::vec3 horizon{0.0f}; // sky colour at the horizon, linear
    glm::vec3 fogColor{0.0f};
    float fogDensity = 0.0f; // per metre
    float rain = 0.0f;       // 0 dry to 1 downpour; set by applyWeather
    glm::vec2 windDirection{1.0f, 0.0f}; // ground-plane unit vector the wind blows towards
    float windStrength = 0.0f;           // 0 still to 1 gale; applyWeather raises it under cloud
    float fogHeightFalloff = 0.0f;       // per metre: how fast the fog thins with height, so it pools in hollows
    float cloudCover = 0.0f;             // 0 a clear sky to 1 solid cloud; weather thickens it
    float lightShafts = 0.0f;            // strength of the sun's scattering in the fog; cloud takes it away
    float sunElevation = 0.0f; // sine of the sun's angle along its daily circle, about its height over the horizon, for tests and tuning
};

// Hours run 0 to 24 and wrap. Sunrise is at 6 and sunset at 18.
Atmosphere atmosphereAt(float hour);

// The hour of the day after `tick` fixed steps, when a whole day lasts
// `dayLengthSeconds` of simulated time and play starts at `startHour`.
float hourOfDay(std::uint64_t tick, double fixedDt, double dayLengthSeconds, float startHour);

// Copies an atmosphere into a view and turns its sky on.
void applyAtmosphere(const Atmosphere& atmosphere, RenderView& view);

}
