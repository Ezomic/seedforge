#include "ichor/world/map_image.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ichor;

namespace {

WorldDesc smallDesc()
{
    WorldDesc desc;
    desc.sizeMetres = 64;
    desc.metresPerSample = 4;
    desc.cellMetres = 32;
    desc.heightRange = 100.0f;
    return desc;
}

Heightfield ramp(WorldDesc desc)
{
    const std::uint32_t n = desc.samplesPerSide();
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            samples[static_cast<std::size_t>(z) * n + x] = static_cast<std::uint16_t>((x + z) * 65535u / (2 * (n - 1)));
        }
    }
    return Heightfield(desc, std::move(samples));
}

}

TEST_CASE("a map is as wide as the world has samples, and opaque")
{
    const MapImage map = renderMapImage(ramp(smallDesc()));
    REQUIRE_FALSE(map.empty());
    REQUIRE(map.width == 17);
    REQUIRE(map.height == 17);
    REQUIRE(map.rgba.size() == 17u * 17u * 4u);
    for (std::size_t i = 3; i < map.rgba.size(); i += 4) {
        REQUIRE(map.rgba[i] == 255);
    }
}

TEST_CASE("a big world is brought down to the map side, and the limit can be lowered")
{
    WorldDesc desc = smallDesc();
    desc.sizeMetres = 1024;
    desc.metresPerSample = 4;
    desc.cellMetres = 256;
    const Heightfield big = ramp(desc);
    const MapImage map = renderMapImage(big);
    REQUIRE(map.width == kMapSide);
    REQUIRE(map.height == kMapSide);
    REQUIRE(renderMapImage(big, 16).width == 16);
    REQUIRE(renderMapImage(big, 0).empty());
}

TEST_CASE("the same world gives the same bytes, and a different one does not")
{
    const MapImage a = renderMapImage(ramp(smallDesc()));
    const MapImage b = renderMapImage(ramp(smallDesc()));
    REQUIRE(a.rgba == b.rgba);
    WorldDesc other = smallDesc();
    other.heightRange = 300.0f;
    REQUIRE(renderMapImage(ramp(other)).rgba != a.rgba);
}

TEST_CASE("water below the water level is blue and land above it is not")
{
    WorldDesc desc = smallDesc();
    desc.waterHeight = 30.0f;
    const MapImage map = renderMapImage(ramp(desc));
    const auto pixel = [&](std::uint32_t x, std::uint32_t y) { return &map.rgba[(static_cast<std::size_t>(y) * map.width + x) * 4]; };
    const std::uint8_t* wet = pixel(0, 0);
    const std::uint8_t* dry = pixel(map.width - 1, map.height - 1);
    REQUIRE(wet[2] > wet[0]);
    REQUIRE(wet[2] > wet[1]);
    REQUIRE_FALSE(dry[2] > dry[0] + 20);
}

TEST_CASE("a world with no heightfield, or one that does not match its description, gives an empty map")
{
    REQUIRE(renderMapImage(Heightfield()).empty());
    REQUIRE(renderMapImage(Heightfield(smallDesc(), std::vector<std::uint16_t>(5))).empty());
    WorldDesc broken = smallDesc();
    broken.metresPerSample = 0;
    REQUIRE(renderMapImage(Heightfield(broken, std::vector<std::uint16_t>(1))).empty());
    WorldDesc nan = smallDesc();
    nan.heightRange = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(renderMapImage(ramp(nan)).empty());
    WorldDesc infinite = smallDesc();
    infinite.heightRange = std::numeric_limits<float>::infinity();
    REQUIRE(renderMapImage(ramp(infinite)).empty());
    WorldDesc lost = smallDesc();
    lost.minHeight = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(renderMapImage(ramp(lost)).empty());
}

TEST_CASE("the smallest worlds still make a picture")
{
    WorldDesc one;
    one.sizeMetres = 4;
    one.metresPerSample = 4;
    one.cellMetres = 4;
    const MapImage tiny = renderMapImage(Heightfield(one, std::vector<std::uint16_t>(4, 1000)));
    REQUIRE(tiny.width == 2);
    REQUIRE_FALSE(tiny.empty());

    WorldDesc huge;
    huge.sizeMetres = 8192;
    huge.metresPerSample = 1;
    huge.cellMetres = 512;
    REQUIRE(renderMapImage(Heightfield(huge, std::vector<std::uint16_t>(16))).empty());
}

TEST_CASE("a position maps to fractions of the picture, north at the top")
{
    const WorldDesc desc = smallDesc();
    REQUIRE(mapPosition(desc, 0.0, 0.0) == MapPoint{0.0f, 0.0f});
    REQUIRE(mapPosition(desc, 64.0, 64.0) == MapPoint{1.0f, 1.0f});
    REQUIRE(mapPosition(desc, 32.0, 16.0) == MapPoint{0.5f, 0.25f});
}

TEST_CASE("a position outside the world is held on the edge, and one that is not a number is in the middle")
{
    const WorldDesc desc = smallDesc();
    REQUIRE(mapPosition(desc, -500.0, 9000.0) == MapPoint{0.0f, 1.0f});
    const double inf = std::numeric_limits<double>::infinity();
    REQUIRE(mapPosition(desc, inf, -inf) == MapPoint{1.0f, 0.0f});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(mapPosition(desc, nan, 16.0) == MapPoint{0.5f, 0.25f});
    REQUIRE(mapPosition(desc, 16.0, nan) == MapPoint{0.25f, 0.5f});
    WorldDesc none = desc;
    none.sizeMetres = 0;
    REQUIRE(mapPosition(none, 10.0, 10.0) == MapPoint{0.5f, 0.5f});
}
