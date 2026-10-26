// Renders the Exile player in idle, run and a heavy swing, driven by the real combat code, on a
// headless Vulkan device: no window is created. Writes one PNG with the three side by side.
// With `--sheets` as the third argument it instead writes one contact sheet per clip into the
// folder named by the first argument: eight poses across the clip, front view above side view.
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/image.h"
#include "ichor/core/log.h"
#include "ichor/gfx/vk_offscreen.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/exile_player.h"
#include "ichor/stage0/grey_box_mesh.h"

#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <string>

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 480;
constexpr double kDt = 1.0 / 60.0;

using namespace ichor;
using namespace ichor::stage0;

struct Shot {
    const char* label;
    std::vector<std::uint8_t> pixels;
};

}

int writeSheets(const std::filesystem::path& outDir, const std::filesystem::path& artDir)
{
    constexpr std::uint32_t kCellW = 256;
    constexpr std::uint32_t kCellH = 384;
    constexpr int kColumns = 8;
    OffscreenRenderer renderer;
    if (!renderer.init(kCellW, kCellH, false)) {
        log(LogLevel::Error, "no Vulkan 1.3 device available");
        return 1;
    }
    SceneBundle bundle;
    appendGreyBoxMeshes(bundle);
    if (!renderer.uploadBundle(bundle)) return 1;
    ExilePlayer exile;
    if (!exile.prepare(artDir, static_cast<std::uint32_t>(bundle.meshes.size()))) return 1;
    for (const SceneBundle& part : exile.kit().bundles) {
        if (!renderer.uploadBundle(part)) return 1;
    }
    std::filesystem::create_directories(outDir);
    for (std::size_t i = 0; i < kExileClipCount; ++i) {
        ExileClipChoice choice;
        choice.clip = static_cast<ExileClip>(i);
        choice.locomotion = i <= static_cast<std::size_t>(ExileClip::Run);
        std::vector<std::uint8_t> sheet(static_cast<std::size_t>(kCellW) * kColumns * kCellH * 2 * 4);
        for (int view = 0; view < 2; ++view) {
            for (int column = 0; column < kColumns; ++column) {
                const float phase = static_cast<float>(column) / static_cast<float>(kColumns - 1);
                choice.progress = phase;
                std::vector<glm::mat4> skin;
                if (!exile.pose(choice, choice.locomotion ? phase * 0.875f : phase, skin)) return 1;
                RenderView rv;
                exile.appendPosed(skin, glm::dvec3(0.0), 0.0f, rv);
                for (int g = -3; g <= 3; ++g) {
                    const double at = static_cast<double>(g) * 0.5;
                    rv.lines.push_back({glm::dvec3(at, 0.0, -1.5), glm::dvec3(at, 0.0, 1.5), glm::vec3(0.35f)});
                    rv.lines.push_back({glm::dvec3(-1.5, 0.0, at), glm::dvec3(1.5, 0.0, at), glm::vec3(0.35f)});
                }
                rv.sunDirection = glm::normalize(glm::vec3(-0.5f, 0.9f, 0.35f));
                // The body faces -Z: the front view looks from -Z, the side view from +X.
                const glm::dvec3 eye = view == 0 ? glm::dvec3(0.0, 1.1, -4.6) : glm::dvec3(4.6, 1.1, 0.0);
                const glm::dvec3 target(0.0, 1.0, 0.0);
                rv.camera.position = eye;
                rv.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
                std::vector<std::uint8_t> pixels;
                if (!renderer.render(rv, pixels)) return 1;
                for (std::uint32_t y = 0; y < kCellH; ++y) {
                    std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(y) * kCellW * 4, kCellW * 4,
                        sheet.begin() + (static_cast<std::ptrdiff_t>(view * kCellH + y) * kCellW * kColumns + static_cast<std::ptrdiff_t>(column) * kCellW) * 4);
                }
            }
        }
        const std::filesystem::path file = outDir / (std::string("sheet_") + name(choice.clip) + ".png");
        if (!writePng(file, kCellW * kColumns, kCellH * 2, sheet.data())) return 1;
        logf(LogLevel::Info, "wrote {}", file.string());
    }
    return 0;
}

