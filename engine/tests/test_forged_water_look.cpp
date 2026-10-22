#include "ichor/gfx/water_look.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/forged_water.h"
#include "ichor/world/water_field.h"
#include "ichor/world/water_mesh.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

using namespace ichor;

namespace {

constexpr int kGrid = 129;
constexpr std::array<std::uint64_t, 8> kSeeds{3, 7, 11, 16, 21, 34, 55, 89};

struct Forged {
    WorldDesc desc;
    Heightfield field;
    ForgedWater water;
    std::size_t riverCells = 0;
};

Forged forge(std::uint64_t seed)
{
    WorldDesc base;
    base.seed = seed;
    base.sizeMetres = 2048;
    base.metresPerSample = 8;
    base.cellMetres = 512;
    const WorldDesc desc = forgedWorldDesc(base);
    worldforge::ForgeConfig config;
    config.hydrology.riverThreshold = 48;
    worldforge::ForgedWorld world = worldforge::forgeWorld(desc.seed, kGrid, config, worldforge::ForgePhase::Hydrology);
    REQUIRE(world.ok());
    ForgedHeightfield forged = heightfieldFromForgedWorld(world, desc);
    REQUIRE(forged.ok());
    ForgedWater water = waterFromForgedWorld(world, forged.field);
    REQUIRE(water.ok());
    const worldforge::HydrologyMap& map = *world.hydrology();
    const auto riverCells = static_cast<std::size_t>(std::count_if(map.river.begin(), map.river.end(), [](auto r) { return r != 0; }));
    return Forged{desc, std::move(forged.field), std::move(water), riverCells};
}

float luminance(const glm::vec3& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

std::string shaderSource()
{
    const std::filesystem::path path = std::filesystem::path(ICHOR_REPO_DATA).parent_path() / "engine" / "gfx" / "shaders" / "water.hlsli";
    std::ifstream in(path);
    REQUIRE(in.good());
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

glm::vec3 hlslVec3(const std::string& source, const std::string& name)
{
    const std::size_t at = source.find(name + " = float3(");
    REQUIRE(at != std::string::npos);
    glm::vec3 v{0.0f};
    REQUIRE(std::sscanf(source.c_str() + at + name.size() + 10, "%f, %f, %f", &v.x, &v.y, &v.z) == 3);
    return v;
}

}

TEST_CASE("the water body gets darker and bluer with depth, and holds its deep colour past the tint depth")
{
    float previous = luminance(waterDepthTint(0.0f));
    for (float depth = 0.05f; depth <= 8.0f; depth += 0.05f) {
        const float now = luminance(waterDepthTint(depth));
        REQUIRE(now <= previous + 1e-6f);
        previous = now;
    }
    CHECK(waterDepthTint(0.0f) == kWaterShallowTint);
    CHECK(waterDepthTint(kWaterTintDepth) == kWaterDeepTint);
    CHECK(waterDepthTint(50.0f) == kWaterDeepTint);
    CHECK(waterDepthTint(-1.0f) == kWaterShallowTint);
    CHECK(luminance(waterDepthTint(0.0f)) > 2.0f * luminance(waterDepthTint(kWaterTintDepth)));
    CHECK(waterDepthTint(0.0f).g > waterDepthTint(0.0f).b - 0.05f); // turquoise: green as strong as blue
    CHECK(waterDepthTint(kWaterTintDepth).b > waterDepthTint(kWaterTintDepth).g * 2.0f); // deep blue
}

TEST_CASE("the shader's depth tint is the one the tests hold to")
{
    const std::string source = shaderSource();
    CHECK(hlslVec3(source, "kShallowTint") == kWaterShallowTint);
    CHECK(hlslVec3(source, "kDeepTint") == kWaterDeepTint);
    CHECK(source.find(std::format("kTintDepth = {:.1f};", kWaterTintDepth)) != std::string::npos);
}

TEST_CASE("forged water lies over its bed and under its bank on eight seeds")
{
    int dryWorlds = 0;
    for (const std::uint64_t seed : kSeeds) {
        const Forged f = forge(seed);
        const WaterMeshParams params;
        const std::uint32_t cellsPerSide = static_cast<std::uint32_t>(f.desc.sizeMetres / f.desc.cellMetres);
        const std::uint32_t perCell = f.desc.samplesPerCellSide() - 1;
        std::size_t wet = 0;
        std::size_t sunk = 0;
        for (std::uint32_t cz = 0; cz < cellsPerSide; ++cz) {
            for (std::uint32_t cx = 0; cx < cellsPerSide; ++cx) {
                const Mesh mesh = buildWaterMesh(f.field, *f.water.hydrology, f.water.rivers, CellId{static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cz)}, params);
                for (const Vertex& v : mesh.vertices) {
                    const auto sx = cx * perCell + static_cast<std::uint32_t>(v.position.x / f.desc.metresPerSample + 0.5f);
                    const auto sz = cz * perCell + static_cast<std::uint32_t>(v.position.z / f.desc.metresPerSample + 0.5f);
                    const float ground = f.field.heightAtSample(sx, sz);
                    const float over = v.position.y - ground;
                    REQUIRE(v.uv.x == Catch::Approx(over).margin(1e-4));
                    if (over > 0.0f) {
                        // No sliver of water a hair over the ground: that is where it would z-fight with the bed.
                        REQUIRE(over >= 0.02f);
                        if (f.water.hydrology->lake[f.water.hydrology->index(sx, sz)] == 0) {
                            REQUIRE(over <= params.riverSurface + 1e-3f); // a river never stands above its bank
                        }
                        ++wet;
                    } else {
                        REQUIRE(over <= -params.submerge + 1e-3f); // sunk under the bank, not lying on it
                        ++sunk;
                    }
                }
            }
        }
        if (wet == 0) {
            ++dryWorlds;
            continue;
        }
        INFO("seed " << seed);
        CHECK(sunk > 0);
    }
    CHECK(dryWorlds <= 2);
}

TEST_CASE("a forged river is a connected, visible ribbon and not a block per forged cell or a hairline")
{
    int dryWorlds = 0;
    for (const std::uint64_t seed : kSeeds) {
        const Forged f = forge(seed);
        const Hydrology& h = *f.water.hydrology;
        const WaterMeshParams params;
        std::size_t rivers = 0;
        for (std::uint32_t z = 4; z + 4 < h.side; ++z) {
            for (std::uint32_t x = 4; x + 4 < h.side; ++x) {
                if (!isRiver(h, h.index(x, z), f.water.rivers)) {
                    continue;
                }
                ++rivers;
                int next = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        next += (dx != 0 || dz != 0) && isRiver(h, h.index(x + dx, z + dz), f.water.rivers) ? 1 : 0;
                    }
                }
                INFO("seed " << seed << " sample " << x << "," << z);
                REQUIRE(next >= 3); // the ribbon is five samples across, so even its fringe has a row of company inside, so every sample of it has company
                if (h.lake[h.index(x, z)] == 0) {
                    REQUIRE(waterLevelAtSample(f.field, h, f.water.rivers, params, x, z).wet);
                }
            }
        }
        if (rivers == 0) {
            ++dryWorlds;
            continue;
        }
        INFO("seed " << seed);
        CHECK(rivers < f.riverCells * 20); // a five wide ribbon is about 12 samples a cell; a ribbon that ran away would be far more
    }
    CHECK(dryWorlds <= 2);
}
