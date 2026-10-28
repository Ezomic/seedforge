#include "ichor/world/hydrology.h"
#include "ichor/world/water_mesh.h"
#include "ichor/world/palette.h"
#include "ichor/anim/animation.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/image.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/gfx/renderer.h"
#include "ichor/gfx/vk_offscreen.h"
#include "ichor/gfx/vulkan_ui.h"
#include "ichor/ui/hud.h"
#include "ichor/ui/pause_menu.h"
#include "ichor/ui/ledger.h"
#include "ichor/ui/journal_screen.h"
#include "ichor/ui/map_screen.h"
#include "ichor/ui/ui_context.h"
#include "ichor/world/atmosphere.h"
#include "ichor/world/vegetation.h"
#include "ichor/world/weather.h"
#include "ichor/world/streamer.h"
#include "ichor/world/world_io.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>

#include <RmlUi/Core/ElementDocument.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>

using namespace ichor;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 360;

const std::filesystem::path kGoldenDir = std::filesystem::path(ICHOR_TEST_DATA) / "golden";
const std::filesystem::path kContent = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
const std::filesystem::path kArmContent = std::filesystem::path(ICHOR_CONTENT_DIR) / "arm.iscene";
const std::filesystem::path kOutDir = std::filesystem::path(ICHOR_TEST_OUTPUT);

bool envFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] == '1';
}

// CI runs these on a software Vulkan device (lavapipe). A case a real GPU draws differently has its own picture under
// golden/software, and the rest compare against the GPU golden with the wider software tolerance.
bool softwareDevice()
{
    return envFlag("ICHOR_GOLDEN_SOFTWARE");
}

std::filesystem::path goldenFile(const std::string& name)
{
    if (softwareDevice()) {
        const std::filesystem::path software = kGoldenDir / "software" / name;
        if (envFlag("ICHOR_UPDATE_GOLDEN")) {
            std::filesystem::create_directories(software.parent_path());
            return software;
        }
        if (std::filesystem::exists(software)) {
            return software;
        }
    }
    return kGoldenDir / name;
}

// Without this a runner that lost its Vulkan device would report every case as skipped, which is the gap SEED-471 closed.
#define NO_VULKAN_DEVICE() \
    do { \
        if (envFlag("ICHOR_REQUIRE_GPU")) { \
            FAIL("no Vulkan 1.3 device available, and ICHOR_REQUIRE_GPU=1"); \
        } \
        SKIP("no Vulkan 1.3 device available"); \
    } while (false)

struct Tolerance {
    double meanAbs;
    double fractionBad;
    std::uint32_t tileBad;
};

// A tile is 16x16 pixels. Driver noise spreads thinly along edges; a missing draw call piles up in a few tiles, so the
// tile count catches a small missing element that the whole-picture fraction cannot see.
constexpr int kTile = 16;
constexpr int kBadThreshold = 16;

struct Diff {
    double meanAbs = 0.0;
    double fractionBad = 0.0; // pixels with any channel off by more than the threshold
    int maxChannelDiff = 0;
    std::uint32_t tileBad = 0; // the most over-threshold pixels in any one tile
    std::vector<std::uint8_t> image;
};

Diff compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int threshold, std::uint32_t width = kWidth)
{
    Diff diff;
    diff.image.resize(a.size());
    std::uint64_t sum = 0;
    std::uint64_t bad = 0;
    const std::size_t pixels = a.size() / 4;
    const std::uint32_t height = static_cast<std::uint32_t>(pixels / width);
    const std::uint32_t tilesX = (width + kTile - 1) / kTile;
    const std::uint32_t tilesY = (height + kTile - 1) / kTile;
    std::vector<std::uint32_t> tileCounts(static_cast<std::size_t>(tilesX) * tilesY, 0);
    for (std::size_t p = 0; p < pixels; ++p) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(static_cast<int>(a[p * 4 + c]) - static_cast<int>(b[p * 4 + c]));
            sum += static_cast<std::uint64_t>(d);
            worst = std::max(worst, d);
        }
        diff.maxChannelDiff = std::max(diff.maxChannelDiff, worst);
        if (worst > threshold) {
            ++bad;
            const std::size_t tile = (p / width / kTile) * tilesX + (p % width) / kTile;
            diff.tileBad = std::max(diff.tileBad, ++tileCounts[tile]);
        }
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

Tolerance toleranceFor(bool software)
{
    if (software) {
        return {2.0, 0.002, 40};
    }
    return {1.5, 0.005, 256};
}

bool within(const Diff& diff, const Tolerance& limit)
{
    return diff.meanAbs < limit.meanAbs && diff.fractionBad < limit.fractionBad && diff.tileBad < limit.tileBad;
}

struct Verdict {
    bool pass = false;
    std::string message;
    std::optional<Diff> diff; // absent when there is no golden or its size differs
};

