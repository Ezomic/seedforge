#include "ichor/assets/image.h"
#include "ichor/world/debug_images.h"
#include "ichor/world/forge.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID static_cast<unsigned long>(_getpid())
#else
#include <unistd.h>
#define ICHOR_PID static_cast<unsigned long>(getpid())
#endif

using namespace ichor;

namespace {

const std::filesystem::path kGoldenDir = std::filesystem::path(ICHOR_TEST_DATA) / "golden";
const std::filesystem::path kOutDir = std::filesystem::path(ICHOR_TEST_OUTPUT);

struct Verdict {
    bool ok = false;
    std::string message;
};

bool updating()
{
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    return update != nullptr && update[0] == '1';
}

// Compares a picture with golden/forge_<phase>.png. Only update mode writes there. A failed
// comparison leaves the actual picture and a diff (x8) in outDir under this process's id, so
// concurrent runs never share a file.
Verdict checkGolden(const std::string& phase, const MapImage& image, const std::filesystem::path& goldenDir, const std::filesystem::path& outDir, bool update,
    int maxChannelDelta)
{
    const std::filesystem::path golden = goldenDir / ("forge_" + phase + ".png");
    const std::string stem = "forge_" + phase + "." + std::to_string(ICHOR_PID);
    const std::string how = std::format("LOOK at {0}.actual.png and {0}.diff.png in {1} first; if the change is intended, regenerate with ICHOR_UPDATE_GOLDEN=1 and commit the new PNG",
        stem, outDir.string());
    if (image.empty()) {
        return {false, std::format("forge phase '{}' produced no picture", phase)};
    }
    if (update) {
        std::filesystem::create_directories(goldenDir);
        return {writePng(golden, image.width, image.height, image.rgba.data()), "golden written"};
    }

    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(golden, w, h, expected)) {
        return {false, std::format("forge phase '{}': no golden image at {}. Rerun with ICHOR_UPDATE_GOLDEN=1 to create it, and LOOK at it before committing", phase, golden.string())};
    }
    if (w != image.width || h != image.height || expected.size() != image.rgba.size()) {
        return {false, std::format("forge phase '{}': golden is {}x{} but the picture is {}x{}. {}", phase, w, h, image.width, image.height, how)};
    }

    std::vector<std::uint8_t> diff(image.rgba.size(), 255);
    std::size_t bad = 0;
    for (std::size_t p = 0; p < image.rgba.size() / 4; ++p) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            worst = std::max(worst, std::abs(static_cast<int>(image.rgba[p * 4 + c]) - static_cast<int>(expected[p * 4 + c])));
        }
        const auto v = static_cast<std::uint8_t>(std::min(255, worst * 8));
        diff[p * 4] = diff[p * 4 + 1] = diff[p * 4 + 2] = v;
        bad += worst > maxChannelDelta ? 1 : 0;
    }
    if (bad == 0) {
        return {true, {}};
    }
    std::filesystem::create_directories(outDir);
    writePng(outDir / (stem + ".actual.png"), image.width, image.height, image.rgba.data());
    writePng(outDir / (stem + ".diff.png"), image.width, image.height, diff.data());
    return {false, std::format("forge phase '{}' changed: {} of {} pixels differ. {}", phase, bad, image.rgba.size() / 4, how)};
}

struct Phases {
    MapImage height;
    MapImage flow;
    MapImage water;
};

