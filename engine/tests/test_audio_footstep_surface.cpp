#include "ichor/audio/soundscape.h"
#include "ichor/data/data_set.h"
#include "ichor/traversal/roads.h"
#include "ichor/world/forged_biome.h"

#include <worldforge/biome_map.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

using namespace ichor;
using worldforge::BiomeId;

namespace {

constexpr int kGrid = 9;
constexpr std::uint32_t kSizeMetres = 800;

Soundscape realSoundscape()
{
    DataSet data;
    REQUIRE(data.loadDirectory(std::filesystem::path(ICHOR_GAME_DATA_DIR)));
    Soundscape soundscape;
    std::string error;
    REQUIRE(Soundscape::load(data, soundscape, error));
    return soundscape;
}

// Grassland on the west third, crag in the middle, glacier on the east third.
worldforge::BiomeMap stripes()
{
    worldforge::BiomeMap map;
    map.width = kGrid;
    map.height = kGrid;
    const auto cells = static_cast<std::size_t>(kGrid * kGrid);
    map.biome.assign(cells, static_cast<std::uint8_t>(BiomeId::Grassland));
    map.secondary.assign(cells, static_cast<std::uint8_t>(BiomeId::Grassland));
    map.blend.assign(cells, 0);
    for (int z = 0; z < kGrid; ++z) {
        for (int x = 3; x < kGrid; ++x) {
            const auto biome = static_cast<std::uint8_t>(x < 6 ? BiomeId::Crag : BiomeId::Glacier);
            map.biome[map.index(x, z)] = biome;
            map.secondary[map.index(x, z)] = biome;
        }
    }
    return map;
}

// The clip the footsteps system in main.cpp plays at a dry point: the forged splat under the foot and the road
// field's coverage, through the surface pick, to the soundscape's step.
std::optional<std::string> clipAt(const Soundscape& soundscape, const ForgedBiomeField& biomes, const RoadField& roads, double x, double z)
{
    const glm::vec3 splat = biomes.splatAt(x, z);
    const SoundSurface surface = soundSurface(splat.x, splat.y, splat.z, roads.coverageAt(x, z), 0.0f);
    const auto step = footstepFor(soundscape, surface);
    if (!step) {
        return std::nullopt;
    }
    return step->clip;
}

}

TEST_CASE("the chosen footstep follows the dominant splat layer under the foot")
{
    const Soundscape soundscape = realSoundscape();
    const ForgedBiomeField biomes(stripes(), kSizeMetres);
    REQUIRE_FALSE(biomes.empty());
    const NullRoadField noRoads;

    REQUIRE(clipAt(soundscape, biomes, noRoads, 100.0, 400.0) == "A_Step_Grass");
    REQUIRE(clipAt(soundscape, biomes, noRoads, 400.0, 400.0) == "A_Step_Rock");
    REQUIRE(clipAt(soundscape, biomes, noRoads, 750.0, 400.0) == "A_Step_Snow");
}

TEST_CASE("a road under the foot wins over the splat, a shoulder does not")
{
    const Soundscape soundscape = realSoundscape();
    const ForgedBiomeField biomes(stripes(), kSizeMetres);
    const UniformRoadField road(1.0f);
    const UniformRoadField shoulder(kRoadSurfaceCoverage * 0.5f);

    for (const double x : {100.0, 400.0, 750.0}) {
        REQUIRE(clipAt(soundscape, biomes, road, x, 400.0) == "A_Step_Road");
    }
    REQUIRE(clipAt(soundscape, biomes, shoulder, 100.0, 400.0) == "A_Step_Grass");
    REQUIRE(clipAt(soundscape, biomes, shoulder, 400.0, 400.0) == "A_Step_Rock");
}

TEST_CASE("a surface without a step of its own plays the fallback set")
{
    Soundscape onlyGrass;
    onlyGrass.setStep(SoundSurface::Grass, StepSound{"grass_set", 0.5f});

    REQUIRE(footstepFor(onlyGrass, SoundSurface::Rock)->clip == "grass_set");
    REQUIRE(footstepFor(onlyGrass, SoundSurface::Snow)->clip == "grass_set");
    REQUIRE(footstepFor(onlyGrass, SoundSurface::Road)->clip == "grass_set");
    REQUIRE(footstepFor(onlyGrass, static_cast<SoundSurface>(250))->clip == "grass_set");

    const ForgedBiomeField biomes(stripes(), kSizeMetres);
    const NullRoadField noRoads;
    REQUIRE(clipAt(onlyGrass, biomes, noRoads, 400.0, 400.0) == "grass_set");
    REQUIRE(clipAt(onlyGrass, biomes, noRoads, 750.0, 400.0) == "grass_set");
}

TEST_CASE("the fallback never speaks for water or a silent set")
{
    Soundscape onlyGrass;
    onlyGrass.setStep(SoundSurface::Grass, StepSound{"grass_set", 0.5f});
    REQUIRE_FALSE(footstepFor(onlyGrass, SoundSurface::Water).has_value());

    Soundscape silentGrass;
    silentGrass.setStep(SoundSurface::Grass, StepSound{"", 0.5f});
    silentGrass.setStep(SoundSurface::Rock, StepSound{"rock_set", 0.0f});
    REQUIRE_FALSE(footstepFor(silentGrass, SoundSurface::Rock).has_value());
    REQUIRE_FALSE(footstepFor(Soundscape{}, SoundSurface::Snow).has_value());

    Soundscape mapped;
    mapped.setStep(SoundSurface::Grass, StepSound{"grass_set", 0.5f});
    mapped.setStep(SoundSurface::Rock, StepSound{"rock_set", 0.5f});
    REQUIRE(footstepFor(mapped, SoundSurface::Rock)->clip == "rock_set");
}