// A failed case leaves NAME.expected.png, NAME.actual.png, NAME.diff.png and NAME.txt in failDir, which CI turns into one
// page (tools/golden_diff_page.py) and uploads only when the job fails. A pass clears that case's leftovers from a rerun.
Verdict judgeGolden(const std::vector<std::uint8_t>& actual, const std::filesystem::path& goldenPath, const Tolerance& limit, const std::filesystem::path& failDir)
{
    const std::string name = goldenPath.stem().string();
    Verdict verdict;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(goldenPath, w, h, expected)) {
        verdict.message = std::format("no golden image at {}; inspect golden-failures/{}.actual.png and rerun with ICHOR_UPDATE_GOLDEN=1", goldenPath.string(), name);
    } else if (w != kWidth || h != kHeight) {
        verdict.message = std::format("{}: the golden is {}x{}, the render is {}x{}", name, w, h, kWidth, kHeight);
    } else {
        verdict.diff = compare(actual, expected, kBadThreshold);
        verdict.pass = within(*verdict.diff, limit);
        verdict.message = std::format("{}: mean abs diff {:.3f}, {:.4f}% pixels over threshold, worst tile {}, worst channel {}", name, verdict.diff->meanAbs, verdict.diff->fractionBad * 100.0, verdict.diff->tileBad, verdict.diff->maxChannelDiff);
    }
    std::error_code ignored;
    for (const char* suffix : {".expected.png", ".actual.png", ".diff.png", ".txt"}) {
        std::filesystem::remove(failDir / (name + suffix), ignored);
    }
    if (verdict.pass) {
        return verdict;
    }
    std::filesystem::create_directories(failDir);
    writePng(failDir / (name + ".actual.png"), kWidth, kHeight, actual.data());
    if (std::filesystem::exists(goldenPath)) {
        std::filesystem::copy_file(goldenPath, failDir / (name + ".expected.png"), std::filesystem::copy_options::overwrite_existing, ignored);
    }
    if (verdict.diff) {
        writePng(failDir / (name + ".diff.png"), kWidth, kHeight, verdict.diff->image.data());
    }
    std::ofstream(failDir / (name + ".txt")) << verdict.message << "\n";
    return verdict;
}

// Every case ends here: the numbers go to golden-metrics.csv when ICHOR_GOLDEN_REPORT=1 (the tolerance study), then
// the picture must sit inside the tolerance of the device it was drawn on.
void checkAgainstGolden(const std::vector<std::uint8_t>& actual, const std::filesystem::path& goldenPath)
{
    const std::string name = goldenPath.stem().string();
    // TEMP SEED-704 CI proof, reverted before review: on lavapipe, kit looks for a missing golden, ui_pause is compared
    // with the kit golden, and sky_noon gets zero tolerance.
    std::filesystem::path tempPath = goldenPath;
    Tolerance tempLimit = toleranceFor(softwareDevice());
    if (softwareDevice() && name == "kit") {
        tempPath = goldenPath.parent_path() / "kit missing on purpose.png";
    } else if (softwareDevice() && name == "ui_pause") {
        tempPath = kGoldenDir / "kit.png";
    } else if (softwareDevice() && name == "sky_noon") {
        tempLimit = {0.0, 0.0, 0};
    }
    const Verdict verdict = judgeGolden(actual, tempPath, tempLimit, kOutDir / "golden-failures");
    if (verdict.diff) {
        const Diff& diff = *verdict.diff;
        writePng(kOutDir / (name + ".diff.png"), kWidth, kHeight, diff.image.data());
        if (envFlag("ICHOR_GOLDEN_REPORT")) {
            std::ofstream csv(kOutDir / "golden-metrics.csv", std::ios::app);
            csv << name << ',' << diff.meanAbs << ',' << diff.fractionBad << ',' << diff.maxChannelDiff << ',' << diff.tileBad << "\n";
        }
    }
    if (!verdict.pass) {
        FAIL(verdict.message);
    }
}

}

