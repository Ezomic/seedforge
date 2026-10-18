#include <catch2/catch_test_macros.hpp>

#include <forgecli/gallery.h>
#include <forgecli/forge.h>

#include <algorithm>
#include <cstddef>

namespace {

forgecli::GalleryOptions options(std::uint64_t a, std::uint64_t b, int size = 64)
{
    forgecli::GalleryOptions o;
    o.seedBegin = a;
    o.seedEnd = b;
    o.size = size;
    return o;
}

std::size_t differingBytes(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b)
{
    REQUIRE(a.size() == b.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

}

TEST_CASE("the gallery is byte-identical across two runs", "[forge_cli][gallery]")
{
    const auto first = forgecli::makeGallery(options(1, 4));
    const auto second = forgecli::makeGallery(options(1, 4));
    REQUIRE(first.error.empty());
    REQUIRE(first.failedSeeds.empty());
    CHECK(first.png == second.png);
    CHECK(first.png.size() > 1000);
    CHECK(first.png[1] == 'P');
}

TEST_CASE("the gallery lays seeds out in a square-ish grid", "[forge_cli][gallery]")
{
    const auto one = forgecli::makeGallery(options(5, 5));
    const auto sixteen = forgecli::makeGallery(options(1, 16));
    const auto seventeen = forgecli::makeGallery(options(1, 17));
    REQUIRE(sixteen.error.empty());
    CHECK(sixteen.width == 2 + 4 * (64 + 2));
    CHECK(sixteen.height == 2 + 4 * (64 + 14 + 2));
    CHECK(seventeen.width == 2 + 5 * (64 + 2));
    CHECK(seventeen.height == 2 + 4 * (64 + 14 + 2));
    CHECK(one.width == 2 + 64 + 2);
}

TEST_CASE("the gallery changes when a phase output changes", "[forge_cli][gallery]")
{
    const auto base = forgecli::makeGallery(options(1, 4));

    auto flatter = options(1, 4);
    flatter.config = forgecli::defaultForgeConfig(64);
    flatter.config->erosion.iterations = 0;
    const auto uneroded = forgecli::makeGallery(flatter);

    auto dry = options(1, 4);
    dry.config = forgecli::defaultForgeConfig(64);
    dry.config->hydrology.riverThreshold = worldforge::kHydrologyMaxRiverThreshold;
    const auto noRivers = forgecli::makeGallery(dry);

    REQUIRE(uneroded.error.empty());
    REQUIRE(noRivers.error.empty());
    // Rivers are thin at this size, so dropping them moves tens of pixels; a gallery that ignored the river layer would differ by zero.
    CHECK(differingBytes(base.png, uneroded.png) > 500);
    CHECK(differingBytes(base.png, noRivers.png) > 100);
}

TEST_CASE("different seeds draw different cells", "[forge_cli][gallery]")
{
    const auto a = forgecli::makeGallery(options(1, 1));
    const auto b = forgecli::makeGallery(options(2, 2));
    CHECK(differingBytes(a.png, b.png) > 1000);
}

TEST_CASE("the gallery rejects hostile options", "[forge_cli][gallery]")
{
    CHECK_FALSE(forgecli::makeGallery(options(5, 4)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(0, forgecli::kMaxGallerySeeds)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(0, ~std::uint64_t{0})).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 2, 0)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 2, -5)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 2, 1 << 20)).error.empty());
    CHECK_FALSE(forgecli::makeGallery(options(1, 256, 1024)).error.empty());
    CHECK(forgecli::makeGallery(options(~std::uint64_t{0}, ~std::uint64_t{0})).error.empty());
}
