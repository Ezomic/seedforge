#include "ichor/world/debug_images.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;

namespace {

WorldDesc smallDesc()
{
    WorldDesc desc;
    desc.sizeMetres = 8;
    desc.metresPerSample = 4;
    desc.cellMetres = 4;
    desc.heightRange = 100.0f;
    return desc;
}

Heightfield heights(std::vector<std::uint16_t> samples)
{
    return Heightfield(smallDesc(), std::move(samples));
}

Heightfield ramp()
{
    return heights({0, 0, 0, 0, 0, 0, 0, 0, 65535});
}

Hydrology hydrologyOf(std::vector<std::uint32_t> accumulation)
{
    Hydrology h;
    h.side = 3;
    h.accumulation = std::move(accumulation);
    h.filled.assign(9, 0.0f);
    h.lake.assign(9, 0);
    return h;
}

const std::uint8_t* pixel(const MapImage& image, std::uint32_t x, std::uint32_t y)
{
    return &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
}

}

TEST_CASE("the height picture has a pixel per sample, is opaque, and runs from black to white")
{
    const MapImage image = renderHeightImage(ramp());
    REQUIRE_FALSE(image.empty());
    REQUIRE(image.width == 3);
    REQUIRE(image.height == 3);
    CHECK(pixel(image, 0, 0)[0] == 0);
    CHECK(pixel(image, 2, 2)[0] == 255);
    CHECK(pixel(image, 2, 2)[2] == 255);
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
        CHECK(image.rgba[i] == 255);
    }
}

TEST_CASE("a level world is mid grey, not a division by zero")
{
    const MapImage image = renderHeightImage(heights(std::vector<std::uint16_t>(9, 777)));
    REQUIRE_FALSE(image.empty());
    CHECK(pixel(image, 1, 1)[0] == 128);
    const MapImage water = renderWaterImage(heights(std::vector<std::uint16_t>(9, 777)), hydrologyOf(std::vector<std::uint32_t>(9, 1)), HydrologyParams{});
    REQUIRE_FALSE(water.empty());
    CHECK(pixel(water, 1, 1)[0] == 90);
}

TEST_CASE("a picture is shrunk to the cap, and a block shows the biggest river in it")
{
    std::vector<std::uint32_t> accumulation(9, 1);
    accumulation[0] = 100;
    const MapImage one = renderFlowImage(hydrologyOf(accumulation), 1);
    REQUIRE(one.width == 1);
    CHECK(pixel(one, 0, 0)[2] == 255);

    accumulation[8] = 10;
    const MapImage two = renderFlowImage(hydrologyOf(accumulation), 2);
    REQUIRE(two.width == 2);
    CHECK(pixel(two, 0, 0)[2] == 255);
    CHECK(pixel(two, 1, 0)[2] == 38);
    CHECK(pixel(two, 1, 1)[2] == 147);
}

TEST_CASE("a shrunk height picture shows the mean of each block")
{
    const MapImage image = renderHeightImage(ramp(), 2);
    REQUIRE(image.width == 2);
    CHECK(pixel(image, 0, 0)[0] == 0);
    CHECK(pixel(image, 1, 1)[0] == 64);
}

TEST_CASE("a shrunk water picture shows a lake over a river in the same block")
{
    Hydrology hydrology = hydrologyOf(std::vector<std::uint32_t>(9, 1));
    hydrology.accumulation[4] = 5000;
    hydrology.lake[8] = 1;
    hydrology.filled[8] = 150.0f;
    hydrology.lakeDeepest = {5.0f};
    const MapImage image = renderWaterImage(ramp(), hydrology, HydrologyParams{}, 2);
    REQUIRE(image.width == 2);
    CHECK(pixel(image, 1, 1)[2] == 168);
    CHECK(pixel(image, 0, 0)[2] != 168);
}

TEST_CASE("flow is black-blue where nothing gathers and white at the biggest river, on a log scale")
{
    const MapImage image = renderFlowImage(hydrologyOf({1, 1, 1, 1, 10, 1, 1, 1, 100}));
    REQUIRE_FALSE(image.empty());
    CHECK(pixel(image, 0, 0)[0] == 0);
    CHECK(pixel(image, 0, 0)[2] == 38);
    CHECK(pixel(image, 2, 2)[0] == 255);
    CHECK(pixel(image, 2, 2)[2] == 255);
    const std::uint8_t middle = pixel(image, 1, 1)[2];
    CHECK(middle == 147); // 10 of 100 is halfway on a log scale, which a linear scale would put near the bottom
}

TEST_CASE("a flow with no gathering at all draws without a divide by zero")
{
    const MapImage image = renderFlowImage(hydrologyOf(std::vector<std::uint32_t>(9, 1)));
    REQUIRE_FALSE(image.empty());
    CHECK(pixel(image, 1, 1)[0] == 0);
    CHECK(renderFlowImage(hydrologyOf(std::vector<std::uint32_t>(9, 0))).empty() == false);
}

TEST_CASE("the water picture draws rivers and lakes over grey ground")
{
    Hydrology hydrology = hydrologyOf({1, 1, 1, 1, 1, 1, 1, 1, 1});
    hydrology.accumulation[0] = 5000;
    hydrology.lake[8] = 1;
    hydrology.filled[8] = 150.0f;
    hydrology.lakeDeepest = {5.0f};
    HydrologyParams params;
    params.riverThreshold = 2000;
    const MapImage image = renderWaterImage(ramp(), hydrology, params);
    REQUIRE_FALSE(image.empty());
    CHECK(pixel(image, 0, 0)[2] == 235);
    CHECK(pixel(image, 2, 2)[2] == 168);
    CHECK(pixel(image, 1, 1)[0] == pixel(image, 1, 1)[2]);
    CHECK(pixel(image, 1, 1)[0] < 100);
}

TEST_CASE("the same inputs give the same bytes")
{
    CHECK(renderHeightImage(ramp()).rgba == renderHeightImage(ramp()).rgba);
}

TEST_CASE("empty, mismatched and zero-size inputs give an empty picture")
{
    CHECK(renderHeightImage(Heightfield{}).empty());
    CHECK(renderHeightImage(ramp(), 0).empty());
    CHECK(renderFlowImage(Hydrology{}).empty());
    CHECK(renderWaterImage(ramp(), Hydrology{}, HydrologyParams{}).empty());
    CHECK(renderWaterImage(Heightfield{}, hydrologyOf(std::vector<std::uint32_t>(9, 1)), HydrologyParams{}).empty());
    Hydrology wrong = hydrologyOf(std::vector<std::uint32_t>(9, 1));
    wrong.side = 2;
    CHECK(renderFlowImage(wrong).empty());
    CHECK(renderWaterImage(ramp(), wrong, HydrologyParams{}).empty());
    WorldDesc nan = smallDesc();
    nan.heightRange = std::numeric_limits<float>::quiet_NaN();
    CHECK(renderHeightImage(Heightfield(nan, std::vector<std::uint16_t>(9, 1))).empty());
}
