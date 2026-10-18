#include "ichor/world/forge.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/validation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

using namespace ichor;

namespace {

// 257 samples a side over a 65 cell forged grid: every 4th sample sits exactly on a forged cell.
constexpr int kGrid = 65;
constexpr std::uint32_t kStride = 4;

WorldDesc gridWorld(std::uint64_t seed = 7)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    return forgedWorldDesc(desc);
}

worldforge::ForgedWorld forgedFor(const WorldDesc& desc, int grid = kGrid, worldforge::ForgePhase stop = worldforge::ForgePhase::Hydrology)
{
    return worldforge::forgeWorld(desc.seed, grid, {}, stop);
}

}

TEST_CASE("forged heights land on the engine samples at the forged cells")
{
    const WorldDesc desc = gridWorld();
    const worldforge::ForgedWorld world = forgedFor(desc);
    REQUIRE(world.ok());
    const ForgedHeightfield forged = heightfieldFromForgedWorld(world, desc);
    REQUIRE(forged.ok());
    REQUIRE(forged.field.samples().size() == 257u * 257u);

    const worldforge::HydrologyMap& map = *world.hydrology();
    const float step = desc.heightRange / 65535.0f;
    for (int z = 0; z < kGrid; ++z) {
        for (int x = 0; x < kGrid; ++x) {
            const float expected = static_cast<float>(map.heightM[static_cast<std::size_t>(z * kGrid + x)]);
            const float actual = forged.field.heightAtSample(static_cast<std::uint32_t>(x) * kStride, static_cast<std::uint32_t>(z) * kStride);
            REQUIRE(actual == Catch::Approx(expected).margin(step));
        }
    }
}

TEST_CASE("the adapter output is the forged terrain, not the stand-in")
{
    const WorldDesc desc = gridWorld();
    const ForgedHeightfield forged = forgeHeightfieldFromWorldforge(desc, kGrid);
    REQUIRE(forged.ok());
    const Heightfield standIn = forgeHeightfield(desc);
    REQUIRE(forged.field.digest() != standIn.digest());

    const worldforge::ForgedWorld world = forgedFor(desc);
    const worldforge::HydrologyMap& map = *world.hydrology();
    const float step = desc.heightRange / 65535.0f;
    std::size_t standInMatches = 0;
    std::size_t forgedMatches = 0;
    for (int z = 0; z < kGrid; ++z) {
        for (int x = 0; x < kGrid; ++x) {
            const float expected = static_cast<float>(map.heightM[static_cast<std::size_t>(z * kGrid + x)]);
            const auto sx = static_cast<std::uint32_t>(x) * kStride;
            const auto sz = static_cast<std::uint32_t>(z) * kStride;
            forgedMatches += std::abs(forged.field.heightAtSample(sx, sz) - expected) <= step ? 1 : 0;
            standInMatches += std::abs(standIn.heightAtSample(sx, sz) - expected) <= step ? 1 : 0;
        }
    }
    REQUIRE(forgedMatches == static_cast<std::size_t>(kGrid) * kGrid);
    REQUIRE(standInMatches < forgedMatches / 10);
}

TEST_CASE("samples between forged cells blend their neighbours")
{
    const WorldDesc desc = gridWorld();
    const worldforge::ForgedWorld world = forgedFor(desc);
    const ForgedHeightfield forged = heightfieldFromForgedWorld(world, desc);
    REQUIRE(forged.ok());
    const float step = desc.heightRange / 65535.0f;
    for (std::uint32_t z = 0; z < 60; ++z) {
        for (std::uint32_t x = 0; x + kStride < 256; x += 3) {
            const std::uint32_t x0 = x / kStride * kStride;
            const float a = forged.field.heightAtSample(x0, z);
            const float b = forged.field.heightAtSample(x0 + kStride, z);
            const float h = forged.field.heightAtSample(x, z);
            REQUIRE(h >= std::min(a, b) - step);
            REQUIRE(h <= std::max(a, b) + step);
        }
    }
    // Halfway between two cells along a row is their mean to within a sample unit.
    const float a = forged.field.heightAtSample(8, 20);
    const float b = forged.field.heightAtSample(12, 20);
    REQUIRE(forged.field.heightAtSample(10, 20) == Catch::Approx((a + b) / 2.0f).margin(2.0f * step));
}

