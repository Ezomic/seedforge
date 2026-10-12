#include "ichor/assets/bundle_io.h"
#include "ichor/assets/image.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/gfx/vk_offscreen.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>

using namespace ichor;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 360;

const std::filesystem::path kGoldenDir = std::filesystem::path(ICHOR_TEST_DATA) / "golden";
const std::filesystem::path kContent = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
const std::filesystem::path kOutDir = std::filesystem::path(ICHOR_TEST_OUTPUT);

struct Diff {
    double meanAbs = 0.0;
    double fractionBad = 0.0; // pixels with any channel off by more than the threshold
    std::vector<std::uint8_t> image;
};

Diff compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int threshold)
{
    Diff diff;
    diff.image.resize(a.size());
    std::uint64_t sum = 0;
    std::uint64_t bad = 0;
    const std::size_t pixels = a.size() / 4;
    for (std::size_t p = 0; p < pixels; ++p) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(static_cast<int>(a[p * 4 + c]) - static_cast<int>(b[p * 4 + c]));
            sum += static_cast<std::uint64_t>(d);
            worst = std::max(worst, d);
        }
        bad += worst > threshold ? 1 : 0;
        const auto v = static_cast<std::uint8_t>(std::min(255, worst * 8));
        diff.image[p * 4] = v;
        diff.image[p * 4 + 1] = v;
        diff.image[p * 4 + 2] = v;
        diff.image[p * 4 + 3] = 255;
    }
    diff.meanAbs = static_cast<double>(sum) / static_cast<double>(pixels * 3);
    diff.fractionBad = static_cast<double>(bad) / static_cast<double>(pixels);
    return diff;
}

}

TEST_CASE("golden image: the hoplite kit")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        SKIP("no Vulkan 1.3 device available");
    }
    INFO("device: " << renderer.deviceName());

    SceneBundle bundle;
    REQUIRE(readBundle(kContent, bundle));
    REQUIRE(renderer.uploadBundle(bundle));

    const RenderView view = kitReferenceView(bundle, static_cast<float>(kWidth) / kHeight);
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    REQUIRE(actual.size() == static_cast<std::size_t>(kWidth) * kHeight * 4);

    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "kit.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = kGoldenDir / "kit.png";
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        std::filesystem::create_directories(kGoldenDir);
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }

    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(goldenPath, w, h, expected)) {
        FAIL("no golden image at " << goldenPath.string() << "; inspect kit.actual.png and rerun with ICHOR_UPDATE_GOLDEN=1");
    }
    REQUIRE(w == kWidth);
    REQUIRE(h == kHeight);

    // Driver differences move a few edge pixels; a real regression moves many.
    const Diff diff = compare(actual, expected, 16);
    writePng(kOutDir / "kit.diff.png", kWidth, kHeight, diff.image.data());
    INFO(std::format("mean abs diff {:.3f}, {:.4f}% pixels over threshold", diff.meanAbs, diff.fractionBad * 100.0));
    CHECK(diff.meanAbs < 1.5);
    CHECK(diff.fractionBad < 0.005);
}