TEST_CASE("golden image: the hoplite kit")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
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

    const std::filesystem::path goldenPath = goldenFile("kit.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        std::filesystem::create_directories(kGoldenDir);
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }

    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the skinned arm mid-wave")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    SceneBundle bundle;
    REQUIRE(readBundle(kArmContent, bundle));
    REQUIRE(renderer.uploadBundle(bundle));
    AnimationSet animations;
    REQUIRE(animations.load(bundle));

    RenderView view;
    view.sunDirection = glm::normalize(glm::vec3(-0.5f, 0.9f, 0.35f));
    const glm::dvec3 eye(1.6, 1.1, 1.9);
    const glm::dvec3 target(0.0, 0.5, 0.0);
    view.camera.position = eye;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
    view.camera.fovY = glm::radians(45.0f);
    RenderInstance instance;
    instance.mesh = 0;
    instance.jointOffset = 0;
    REQUIRE(animations.skinningMatrices(0, 0.26f, view.jointPalette));
    instance.jointCount = static_cast<std::uint32_t>(view.jointPalette.size());
    view.instances.push_back(instance);
    for (int i = -2; i <= 2; ++i) {
        view.lines.push_back({glm::dvec3(i, 0.0, -2.0), glm::dvec3(i, 0.0, 2.0), glm::vec3(0.35f)});
        view.lines.push_back({glm::dvec3(-2.0, 0.0, i), glm::dvec3(2.0, 0.0, i), glm::vec3(0.35f)});
    }

    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "arm.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("arm.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the forged terrain at the world's centre")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    const std::filesystem::path worldDir = std::filesystem::path(ICHOR_CONTENT_DIR) / "world";
    Heightfield heightfield;
    REQUIRE(readWorld(worldDir, heightfield));
    SceneBundle layers;
    REQUIRE(readBundle(worldDir / "terrain.iscene", layers));
    REQUIRE(renderer.setTerrainMaterials(layers));

    // The nine cells around the centre, the near one at full detail.
    RenderView view;
    const double centre = heightfield.desc().sizeMetres * 0.5;
    const CellId home = heightfield.cellAt(centre, centre);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const CellId id{home.x + dx, home.z + dz};
            CellPayload payload;
            WorldStreamer::buildPayload(heightfield, id, StreamerConfig{}, payload);
            const std::uint32_t handle = renderer.uploadTerrainCell(payload.lods);
            REQUIRE(handle != Renderer::kNoHandle);
            view.terrain.push_back({handle, {heightfield.cellOriginX(id), 0.0, heightfield.cellOriginZ(id)}, (dx == 0 && dz == 0) ? 0u : 1u});
        }
    }
    view.sunDirection = glm::normalize(glm::vec3(-0.5f, 0.6f, 0.35f));
    const glm::dvec3 eye(centre - 60.0, heightfield.heightAt(centre - 60.0, centre + 90.0) + 25.0, centre + 90.0);
    const glm::dvec3 target(centre + 120.0, heightfield.heightAt(centre + 120.0, centre - 200.0), centre - 200.0);
    view.camera.position = eye;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
    view.camera.farPlane = 2500.0f;

    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "terrain.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("terrain.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

