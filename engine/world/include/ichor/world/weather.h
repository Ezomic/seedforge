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

// How wet the ground would end up if this rain fell for long: 0 dry to 1 soaked. Light
// rain already wets the surface through; a bad value counts as dry.
float wetnessTarget(float rain);

// Moves wetness towards what the rain calls for over `seconds`: quickly when wetting, slowly
// when drying. Exact for any step, so a long jump needs no loop. Bad inputs change nothing.
float wetnessStep(float wetness, float rain, double seconds);

// The wetness of the world's surfaces as the renderer sees it, advanced by fixed ticks. Pure
// presentation: it is rebuilt from the rain on load and never saved or hashed.
struct SurfaceWetness {
    float value = 0.0f;
    std::uint64_t tick = 0;
    bool primed = false;

    // The first call, and any call for an earlier tick, takes the rain's own wetness at once.
    float update(float rain, std::uint64_t currentTick, double fixedDt);
};

}