TEST_CASE("the forged heightfield is deterministic and follows the seed")
{
    const WorldDesc desc = gridWorld(11);
    const ForgedHeightfield a = forgeHeightfieldFromWorldforge(desc, kGrid);
    const ForgedHeightfield b = forgeHeightfieldFromWorldforge(desc, kGrid);
    const ForgedHeightfield other = forgeHeightfieldFromWorldforge(gridWorld(12), kGrid);
    REQUIRE(a.ok());
    REQUIRE(a.field.digest() == b.field.digest());
    REQUIRE(a.field.digest() != other.field.digest());
    REQUIRE(a.field.desc().seed == 11u);
}

TEST_CASE("the forged heightfield has sea and land at the forged sea level")
{
    const WorldDesc desc = gridWorld();
    const ForgedHeightfield forged = forgeHeightfieldFromWorldforge(desc, kGrid);
    REQUIRE(forged.ok());
    REQUIRE(desc.waterLevel() == Catch::Approx(static_cast<float>(kForgedSeaLevelM)));
    std::size_t below = 0;
    std::size_t above = 0;
    for (std::uint32_t z = 0; z < 257; ++z) {
        for (std::uint32_t x = 0; x < 257; ++x) {
            (forged.field.heightAtSample(x, z) > desc.waterLevel() ? above : below)++;
        }
    }
    REQUIRE(above > 0);
    REQUIRE(below > 0);
}

TEST_CASE("the adapter refuses bad input without crashing")
{
    const WorldDesc desc = gridWorld();
    const worldforge::ForgedWorld full = forgedFor(desc);

    WorldDesc bad = desc;
    bad.metresPerSample = 0;
    REQUIRE_FALSE(heightfieldFromForgedWorld(full, bad).ok());
    bad = desc;
    bad.heightRange = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(heightfieldFromForgedWorld(full, bad).ok());
    bad = desc;
    bad.minHeight = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(heightfieldFromForgedWorld(full, bad).ok());
    bad = desc;
    bad.heightRange = 0.0f;
    REQUIRE_FALSE(heightfieldFromForgedWorld(full, bad).ok());

    const worldforge::ForgedWorld early = forgedFor(desc, kGrid, worldforge::ForgePhase::Erosion);
    const ForgedHeightfield stopped = heightfieldFromForgedWorld(early, desc);
    REQUIRE_FALSE(stopped.ok());
    REQUIRE(stopped.field.empty());

    const worldforge::ForgedWorld failed = forgedFor(desc, 8);
    REQUIRE_FALSE(failed.ok());
    REQUIRE_FALSE(heightfieldFromForgedWorld(failed, desc).ok());

    REQUIRE_FALSE(forgeHeightfieldFromWorldforge(desc, 8).ok());
    REQUIRE_FALSE(forgeHeightfieldFromWorldforge(desc, 100000).ok());
    REQUIRE_FALSE(forgeHeightfieldFromWorldforge(desc, -1).ok());
    REQUIRE_FALSE(forgeHeightfieldFromWorldforge(desc, std::numeric_limits<int>::min()).ok());
}

TEST_CASE("heights outside the desc range flatten against its ends")
{
    WorldDesc narrow = gridWorld();
    narrow.minHeight = 100.0f;
    narrow.heightRange = 50.0f;
    const ForgedHeightfield forged = forgeHeightfieldFromWorldforge(narrow, kGrid);
    REQUIRE(forged.ok());
    const auto [low, high] = std::minmax_element(forged.field.samples().begin(), forged.field.samples().end());
    REQUIRE(*low == 0);
    REQUIRE(*high == 65535);
}

TEST_CASE("the forge request flag chooses the forged terrain and defaults to the stand-in")
{
    ForgeRequest request;
    request.desc = gridWorld();
    REQUIRE_FALSE(request.forgedTerrain);
    const ichor::ForgedWorld standIn = forgeWorld(request);
    REQUIRE(standIn.heightfield.digest() == forgeHeightfield(request.desc).digest());

    request.forgedTerrain = true;
    const ichor::ForgedWorld forged = forgeWorld(request);
    REQUIRE(forged.heightfield.digest() == forgeHeightfieldFromWorldforge(request.desc).field.digest());
    REQUIRE(forged.heightfield.digest() != standIn.heightfield.digest());
}
