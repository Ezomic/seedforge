#include "ichor/world/terrain_shore.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

constexpr std::uint32_t kSide = 65;

// A square heightfield kSide samples to a side, one metre apart, a height in tenths of a metre at each sample.
Heightfield gridOf(float waterHeight, const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& tenths)
{
    WorldDesc desc;
    desc.sizeMetres = kSide - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = kSide - 1;
    desc.heightRange = 6553.5f;
    desc.waterHeight = waterHeight;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(kSide) * kSide);
    for (std::uint32_t z = 0; z < kSide; ++z) {
        for (std::uint32_t x = 0; x < kSide; ++x) {
            samples[static_cast<std::size_t>(z) * kSide + x] = tenths(x, z);
        }
    }
    return Heightfield(desc, std::move(samples));
}

glm::vec2 at(const std::vector<glm::vec2>& shore, std::uint32_t x, std::uint32_t z)
{
    return shore[static_cast<std::size_t>(z) * kSide + x];
}

}

TEST_CASE("sand lies on the low ground beside the sea and nowhere else")
{
    // The west edge is sea floor 2 m high under 5 m of water; the land climbs a tenth of a metre per metre from there.
    const Heightfield field = gridOf(5.0f, [](std::uint32_t x, std::uint32_t) -> std::uint16_t { return x < 20 ? 20 : static_cast<std::uint16_t>(50 + (x - 20) * 4); });
    const Hydrology hydrology = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 1u << 30;
    const std::vector<glm::vec2> shore = terrainShore(field, hydrology, rivers, {0, 0});
    REQUIRE(shore.size() == static_cast<std::size_t>(kSide) * kSide);

    const float onTheBeach = at(shore, 21, 30).x;
    const float aboveTheBeach = at(shore, 32, 30).x;
    const float inland = at(shore, 60, 30).x;
    REQUIRE(onTheBeach > 0.8f);
    REQUIRE(aboveTheBeach < onTheBeach * 0.5f);
    REQUIRE(inland == 0.0f);
    for (std::uint32_t x = 0; x < kSide; ++x) {
        REQUIRE(at(shore, x, 30).y == 0.0f);
    }
}

TEST_CASE("the bank of a river is wet and the ground away from it is dry")
{
    // A valley along z whose floor runs down to the edge at z = 0.
    const Heightfield field = gridOf(0.0f, [](std::uint32_t x, std::uint32_t z) -> std::uint16_t {
        return static_cast<std::uint16_t>(100 + 6 * (x > 32 ? x - 32 : 32 - x) + 2 * z);
    });
    const Hydrology hydrology = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 200;
    rivers.springHalfWidth = 1.0f;
    rivers.maxHalfWidth = 1.0f;
    const std::vector<glm::vec2> shore = terrainShore(field, hydrology, rivers, {0, 0});
    REQUIRE(shore.size() == static_cast<std::size_t>(kSide) * kSide);

    const float channel =at(shore, 32, 8).y;
    const float bank = at(shore, 35, 8).y;
    const float far = at(shore, 55, 8).y;
    REQUIRE(channel > 0.95f);
    REQUIRE(bank < channel);
    REQUIRE(bank > far);
    REQUIRE(far == 0.0f);
    REQUIRE(at(shore, 32, 8).x == 0.0f);
}

TEST_CASE("a hydrology of another size or none gives no shore")
{
    const Heightfield field = gridOf(0.0f, [](std::uint32_t, std::uint32_t) -> std::uint16_t { return 100; });
    REQUIRE(terrainShore(field, Hydrology{}, HydrologyParams{}, {0, 0}).empty());
    Hydrology wrong;
    wrong.side = 3;
    wrong.filled.assign(9, 0.0f);
    REQUIRE(terrainShore(field, wrong, HydrologyParams{}, {0, 0}).empty());
    const Hydrology hydrology = computeHydrology(field);
    REQUIRE(terrainShore(field, hydrology, HydrologyParams{}, {5, 5}).empty());
}

TEST_CASE("neighbouring cells agree on the shore along the edge they share")
{
    WorldDesc desc;
    desc.sizeMetres = kSide - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = (kSide - 1) / 2;
    desc.heightRange = 6553.5f;
    desc.waterHeight = 5.0f;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(kSide) * kSide);
    for (std::uint32_t z = 0; z < kSide; ++z) {
        for (std::uint32_t x = 0; x < kSide; ++x) {
            const std::uint32_t d = (x + 2 * z) / 3;
            samples[static_cast<std::size_t>(z) * kSide + x] = d < 20 ? 20 : static_cast<std::uint16_t>(50 + (d - 20) * 3);
        }
    }
    const Heightfield field(desc, std::move(samples));
    const Hydrology hydrology = computeHydrology(field);
    HydrologyParams rivers;
    rivers.riverThreshold = 1u << 30;
    const std::vector<glm::vec2> west = terrainShore(field, hydrology, rivers, {0, 0});
    const std::vector<glm::vec2> east = terrainShore(field, hydrology, rivers, {1, 0});
    const std::uint32_t side = desc.samplesPerCellSide();
    REQUIRE(west.size() == static_cast<std::size_t>(side) * side);
    float most = 0.0f;
    for (std::uint32_t z = 0; z < side; ++z) {
        const glm::vec2 a = west[static_cast<std::size_t>(z) * side + side - 1];
        const glm::vec2 b = east[static_cast<std::size_t>(z) * side];
        REQUIRE(a == b);
        most = a.x > most ? a.x : most;
    }
    REQUIRE(most > 0.0f);
}
