#include "codec_fuzz.h"
#include "hostile_text.h"

#include "ichor/assets/image.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ichor;
using ichor::fuzz::Bytes;
using ichor::fuzz::Canonical;

TEST_CASE("hostile: png bytes through decodeTexture", "[hostile][texture]")
{
    const std::filesystem::path dir = hostile::uniqueDir("texture");
    const std::filesystem::path file = dir / "sample.png";
    std::vector<std::uint8_t> pixels;
    for (int i = 0; i < 6 * 5; ++i) {
        pixels.insert(pixels.end(), {static_cast<std::uint8_t>(i * 8), static_cast<std::uint8_t>(255 - i * 8), static_cast<std::uint8_t>(i * 3), 255});
    }
    REQUIRE(writePng(file, 6, 5, pixels.data()));
    std::ifstream in(file, std::ios::binary);
    const Bytes valid((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    REQUIRE(valid.size() > 40);

    ichor::fuzz::Options options;
    options.allowNormalising = true;
    options.allowNonCanonical = true;
    options.window = 96;
    options.randomMutations = 1500;
    const auto report = ichor::fuzz::fuzzCodec(valid, [](std::span<const std::uint8_t> b) -> Canonical {
        Texture srgb;
        Texture unorm;
        const bool a = decodeTexture(b.data(), b.size(), TextureFormat::Rgba8Srgb, srgb);
        const bool c = decodeTexture(b.data(), b.size(), TextureFormat::Rgba8Unorm, unorm);
        if (a != c) return Bytes{'m', 'i', 's', 'm', 'a', 't', 'c', 'h'};
        if (!a) return std::nullopt;
        if (srgb.pixels.empty() || srgb.mipCount == 0) return Bytes{'e', 'm', 'p', 't', 'y'};
        return Bytes(b.begin(), b.end());
    }, options);
    INFO(ichor::fuzz::summary(report));
    CHECK(report.cases > 100);
    CHECK(report.rejected > 0);
    CHECK(report.ok());
}
