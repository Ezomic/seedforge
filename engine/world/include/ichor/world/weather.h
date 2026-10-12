#pragma once

#include <cstdint>

namespace ichor {

struct Atmosphere;

// The weather at one moment: how overcast the sky is and how hard it rains.
// Rain only falls from heavy cloud, so rain is zero until overcast is high.
struct Weather {
    float overcast = 0.0f; // 0 clear, 1 solid cloud
    float rain = 0.0f;     // 0 dry, 1 downpour
};

// A pure function of the world seed and the hours elapsed since the world
// began, built from a few slow waves with seeded phases. The same seed and
// time always give the same weather, on every machine, with nothing to save,
// and it changes over hours rather than seconds.
Weather weatherAt(std::uint64_t worldSeed, double totalHours);

// Greys and dims the light, thickens the fog and brightens the ambient under
// cloud, and carries the rain through for the renderer.
void applyWeather(const Weather& weather, Atmosphere& atmosphere);

}
