#include "ichor/anim/animation.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/image.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/gfx/renderer.h"
#include "ichor/gfx/vk_offscreen.h"
#include "ichor/gfx/vulkan_ui.h"
#include "ichor/ui/ui_context.h"
#include "ichor/world/atmosphere.h"
#include "ichor/world/vegetation.h"
#include "ichor/world/weather.h"
#include "ichor/world/streamer.h"
#include "ichor/world/world_io.h"

#include <catch2/catch_test_macros.hpp>

#include <RmlUi/Core/ElementDocument.h>

#include <cstdlib>
#include <filesystem>
#include <format>

using namespace ichor;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 360;

const std::filesystem::path kGoldenDir = std::filesystem::path(ICHOR_TEST_DATA) / "golden";
const std::filesystem::path kContent = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
const std::filesystem::path kArmContent = std::filesystem::path(ICHOR_CONTENT_DIR) / "arm.iscene";
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

TEST_CASE("golden image: the skinned arm mid-wave")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        SKIP("no Vulkan 1.3 device available");
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

    const std::filesystem::path goldenPath = kGoldenDir / "arm.png";
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(goldenPath, w, h, expected)) {
        FAIL("no golden image at " << goldenPath.string() << "; inspect arm.actual.png and rerun with ICHOR_UPDATE_GOLDEN=1");
    }
    const Diff diff = compare(actual, expected, 16);
    writePng(kOutDir / "arm.diff.png", kWidth, kHeight, diff.image.data());
    INFO(std::format("mean abs diff {:.3f}, {:.4f}% pixels over threshold", diff.meanAbs, diff.fractionBad * 100.0));
    CHECK(diff.meanAbs < 1.5);
    CHECK(diff.fractionBad < 0.005);
}

TEST_CASE("golden image: the forged terrain at the world's centre")
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        SKIP("no Vulkan 1.3 device available");
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

    const std::filesystem::path goldenPath = kGoldenDir / "terrain.png";
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(goldenPath, w, h, expected)) {
        FAIL("no golden image at " << goldenPath.string() << "; inspect terrain.actual.png and rerun with ICHOR_UPDATE_GOLDEN=1");
    }
    const Diff diff = compare(actual, expected, 16);
    writePng(kOutDir / "terrain.diff.png", kWidth, kHeight, diff.image.data());
    INFO(std::format("mean abs diff {:.3f}, {:.4f}% pixels over threshold", diff.meanAbs, diff.fractionBad * 100.0));
    CHECK(diff.meanAbs < 1.5);
    CHECK(diff.fractionBad < 0.005);
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
        SKIP("no Vulkan 1.3 device available");
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
    const std::filesystem::path goldenPath = kGoldenDir / "ui.png";
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(goldenPath, w, h, expected)) {
        FAIL("no golden image at " << goldenPath.string() << "; inspect ui.actual.png and rerun with ICHOR_UPDATE_GOLDEN=1");
    }
    const Diff diff = compare(actual, expected, 16);
    writePng(kOutDir / "ui.diff.png", kWidth, kHeight, diff.image.data());
    INFO(std::format("mean abs diff {:.3f}, {:.4f}% pixels over threshold", diff.meanAbs, diff.fractionBad * 100.0));
    CHECK(diff.meanAbs < 1.5);
    CHECK(diff.fractionBad < 0.005);
}

namespace {

// The terrain at the world's centre under the sky of a given hour, looking
// towards where the sun sets, so the dusk glow and the fog are in view.
void checkTerrainAtHour(float hour, const char* name, bool vegetation = false, Weather weather = {}, bool water = false, bool submerged = false)
{
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        SKIP("no Vulkan 1.3 device available");
    }
    const std::filesystem::path worldDir = std::filesystem::path(ICHOR_CONTENT_DIR) / "world";
    Heightfield heightfield;
    REQUIRE(readWorld(worldDir, heightfield));
    SceneBundle layers;
    REQUIRE(readBundle(worldDir / "terrain.iscene", layers));
    REQUIRE(renderer.setTerrainMaterials(layers));

    RenderView view;
    if (vegetation) {
        REQUIRE(renderer.setVegetationMeshes(treeBundle()));
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
    view.time = 1.25f;
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

    const std::filesystem::path goldenPath = kGoldenDir / (std::string(name) + ".png");
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(goldenPath, w, h, expected)) {
        FAIL("no golden image at " << goldenPath.string() << "; inspect " << name << ".actual.png and rerun with ICHOR_UPDATE_GOLDEN=1");
    }
    const Diff diff = compare(actual, expected, 16);
    writePng(kOutDir / (std::string(name) + ".diff.png"), kWidth, kHeight, diff.image.data());
    INFO(std::format("mean abs diff {:.3f}, {:.4f}% pixels over threshold", diff.meanAbs, diff.fractionBad * 100.0));
    CHECK(diff.meanAbs < 1.5);
    CHECK(diff.fractionBad < 0.005);
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

TEST_CASE("golden image: a downpour")
{
    checkTerrainAtHour(15.0f, "terrain_rain", true, {1.0f, 1.0f});
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
        SKIP("no Vulkan 1.3 device available");
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

    const std::filesystem::path goldenPath = kGoldenDir / "ui_text.png";
    const char* update = std::getenv("ICHOR_UPDATE_GOLDEN");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(writePng(goldenPath, kWidth, kHeight, actual.data()));
        SUCCEED("golden image written");
        return;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> expected;
    if (!readPng(goldenPath, w, h, expected)) {
        FAIL("no golden image at " << goldenPath.string() << "; inspect ui_text.actual.png and rerun with ICHOR_UPDATE_GOLDEN=1");
    }
    const Diff diff = compare(actual, expected, 16);
    writePng(kOutDir / "ui_text.diff.png", kWidth, kHeight, diff.image.data());
    INFO(std::format("mean abs diff {:.3f}, {:.4f}% pixels over threshold", diff.meanAbs, diff.fractionBad * 100.0));
    CHECK(diff.meanAbs < 1.5);
    CHECK(diff.fractionBad < 0.005);
}