WorldDesc worldDesc()
{
    WorldDesc desc;
    desc.seed = 7;
    desc.sizeMetres = 512;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

HydrologyParams waterParams()
{
    HydrologyParams params;
    params.riverThreshold = 40;
    return params;
}

const Phases& phases()
{
    static const Phases built = [] {
        const Heightfield field = forgeHeightfield(worldDesc());
        const Hydrology hydrology = computeHydrology(field);
        return Phases{renderHeightImage(field), renderFlowImage(hydrology), renderWaterImage(field, hydrology, waterParams())};
    }();
    return built;
}

void expectGolden(const std::string& phase, const MapImage& image, int maxChannelDelta)
{
    const Verdict verdict = checkGolden(phase, image, kGoldenDir, kOutDir, updating(), maxChannelDelta);
    INFO(verdict.message);
    CHECK(verdict.ok);
}

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name)
    {
        path = std::filesystem::temp_directory_path() / ("ichor-forge-goldens-" + name + "-" + std::to_string(ICHOR_PID));
        std::filesystem::remove_all(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

MapImage grey(std::uint32_t side, std::uint8_t value)
{
    MapImage image;
    image.width = image.height = side;
    image.rgba.assign(static_cast<std::size_t>(side) * side * 4, value);
    return image;
}

}

TEST_CASE("forge golden: the heightfield phase still draws the same land")
{
    expectGolden("height", phases().height, 0);
}

// The flow picture scales with std::log, which libm may round differently between platforms, so a
// channel may move by one level; a real change in the flow moves whole rivers.
TEST_CASE("forge golden: the hydrology flow phase still gathers the same rivers")
{
    expectGolden("flow", phases().flow, 1);
}

TEST_CASE("forge golden: the water phase still puts the same rivers and lakes on the land")
{
    expectGolden("water", phases().water, 0);
}

TEST_CASE("forge golden: the pictures are small and not blank")
{
    for (const MapImage* image : {&phases().height, &phases().flow, &phases().water}) {
        REQUIRE_FALSE(image->empty());
        CHECK(image->width <= 128);
        CHECK(std::adjacent_find(image->rgba.begin(), image->rgba.end(), [](auto a, auto b) { return a != b; }) != image->rgba.end());
    }
}

TEST_CASE("forge golden: a missing golden is a clear failure that names the regenerate step")
{
    const TempDir dir("missing");
    const Verdict verdict = checkGolden("height", grey(4, 10), dir.path / "golden", dir.path / "out", false, 0);
    CHECK_FALSE(verdict.ok);
    CHECK(verdict.message.find("ICHOR_UPDATE_GOLDEN=1") != std::string::npos);
    CHECK(verdict.message.find("'height'") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(dir.path / "golden"));
}

TEST_CASE("forge golden: update mode creates the directory, and only update mode writes the golden")
{
    const TempDir dir("update");
    const MapImage image = grey(4, 10);
    REQUIRE(checkGolden("water", image, dir.path / "golden", dir.path / "out", true, 0).ok);
    CHECK(std::filesystem::exists(dir.path / "golden" / "forge_water.png"));
    CHECK(checkGolden("water", image, dir.path / "golden", dir.path / "out", false, 0).ok);

    const auto before = std::filesystem::last_write_time(dir.path / "golden" / "forge_water.png");
    CHECK_FALSE(checkGolden("water", grey(4, 200), dir.path / "golden", dir.path / "out", false, 0).ok);
    CHECK(std::filesystem::last_write_time(dir.path / "golden" / "forge_water.png") == before);
    CHECK(checkGolden("water", image, dir.path / "golden", dir.path / "out", false, 0).ok);
}

TEST_CASE("forge golden: a golden of the wrong size fails and names both sizes")
{
    const TempDir dir("size");
    REQUIRE(checkGolden("flow", grey(4, 10), dir.path / "golden", dir.path / "out", true, 0).ok);
    const Verdict verdict = checkGolden("flow", grey(5, 10), dir.path / "golden", dir.path / "out", false, 0);
    CHECK_FALSE(verdict.ok);
    CHECK(verdict.message.find("4x4") != std::string::npos);
    CHECK(verdict.message.find("5x5") != std::string::npos);
}

TEST_CASE("forge golden: a changed picture fails, says to look at the diff, and the tolerance lets one level through")
{
    const TempDir dir("changed");
    REQUIRE(checkGolden("height", grey(4, 10), dir.path / "golden", dir.path / "out", true, 0).ok);
    const Verdict verdict = checkGolden("height", grey(4, 12), dir.path / "golden", dir.path / "out", false, 0);
    CHECK_FALSE(verdict.ok);
    CHECK(verdict.message.find("LOOK at") != std::string::npos);
    CHECK(verdict.message.find("ICHOR_UPDATE_GOLDEN=1") != std::string::npos);
    CHECK(std::filesystem::exists(dir.path / "out"));
    CHECK(checkGolden("height", grey(4, 11), dir.path / "golden", dir.path / "out", false, 1).ok);
    CHECK_FALSE(checkGolden("height", grey(4, 12), dir.path / "golden", dir.path / "out", false, 1).ok);
}