// Drives the real player through walking, running and sprinting and writes one PNG per gait: the live blended
// pose after 10, 30 and 70 ticks, front view above side view. Also prints where the held props sit.
int writeLocomotion(const std::filesystem::path& outDir, const std::filesystem::path& artDir)
{
    constexpr std::uint32_t kCellW = 256;
    constexpr std::uint32_t kCellH = 384;
    OffscreenRenderer renderer;
    if (!renderer.init(kCellW, kCellH, false)) return 1;
    SceneBundle bundle;
    appendGreyBoxMeshes(bundle);
    if (!renderer.uploadBundle(bundle)) return 1;
    ExilePlayer exile;
    if (!exile.prepare(artDir, static_cast<std::uint32_t>(bundle.meshes.size()))) return 1;
    for (const SceneBundle& part : exile.kit().bundles) {
        if (!renderer.uploadBundle(part)) return 1;
    }
    std::filesystem::create_directories(outDir);
    struct Gait {
        const char* label;
        float move;
        std::uint32_t buttons;
    };
    const Gait gaits[] = {{"walk", 0.3f, 0}, {"run", 1.0f, 0}, {"sprint", 1.0f, InputFrame::Sprint}};
    const int ticks[] = {10, 30, 70};
    const Tuning tuning;
    for (const Gait& gait : gaits) {
        Scene scene(1);
        Stage0 stage(tuning);
        stage.install(scene, {0.0, 0.0, 30.0}, {0.0, 0.0, -90.0});
        scene.tick(kDt);
        stage.resetFight(scene);
        ExilePlayer player;
        if (!player.prepare(artDir, static_cast<std::uint32_t>(bundle.meshes.size()))) return 1;
        for (int i = 0; i < 30; ++i) scene.tick(kDt);
        InputFrame input;
        input.moveY = gait.move;
        input.buttons = gait.buttons;
        std::vector<std::uint8_t> sheet(static_cast<std::size_t>(kCellW) * 3 * kCellH * 2 * 4);
        int tick = 0;
        for (int column = 0; column < 3; ++column) {
            RenderView view;
            for (; tick < ticks[column]; ++tick) {
                scene.tick(kDt, input);
                view.instances.clear();
                view.jointPalette.clear();
                player.append(scene, 1.0, kDt, static_cast<float>(kDt), tuning, view);
            }
            const entt::entity entity = scene.find(stage.playerId());
            const glm::dvec3 feet = scene.registry().get<Transform>(entity).position;
            for (std::size_t k = 0; k < view.instances.size(); ++k) {
                const glm::dvec3 rel = view.instances[k].position - feet;
                logf(LogLevel::Info, "{} t{} instance {} rel ({:.2f}, {:.2f}, {:.2f})", gait.label, ticks[column], k, rel.x, rel.y, rel.z);
            }
            for (int v = 0; v < 2; ++v) {
                RenderView rv = view;
                for (int g = -3; g <= 3; ++g) {
                    const double at = static_cast<double>(g) * 0.5;
                    rv.lines.push_back({feet + glm::dvec3(at, 0.0, -1.5), feet + glm::dvec3(at, 0.0, 1.5), glm::vec3(0.35f)});
                    rv.lines.push_back({feet + glm::dvec3(-1.5, 0.0, at), feet + glm::dvec3(1.5, 0.0, at), glm::vec3(0.35f)});
                }
                rv.sunDirection = glm::normalize(glm::vec3(-0.5f, 0.9f, 0.35f));
                const glm::dvec3 eye = feet + (v == 0 ? glm::dvec3(0.0, 1.1, -4.6) : glm::dvec3(4.6, 1.1, 0.0));
                const glm::dvec3 target = feet + glm::dvec3(0.0, 1.0, 0.0);
                rv.camera.position = eye;
                rv.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
                std::vector<std::uint8_t> pixels;
                if (!renderer.render(rv, pixels)) return 1;
                for (std::uint32_t y = 0; y < kCellH; ++y) {
                    std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(y) * kCellW * 4, kCellW * 4,
                        sheet.begin() + (static_cast<std::ptrdiff_t>(v * kCellH + y) * kCellW * 3 + static_cast<std::ptrdiff_t>(column) * kCellW) * 4);
                }
            }
        }
        const std::filesystem::path file = outDir / (std::string("live_") + gait.label + ".png");
        if (!writePng(file, kCellW * 3, kCellH * 2, sheet.data())) return 1;
        logf(LogLevel::Info, "wrote {}", file.string());
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 3 && std::string(argv[3]) == "--locomotion") {
        return writeLocomotion(argv[1], argc > 4 ? std::filesystem::path(argv[4]) : std::filesystem::path(ICHOR_ART_DIR) / "exile");
    }
    if (argc > 3 && std::string(argv[3]) == "--sheets") {
        return writeSheets(argv[1], argc > 4 ? std::filesystem::path(argv[4]) : std::filesystem::path(ICHOR_ART_DIR) / "exile");
    }
    const std::filesystem::path out = argc > 1 ? argv[1] : "exile_player.png";
    const std::filesystem::path artDir = argc > 2 ? argv[2] : std::filesystem::path(ICHOR_ART_DIR) / "exile";

    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        log(LogLevel::Error, "no Vulkan 1.3 device available");
        return 1;
    }
    SceneBundle bundle;
    const GreyBoxMeshes greyBox = appendGreyBoxMeshes(bundle);
    if (!renderer.uploadBundle(bundle)) return 1;

    ExilePlayer exile;
    if (!exile.prepare(artDir, static_cast<std::uint32_t>(bundle.meshes.size()))) return 1;
    for (const SceneBundle& part : exile.kit().bundles) {
        if (!renderer.uploadBundle(part)) return 1;
    }

    const Tuning tuning;
    std::vector<Shot> shots;
    const auto shoot = [&](const char* label, const InputFrame& input, int ticks, bool untilActive) {
        Scene scene(1);
        Stage0 stage(tuning);
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -9.0});
        scene.tick(kDt);
        stage.resetFight(scene);
        for (int i = 0; i < 30; ++i) scene.tick(kDt);
        RenderView view;
        for (int i = 0; i < 400; ++i) {
            if (i >= ticks && !untilActive) break;
            scene.tick(kDt, input);
            const entt::entity player = scene.find(stage.playerId());
            view.instances.clear();
            view.jointPalette.clear();
            exile.append(scene, 1.0, kDt, static_cast<float>(kDt), tuning, view);
            if (untilActive && scene.registry().get<Combatant>(player).combatState() == CombatState::Active) break;
        }
        const glm::dvec3 feet = scene.registry().get<Transform>(scene.find(stage.playerId())).position;
        for (int i = -4; i <= 4; ++i) {
            const double at = static_cast<double>(i) * 0.5;
            view.lines.push_back({feet + glm::dvec3(at, 0.0, -2.0), feet + glm::dvec3(at, 0.0, 2.0), glm::vec3(0.35f)});
            view.lines.push_back({feet + glm::dvec3(-2.0, 0.0, at), feet + glm::dvec3(2.0, 0.0, at), glm::vec3(0.35f)});
        }
        view.sunDirection = glm::normalize(glm::vec3(-0.5f, 0.9f, 0.35f));
        const glm::dvec3 eye = feet + glm::dvec3(-1.9, 1.3, -2.4);
        const glm::dvec3 target = feet + glm::dvec3(0.0, 0.9, 0.0);
        view.camera.position = eye;
        view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
        Shot shot{label, {}};
        if (!renderer.render(view, shot.pixels)) return false;
        shots.push_back(std::move(shot));
        return true;
    };

    InputFrame run;
    run.moveY = 1.0f;
    InputFrame heavy;
    heavy.buttons = InputFrame::Heavy;
    if (!shoot("idle", {}, 5, false) || !shoot("run", run, 45, false) || !shoot("swing", heavy, 0, true)) return 1;

    std::vector<std::uint8_t> strip(static_cast<std::size_t>(kWidth) * shots.size() * kHeight * 4);
    for (std::size_t s = 0; s < shots.size(); ++s) {
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            std::copy_n(shots[s].pixels.begin() + static_cast<std::ptrdiff_t>(y) * kWidth * 4, kWidth * 4,
                strip.begin() + (static_cast<std::ptrdiff_t>(y) * kWidth * static_cast<std::ptrdiff_t>(shots.size()) + static_cast<std::ptrdiff_t>(s) * kWidth) * 4);
        }
    }
    if (!writePng(out, kWidth * static_cast<std::uint32_t>(shots.size()), kHeight, strip.data())) return 1;
    logf(LogLevel::Info, "wrote {}", out.string());
    return 0;
}
