#include "ichor/assets/bundle_io.h"
#include "ichor/audio/miniaudio_device.h"
#include "ichor/audio/null_audio.h"
#include "ichor/core/log.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/gfx/vulkan_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/platform/sdl_platform.h"
#include "ichor/stage0/combat.h"

#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace {

struct Options {
    bool headless = false;
    std::uint64_t frames = 0;
    std::uint64_t seed = 1;
    std::filesystem::path scene = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
    std::filesystem::path fights;
    std::filesystem::path record;
    std::filesystem::path replay;
};

template <class T>
bool parseNumber(std::string_view text, T& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--headless") options.headless = true;
        else if (arg == "--frames" && hasValue) { if (!parseNumber(argv[++i], options.frames)) return false; }
        else if (arg == "--seed" && hasValue) { if (!parseNumber(argv[++i], options.seed)) return false; }
        else if (arg == "--scene" && hasValue) options.scene = argv[++i];
        else if (arg == "--fights" && hasValue) options.fights = argv[++i];
        else if (arg == "--record" && hasValue) options.record = argv[++i];
        else if (arg == "--replay" && hasValue) options.replay = argv[++i];
        else {
            ichor::logf(ichor::LogLevel::Error, "unknown argument: {}", arg);
            return false;
        }
    }
    return true;
}

int run(ichor::Platform& platform, ichor::Renderer& renderer, ichor::AudioHost& device, const Options& options)
{
    if (!platform.init({}) || !renderer.init()) {
        return 1;
    }
    ichor::NullAudio silent;
    ichor::AudioHost& audio = device.init() ? device : static_cast<ichor::AudioHost&>(silent);

    ichor::SceneBundle bundle;
    if (!ichor::readBundle(options.scene, bundle) || !renderer.uploadBundle(bundle)) {
        return 1;
    }
    audio.addBundle(bundle);

    ichor::stage0::Tuning tuning;
    tuning.lightWindupCue = audio.findSound("A_WindupLight");
    tuning.heavyWindupCue = audio.findSound("A_WindupHeavy");
    tuning.tellCue = audio.findSound("A_Tell");
    ichor::stage0::Stage0 stage(tuning);

    ichor::EngineConfig config;
    config.seed = options.seed;
    ichor::Engine engine(platform, renderer, config);
    engine.setAudio(&audio);

    // Automated runs log elsewhere so they never pollute the protocol's dataset.
    const std::filesystem::path fights = !options.fights.empty() ? options.fights
        : std::filesystem::path(options.headless ? "fights_headless.csv" : "fights.csv");
    stage.recorder().open(fights);
    stage.install(engine.scene(), {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    engine.scene().tick(0.0); // spawn, so the reset below has bodies to place
    stage.resetFight(engine.scene());

    if (!options.replay.empty()) {
        ichor::InputStream stream;
        if (!stream.load(options.replay)) return 1;
        engine.replayInputs(std::move(stream));
    }
    engine.recordInputs(!options.record.empty());

    const ichor::RenderView reference = ichor::kitReferenceView(bundle, 16.0f / 9.0f);
    engine.setViewBuilder([&](const ichor::Scene& scene, double alpha, ichor::RenderView& view) {
        view.sunDirection = reference.sunDirection;
        view.lines = reference.lines;
        view.camera = reference.camera;
        const auto blend = [alpha](const ichor::PreviousTransform& previous, const ichor::Transform& current) {
            return glm::mix(previous.position, current.position, alpha);
        };
        // Third person: behind the player, looking over them at the skirmisher.
        const entt::entity player = scene.find(stage.playerId());
        const entt::entity foe = scene.find(stage.foeId());
        if (player != entt::null && foe != entt::null) {
            const glm::dvec3 p = blend(scene.registry().get<ichor::PreviousTransform>(player), scene.registry().get<ichor::Transform>(player));
            const glm::dvec3 f = blend(scene.registry().get<ichor::PreviousTransform>(foe), scene.registry().get<ichor::Transform>(foe));
            glm::dvec3 away = p - f;
            away.y = 0.0;
            away = glm::length(away) > 1e-6 ? glm::normalize(away) : glm::dvec3(0.0, 0.0, 1.0);
            const glm::dvec3 eye = p + away * 3.2 + glm::dvec3(0.0, 2.0, 0.0);
            const glm::dvec3 target = (p + f) * 0.5 + glm::dvec3(0.0, 0.9, 0.0);
            view.camera.position = eye;
            view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        std::vector<ichor::LineSegment> lines;
        scene.physics().debugLines(lines);
        stage.debugLines(scene, lines);
        for (const ichor::LineSegment& line : lines) {
            view.lines.push_back({line.a, line.b, line.color});
        }
    });
    engine.setOverlayBuilder([&](const ichor::Scene& scene, std::vector<std::string>& lines) { stage.overlay(scene, lines); });

    double last = platform.now();
    for (;;) {
        ZoneScopedN("frame");
        const double now = platform.now();
        const double dt = options.headless ? config.fixedDt : now - last;
        last = now;
        if (!engine.frame(dt)) break;
        if (options.frames != 0 && engine.frameCount() >= options.frames) break;
        if (engine.replayFinished() && options.frames == 0) break;
    }
    if (!options.record.empty() && !engine.recordedInputs().save(options.record)) {
        return 1;
    }
    ichor::logf(ichor::LogLevel::Info, "exit after {} frames, {} ticks, {} fights recorded, state {:016x}",
        engine.frameCount(), engine.scene().tickCount(), stage.recorder().fightsWritten(), engine.scene().stateHash());
    return 0;
}

}

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        ichor::log(ichor::LogLevel::Info, "usage: stage0 [--headless] [--frames N] [--seed S] [--scene kit.iscene] [--fights out.csv] [--record in.iinp] [--replay in.iinp]");
        return 2;
    }
    if (options.headless) {
        ichor::NullPlatform platform;
        ichor::NullRenderer renderer;
        ichor::NullAudio audio;
        return run(platform, renderer, audio, options);
    }
    ichor::SdlPlatform platform;
    ichor::VulkanRenderer renderer(platform);
    ichor::MiniaudioDevice audio;
    return run(platform, renderer, audio, options);
}