namespace {

constexpr const char* kUiDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 640px; height: 360px; }
    .panel { position: absolute; left: 40px; top: 40px; width: 300px; height: 200px; background-color: #203050; border: 4px #8090c0; }
    .bar { position: absolute; left: 60px; top: 60px; width: 200px; height: 24px; background-color: #e0a020; }
    .glass { position: absolute; left: 200px; top: 120px; width: 260px; height: 160px; background-color: #ff208080; }
    .clip { position: absolute; left: 380px; top: 40px; width: 120px; height: 90px; overflow: hidden; background-color: #204020; }
    .big { width: 300px; height: 300px; background-color: #40e040; }
    .round { position: absolute; left: 500px; top: 200px; width: 100px; height: 100px; background-color: #f0f0f0; border-radius: 24px; }
    .pic { position: absolute; left: 60px; top: 270px; width: 64px; height: 64px; }
</style></head><body>
    <div class="panel"></div>
    <div class="bar"></div>
    <div class="glass"></div>
    <div class="clip"><div class="big"></div></div>
    <div class="round"></div>
    <img class="pic" src="ui_texture.png"/>
</body></rml>)";

}

TEST_CASE("golden image: the game UI drawn with Vulkan")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    // A small translucent checker, loaded through the texture path: PNG decode, premultiply, upload.
    std::filesystem::create_directories(kOutDir);
    std::vector<std::uint8_t> checker;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const bool light = (x + y) % 2 == 0;
            checker.insert(checker.end(), {static_cast<std::uint8_t>(light ? 240 : 40), static_cast<std::uint8_t>(light ? 200 : 60), static_cast<std::uint8_t>(light ? 40 : 200), static_cast<std::uint8_t>(light ? 255 : 160)});
        }
    }
    REQUIRE(writePng(kOutDir / "ui_texture.png", 4, 4, checker.data()));
    Rml::ElementDocument* document = ui.loadDocumentFromMemory(kUiDocument, std::filesystem::relative(kOutDir / "ui_document.rml").generic_string()); // RmlUi reads a drive letter as a URL scheme
    REQUIRE(document != nullptr);
    document->Show();
    ui.update(0.0);

    RenderView view;
    view.clearColor = glm::vec3(0.12f, 0.14f, 0.18f);
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    REQUIRE(vkUi.liveGeometry() > 0);

    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui.actual.png", kWidth, kHeight, actual.data()));
    const std::filesystem::path goldenPath = goldenFile("ui.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

namespace {

// The terrain at the world's centre under the sky of a given hour, looking
// towards where the sun sets, so the dusk glow and the fog are in view.
void checkTerrainAtHour(float hour, const char* name, bool vegetation = false, Weather weather = {}, bool water = false, bool submerged = false, bool graded = false, float cloudCover = -1.0f, float wetness = 0.0f)
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    const std::filesystem::path worldDir = std::filesystem::path(ICHOR_CONTENT_DIR) / "world";
    Heightfield heightfield;
    REQUIRE(readWorld(worldDir, heightfield));
    SceneBundle layers;
    REQUIRE(readBundle(worldDir / "terrain.iscene", layers));
    REQUIRE(renderer.setTerrainMaterials(layers));

    RenderView view;
    if (vegetation) {
        REQUIRE(renderer.setVegetationMeshes(treeBundle(), vegetationUnblendedKinds()));
    }
    const double centre = heightfield.desc().sizeMetres * 0.5;
    const CellId home = heightfield.cellAt(centre, centre);
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            const CellId id{home.x + dx, home.z + dz};
            if (!heightfield.contains(id)) continue;
            CellPayload payload;
            WorldStreamer::buildPayload(heightfield, id, StreamerConfig{}, payload);
            const std::uint32_t handle = renderer.uploadTerrainCell(payload.lods);
            REQUIRE(handle != Renderer::kNoHandle);
            const int ring = std::max(std::abs(dx), std::abs(dz));
            view.terrain.push_back({handle, {heightfield.cellOriginX(id), 0.0, heightfield.cellOriginZ(id)}, static_cast<std::uint32_t>(std::min(ring, 2))});
            if (vegetation) {
                std::vector<PlantInstance> plants;
                scatterVegetation(heightfield, id, VegetationParams{}, plants);
                const std::uint32_t grove = renderer.uploadVegetationCell(plants);
                if (grove != Renderer::kNoHandle) {
                    view.vegetation.push_back({grove, {heightfield.cellOriginX(id), 0.0, heightfield.cellOriginZ(id)}});
                }
            }
        }
    }
    Atmosphere atmosphere = atmosphereAt(hour);
    applyWeather(weather, atmosphere);
    applyAtmosphere(atmosphere, view);
    view.wetness = wetness;
    if (cloudCover >= 0.0f) {
        view.cloudCover = cloudCover;
    }
    view.time = 1.25f;
    if (graded) {
        applyPalette(defaultPalette(), view);
    }
    const glm::dvec3 eye(centre - 60.0, heightfield.heightAt(centre - 60.0, centre + 90.0) + 25.0, centre + 90.0);
    if (water) {
        // Flood the hollows: a few metres over the lowest ground along the line of sight.
        float lowest = 1e30f;
        for (int step = 1; step <= 45; ++step) {
            const double t = step / 45.0;
            lowest = std::min(lowest, heightfield.heightAt(centre - 60.0 - 840.0 * t, centre + 90.0 - 190.0 * t));
        }
        view.waterEnabled = true;
        view.waterLevel = submerged ? static_cast<float>(eye.y) + 4.0f : std::min(lowest + 22.0f, static_cast<float>(eye.y) - 8.0f);
    }
    const glm::dvec3 west(centre - 900.0, heightfield.heightAt(centre - 900.0, centre - 100.0) + 30.0, centre - 100.0);
    view.camera.position = eye;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(west - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
    view.camera.farPlane = 3000.0f;

    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / (std::string(name) + ".actual.png"), kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile(std::string(name) + ".png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

}

TEST_CASE("golden image: the terrain at noon")
{
    checkTerrainAtHour(12.0f, "terrain_noon");
}

TEST_CASE("golden image: the terrain at dusk")
{
    checkTerrainAtHour(17.7f, "terrain_dusk");
}

TEST_CASE("golden image: the terrain at night")
{
    checkTerrainAtHour(1.0f, "terrain_night");
}

TEST_CASE("golden image: the forested terrain")
{
    checkTerrainAtHour(15.0f, "terrain_forest", true);
}

TEST_CASE("golden image: an overcast day")
{
    checkTerrainAtHour(15.0f, "terrain_overcast", true, {0.8f, 0.0f});
}

TEST_CASE("golden image: cloud shadows drifting over the land")
{
    checkTerrainAtHour(13.0f, "terrain_cloud_shadows", true, {}, false, false, false, 0.65f);
}

TEST_CASE("golden image: a downpour")
{
    checkTerrainAtHour(15.0f, "terrain_rain", true, {1.0f, 1.0f});
}

TEST_CASE("golden image: soaked ground under cloud")
{
    checkTerrainAtHour(15.0f, "terrain_wet", true, {0.8f, 0.0f}, false, false, false, -1.0f, 1.0f);
}

TEST_CASE("golden image: flooded hollows")
{
    checkTerrainAtHour(15.0f, "terrain_water", true, {}, true);
}

TEST_CASE("golden image: under the water")
{
    checkTerrainAtHour(15.0f, "terrain_underwater", true, {}, true, true);
}

namespace {

constexpr const char* kTextDocument = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 640px; height: 360px; }
    .card { position: absolute; left: 40px; top: 40px; width: 560px; height: 280px; background-color: #1a1f2acc; border: 3px #a08850; }
    .title { position: absolute; left: 30px; top: 24px; font-family: Cinzel; font-size: 40px; color: #e8d8a8; letter-spacing: 3px; }
    .body { position: absolute; left: 30px; top: 100px; width: 500px; font-family: Alegreya Sans; font-size: 22px; color: #d8d8d8; line-height: 28px; }
    .strong { position: absolute; left: 30px; top: 200px; font-family: Alegreya Sans; font-weight: bold; font-size: 24px; color: #f0b040; }
</style></head><body>
    <div class="card">
        <div class="title">THE LEDGER</div>
        <div class="body">The ridge road holds three open debts. Carry the ash to the ferryman before the river rises.</div>
        <div class="strong">Hecatomb: health is the offering</div>
    </div>
</body></rml>)";

}

TEST_CASE("golden image: UI text in the game fonts")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Bold.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    std::filesystem::create_directories(kOutDir);
    Rml::ElementDocument* document = ui.loadDocumentFromMemory(kTextDocument, "[text document]");
    REQUIRE(document != nullptr);
    document->Show();
    ui.update(0.0);

    RenderView view;
    view.clearColor = glm::vec3(0.12f, 0.14f, 0.18f);
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    REQUIRE(writePng(kOutDir / "ui_text.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_text.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the HUD over the sky")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Bold.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Hud hud(ui);
    REQUIRE(hud.ok());
    hud.update(0.8f, 17.5f, 0.9f, 70.0f, 100.0f);
    ui.update(0.0);

    RenderView view;
    applyAtmosphere(atmosphereAt(17.5f), view);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, 0.15f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui_hud.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_hud.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the HUD pointing at a target")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Bold.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Hud hud(ui);
    REQUIRE(hud.ok());
    hud.update(0.8f, 17.5f, 0.9f, 70.0f, 100.0f);
    hud.setGuide(targetGuide(0.0, 0.0, 300.0, -300.0));
    ui.update(0.0);

    RenderView view;
    applyAtmosphere(atmosphereAt(17.5f), view);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, 0.15f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui_hud_target.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_hud_target.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the HUD with only the compass and health")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Bold.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    HudLayout layout;
    layout.slots = {{HudElement::Compass, true}, {HudElement::Health, true}};
    Hud hud(ui, layout);
    REQUIRE(hud.ok());
    hud.update(0.8f, 17.5f, 0.9f, 70.0f, 100.0f);
    ui.update(0.0);

    RenderView view;
    applyAtmosphere(atmosphereAt(17.5f), view);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, 0.15f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui_hud_compass_health.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_hud_compass_health.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the ledger over the world")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Ledger ledger(ui);
    REQUIRE(ledger.ok());
    ledger.setRows({
        {"1:12:40", "death", "Fell to the skirmisher on the ridge road."},
        {"1:04:02", "quest", "Carried the ash to the ferryman before the river rose."},
        {"0:51:30", "discovery", "Found the drowned shrine below the weir."},
        {"0:20:11", "note", "<b>Markup</b> & ampersands stay plain text."},
        {"0:00:00", "start", "The road begins."},
    });
    ledger.setVisible(true);
    ui.update(0.0);

    RenderView view;
    applyAtmosphere(atmosphereAt(17.5f), view);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, 0.15f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui_ledger.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_ledger.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the map over the world")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    Heightfield heightfield;
    REQUIRE(readWorld(std::filesystem::path(ICHOR_CONTENT_DIR) / "world", heightfield));
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    MapScreen map(ui);
    REQUIRE(map.ok());
    map.setImage(renderMapImage(heightfield));
    map.setPlayer(mapPosition(heightfield.desc(), heightfield.desc().sizeMetres * 0.4, heightfield.desc().sizeMetres * 0.6), 0.8f);
    map.setVisible(true);
    ui.update(0.0);

    RenderView view;
    applyAtmosphere(atmosphereAt(17.5f), view);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, 0.15f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui_map.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_map.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the default palette over the forest")
{
    checkTerrainAtHour(15.0f, "terrain_graded", true, {}, false, false, true);
}

TEST_CASE("golden image: a prop that opts in blends into the terrain, one that does not stands on it")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    const std::filesystem::path worldDir = std::filesystem::path(ICHOR_CONTENT_DIR) / "world";
    Heightfield heightfield;
    REQUIRE(readWorld(worldDir, heightfield));
    SceneBundle layers;
    REQUIRE(readBundle(worldDir / "terrain.iscene", layers));
    REQUIRE(renderer.setTerrainMaterials(layers));

    // The trees' meshes, used here as ordinary props: they have their origin at the foot.
    const std::uint32_t treeMesh = 0; // the first bundle uploaded: terrain keeps its own meshes
    REQUIRE(renderer.uploadBundle(treeBundle()));

    RenderView view;
    const double centre = heightfield.desc().sizeMetres * 0.5;
    const CellId home = heightfield.cellAt(centre, centre);
    CellPayload payload;
    WorldStreamer::buildPayload(heightfield, home, StreamerConfig{}, payload);
    const std::uint32_t handle = renderer.uploadTerrainCell(payload.lods);
    REQUIRE(handle != Renderer::kNoHandle);
    view.terrain.push_back({handle, {heightfield.cellOriginX(home), 0.0, heightfield.cellOriginZ(home)}, 0u});

    const double x = centre + 40.0;
    const double z = centre + 40.0;
    const glm::dvec3 eye(x - 3.0, heightfield.heightAt(x - 3.0, z + 8.0) + 3.0, z + 8.0);
    for (int i = 0; i < 2; ++i) {
        const double px = x + (i == 0 ? -2.5 : 2.5) + 1.0;
        RenderInstance prop;
        prop.mesh = treeMesh; // the conifer
        prop.position = {px, heightfield.heightAt(px, z), z};
        prop.basis = glm::mat3(1.5f);
        prop.groundBlend = i == 0;
        view.instances.push_back(prop);
    }
    Atmosphere atmosphere = atmosphereAt(15.0f);
    applyAtmosphere(atmosphere, view);
    applyPalette(defaultPalette(), view);
    view.camera.position = eye;
    const glm::dvec3 target(x + 1.0, heightfield.heightAt(x + 1.0, z) + 1.0, z);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
    view.camera.farPlane = 800.0f;

    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "props_grounded.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("props_grounded.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

// The biggest river of the forged world seen from `eyeOffset` metres (x, z) from its wettest sample and
// `eyeHeight` over the ground there, looking at that sample.
static void checkRiver(const char* name, glm::dvec2 eyeOffset, double eyeHeight)
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    const std::filesystem::path worldDir = std::filesystem::path(ICHOR_CONTENT_DIR) / "world";
    Heightfield heightfield;
    REQUIRE(readWorld(worldDir, heightfield));
    SceneBundle layers;
    REQUIRE(readBundle(worldDir / "terrain.iscene", layers));
    REQUIRE(renderer.setTerrainMaterials(layers));

    // The wettest dry-land sample not at the map's edge: where the most water gathers into a river.
    HydrologyParams rivers;
    rivers.maxLakeDepth = kPlayableLakeDepth;
    const Hydrology hydrology = computeHydrology(heightfield);
    const std::uint32_t side = hydrology.side;
    std::size_t best = 0;
    for (std::uint32_t z = 16; z + 16 < side; ++z) {
        for (std::uint32_t x = 16; x + 16 < side; ++x) {
            const std::size_t i = hydrology.index(x, z);
            if (hydrology.lake[i] == 0 && hydrology.accumulation[i] > hydrology.accumulation[best]) {
                best = i;
            }
        }
    }
    REQUIRE(isRiver(hydrology, best, rivers));
    const double metres = heightfield.desc().metresPerSample;
    const double targetX = static_cast<double>(best % side) * metres;
    const double targetZ = static_cast<double>(best / side) * metres;

    RenderView view;
    const CellId home = heightfield.cellAt(targetX, targetZ);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const CellId id{home.x + dx, home.z + dz};
            if (!heightfield.contains(id)) continue;
            const glm::dvec3 origin{heightfield.cellOriginX(id), 0.0, heightfield.cellOriginZ(id)};
            CellPayload payload;
            WorldStreamer::buildPayload(heightfield, id, StreamerConfig{}, payload);
            const std::uint32_t handle = renderer.uploadTerrainCell(payload.lods);
            REQUIRE(handle != Renderer::kNoHandle);
            view.terrain.push_back({handle, origin, (dx == 0 && dz == 0) ? 0u : 1u});
            const std::uint32_t surface = renderer.uploadWaterCell(buildWaterMesh(heightfield, hydrology, rivers, id));
            if (surface != Renderer::kNoHandle) {
                view.waterCells.push_back({surface, origin});
            }
        }
    }
    REQUIRE_FALSE(view.waterCells.empty());
    applyAtmosphere(atmosphereAt(15.0f), view);
    view.time = 1.25f;
    applyPalette(defaultPalette(), view);
    const glm::dvec3 target(targetX, heightfield.heightAt(targetX, targetZ), targetZ);
    const glm::dvec3 eye(targetX + eyeOffset.x, heightfield.heightAt(targetX + eyeOffset.x, targetZ + eyeOffset.y) + eyeHeight, targetZ + eyeOffset.y);
    view.camera.position = eye;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
    view.camera.farPlane = 2500.0f;

    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / std::format("{}.actual.png", name), kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile(std::format("{}.png", name));
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the biggest river of the forged world")
{
    checkRiver("terrain_river", {-16.0, 16.0}, 7.0);
}

TEST_CASE("golden image: ripples running down a river")
{
    checkRiver("terrain_river_ripples", {-6.0, 5.0}, 3.0);
}

TEST_CASE("golden image: the pause menu over the world")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    PauseMenu menu(ui);
    REQUIRE(menu.ok());
    menu.setVisible(true);
    ui.update(0.0);

    RenderView view;
    applyAtmosphere(atmosphereAt(15.0f), view);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, 0.15f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui_pause.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_pause.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the night sky looking up")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    RenderView view;
    applyAtmosphere(atmosphereAt(1.0f), view);
    view.time = 2.0f;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.2f, 1.0f, -0.3f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "sky_night.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("sky_night.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the noon sky with clouds")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    RenderView view;
    applyAtmosphere(atmosphereAt(12.0f), view);
    view.time = 2.0f;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.4f, 0.6f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "sky_noon.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("sky_noon.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image: the journal over the world")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VulkanUiRenderer vkUi(renderer.context(), renderer.colorFormat(), SceneRenderer::depthFormat());
    REQUIRE(vkUi.init());
    UiContext ui(vkUi, kWidth, kHeight);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    JournalScreen journal(ui);
    REQUIRE(journal.ok());
    journal.setRows({
        {"1:04:02", "At the weir I heard Kallias out: they wished me to carry the ash to the ferryman. I thought it over, and I agreed to go."},
        {"0:51:30", "A stranger found me near a place I cannot name, wanting me to help them. I have given no answer yet."},
        {"0:20:11", "Myrto met me at the {who} gate and asked me to <b>bring water</b>. In the end, I refused."},
    });
    journal.setVisible(true);
    ui.update(0.0);

    RenderView view;
    applyAtmosphere(atmosphereAt(17.5f), view);
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, 0.15f, -1.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<std::uint8_t> actual;
    REQUIRE(renderer.render(view, actual, [&](VkCommandBuffer cmd, VkExtent2D extent) {
        vkUi.begin(cmd, extent);
        ui.render();
        vkUi.end();
    }));
    std::filesystem::create_directories(kOutDir);
    REQUIRE(writePng(kOutDir / "ui_journal.actual.png", kWidth, kHeight, actual.data()));

    const std::filesystem::path goldenPath = goldenFile("ui_journal.png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    checkAgainstGolden(actual, goldenPath);
}

TEST_CASE("golden image tolerance: driver noise passes, a missing element does not")
{
    // No GPU needed: this pins the metric itself, on the committed goldens, so loosening a bound until a
    // removed draw call slips through turns this red.
    for (const char* name : {"ui_hud", "ui_pause", "ui_ledger", "kit", "terrain_graded"}) {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        std::vector<std::uint8_t> golden;
        REQUIRE(readPng(kGoldenDir / (std::string(name) + ".png"), w, h, golden));
        REQUIRE(w == kWidth);
        INFO(name);

        std::uint32_t seed = 12345;
        const auto next = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<int>(seed >> 24);
        };

        std::vector<std::uint8_t> noisy = golden;
        for (std::size_t i = 0; i < noisy.size(); ++i) {
            if (i % 4 != 3) {
                noisy[i] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(noisy[i]) + next() % 5 - 2, 0, 255));
            }
        }
        for (int i = 0; i < 120; ++i) { // stray edge pixels a rasteriser draws one pixel off
            const std::size_t pixel = static_cast<std::size_t>(next()) * 2000 % (noisy.size() / 4);
            noisy[pixel * 4] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(noisy[pixel * 4]) + 60, 0, 255));
        }
        CHECK(within(compare(noisy, golden, kBadThreshold), toleranceFor(true)));

        // Remove one element the size of a label or an icon (64x32): the block with the most content goes flat, as if the
        // draw call that made it was gone.
        constexpr std::size_t kBlockW = 64;
        constexpr std::size_t kBlockH = 32;
        const auto spread = [&](std::size_t x0, std::size_t y0) {
            int count = 0;
            for (std::size_t y = y0; y < y0 + kBlockH; ++y) {
                for (std::size_t x = x0; x < x0 + kBlockW; ++x) {
                    const std::size_t at = (y * kWidth + x) * 4;
                    const std::size_t base = (y0 * kWidth + x0) * 4;
                    int worst = 0;
                    for (std::size_t c = 0; c < 3; ++c) {
                        worst = std::max(worst, std::abs(static_cast<int>(golden[at + c]) - static_cast<int>(golden[base + c])));
                    }
                    count += worst > kBadThreshold ? 1 : 0;
                }
            }
            return count;
        };
        std::size_t bestX = 0;
        std::size_t bestY = 0;
        int bestSpread = -1;
        for (std::size_t y = 0; y + kBlockH <= kHeight; y += 16) {
            for (std::size_t x = 0; x + kBlockW <= kWidth; x += 16) {
                const int v = spread(x, y);
                if (v > bestSpread) {
                    bestSpread = v;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        std::vector<std::uint8_t> removed = golden;
        for (std::size_t y = bestY; y < bestY + kBlockH; ++y) {
            for (std::size_t x = bestX; x < bestX + kBlockW; ++x) {
                for (int c = 0; c < 3; ++c) {
                    removed[(y * kWidth + x) * 4 + c] = golden[(bestY * kWidth + bestX) * 4 + c];
                }
            }
        }
        const Diff gone = compare(removed, golden, kBadThreshold);
        INFO(std::format("most content in a block {}, worst tile {}, fraction {}", bestSpread, gone.tileBad, gone.fractionBad));
        CHECK_FALSE(within(gone, toleranceFor(true)));
    }
}

TEST_CASE("golden image failure output: a failed case leaves expected, actual and diff, a passing one nothing")
{
    // No GPU needed. A space in the folder and in a case name, because CI links these files from an HTML page.
    const std::filesystem::path root = kOutDir / "golden failure self test";
    const std::filesystem::path goldens = root / "goldens";
    const std::filesystem::path failDir = root / "golden-failures";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(goldens);
    const Tolerance limit = toleranceFor(true);
    const auto exists = [&](const std::string& file) { return std::filesystem::exists(failDir / file); };

    const std::vector<std::uint8_t> grey(static_cast<std::size_t>(kWidth) * kHeight * 4, 128);
    std::vector<std::uint8_t> blotched = grey;
    for (std::uint32_t y = 0; y < 64; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) {
            for (int c = 0; c < 3; ++c) {
                blotched[(static_cast<std::size_t>(y) * kWidth + x) * 4 + c] = 255;
            }
        }
    }
    REQUIRE(writePng(goldens / "plain.png", kWidth, kHeight, grey.data()));
    REQUIRE(writePng(goldens / "ui hud.png", kWidth, kHeight, grey.data()));
    REQUIRE(writePng(goldens / "small.png", kWidth / 2, kHeight / 2, grey.data()));

    SECTION("a match passes and leaves no folder, so CI has nothing to upload")
    {
        CHECK(judgeGolden(grey, goldens / "plain.png", limit, failDir).pass);
        CHECK_FALSE(std::filesystem::exists(failDir));
    }

    SECTION("several failures in one run each keep their three pictures and the reason")
    {
        const Verdict plain = judgeGolden(blotched, goldens / "plain.png", limit, failDir);
        const Verdict spaced = judgeGolden(blotched, goldens / "ui hud.png", limit, failDir);
        CHECK_FALSE(plain.pass);
        CHECK_FALSE(spaced.pass);
        for (const std::string name : {"plain", "ui hud"}) {
            INFO(name);
            CHECK(exists(name + ".expected.png"));
            CHECK(exists(name + ".actual.png"));
            CHECK(exists(name + ".diff.png"));
            CHECK(exists(name + ".txt"));
        }
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        std::vector<std::uint8_t> kept;
        REQUIRE(readPng(failDir / "plain.expected.png", w, h, kept));
        CHECK(kept == grey);
        REQUIRE(readPng(failDir / "plain.actual.png", w, h, kept));
        CHECK(kept == blotched);
        const std::string text = [&] {
            std::ifstream reason(failDir / "plain.txt");
            return std::string((std::istreambuf_iterator<char>(reason)), std::istreambuf_iterator<char>());
        }();
        CHECK(text.find("mean abs diff") != std::string::npos);

        SECTION("the same case passing on a rerun clears its stale pictures")
        {
            CHECK(judgeGolden(grey, goldens / "plain.png", limit, failDir).pass);
            CHECK_FALSE(exists("plain.actual.png"));
            CHECK_FALSE(exists("plain.expected.png"));
            CHECK_FALSE(exists("plain.diff.png"));
            CHECK_FALSE(exists("plain.txt"));
            CHECK(exists("ui hud.actual.png"));
        }
    }

    SECTION("a golden of another size fails with both sizes named and keeps expected and actual, no diff")
    {
        const Verdict verdict = judgeGolden(grey, goldens / "small.png", limit, failDir);
        CHECK_FALSE(verdict.pass);
        CHECK_FALSE(verdict.diff.has_value());
        CHECK(verdict.message.find("320x180") != std::string::npos);
        CHECK(verdict.message.find("640x360") != std::string::npos);
        CHECK(exists("small.expected.png"));
        CHECK(exists("small.actual.png"));
        CHECK_FALSE(exists("small.diff.png"));
    }

    SECTION("a missing golden fails and keeps the actual picture only")
    {
        const Verdict verdict = judgeGolden(grey, goldens / "absent.png", limit, failDir);
        CHECK_FALSE(verdict.pass);
        CHECK(verdict.message.find("no golden image") != std::string::npos);
        CHECK(exists("absent.actual.png"));
        CHECK_FALSE(exists("absent.expected.png"));
        CHECK_FALSE(exists("absent.diff.png"));
        CHECK(exists("absent.txt"));
    }

    std::filesystem::remove_all(root);
}

// The lavapipe job sets ICHOR_VALIDATION_CANARY and requires exactly this one validation error, which proves the
// Khronos layer reached the test processes and its output is still being counted.
TEST_CASE("golden image: validation canary")
{
    if (!envFlag("ICHOR_VALIDATION_CANARY")) {
        SUCCEED("canary only runs under the validation layer");
        return;
    }
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        NO_VULKAN_DEVICE();
    }
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // minLod above maxLod is invalid but drivers clamp it; an invalid anisotropy crashed the NVIDIA driver.
    info.minLod = 1.0f;
    info.maxLod = 0.0f;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(renderer.context().device(), &info, nullptr, &sampler) == VK_SUCCESS) {
        vkDestroySampler(renderer.context().device(), sampler, nullptr);
    }
    SUCCEED("invalid sampler submitted to the validation layer");
}
