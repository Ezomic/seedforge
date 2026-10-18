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
#include "ichor/stage0/camera_rig.h"
#include "ichor/stage0/ai_knobs.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/fight_review.h"

#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace {

struct Options {
    bool headless = false;
    bool lockOn = false;
    std::uint64_t frames = 0;
    std::uint64_t seed = 1;
    std::filesystem::path scene = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
    std::filesystem::path fights;
    std::filesystem::path record;
    std::filesystem::path replay;
    std::filesystem::path review;
    std::filesystem::path knobs;
    std::string session;
    std::string variable;
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
        else if (arg == "--lock-on") options.lockOn = true;
        else if (arg == "--frames" && hasValue) { if (!parseNumber(argv[++i], options.frames)) return false; }
        else if (arg == "--seed" && hasValue) { if (!parseNumber(argv[++i], options.seed)) return false; }
        else if (arg == "--scene" && hasValue) options.scene = argv[++i];
        else if (arg == "--fights" && hasValue) options.fights = argv[++i];
        else if (arg == "--record" && hasValue) options.record = argv[++i];
        else if (arg == "--replay" && hasValue) options.replay = argv[++i];
        else if (arg == "--review-fight" && hasValue) options.review = argv[++i];
        else if (arg == "--knobs" && hasValue) options.knobs = argv[++i];
        else if (arg == "--session" && hasValue) options.session = argv[++i];
        else if (arg == "--variable" && hasValue) options.variable = argv[++i];
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
    ichor::stage0::KnobLoad knobLoad;
    if (!options.knobs.empty()) {
        knobLoad = ichor::stage0::loadKnobsFromDirectory(options.knobs);
        for (const std::string& problem : knobLoad.problems) {
            ichor::logf(ichor::LogLevel::Warn, "knobs: {}", problem);
        }
    }
    tuning = ichor::stage0::applyKnobs(tuning, knobLoad.knobs);
    ichor::stage0::Stage0 stage(tuning);

    ichor::EngineConfig config;
    config.seed = options.seed;
    ichor::Engine engine(platform, renderer, config);
    engine.setAudio(&audio);

    // Automated runs log elsewhere so they never pollute the protocol's dataset.
    const std::filesystem::path fights = !options.fights.empty() ? options.fights
        : std::filesystem::path(options.headless ? "fights_headless.csv" : "fights.csv");
    stage.recorder().open(fights);
    ichor::stage0::appendKnobStamp(fights, ichor::stage0::knobStampLine(knobLoad.knobs,
        options.knobs.empty() ? std::string("defaults") : options.knobs.string(), stage.recorder().fightsWritten() + 1));
    stage.recorder().setSession({ichor::stage0::cleanField(options.session), ichor::stage0::cleanField(options.variable)});
    ichor::log(ichor::LogLevel::Info, ichor::stage0::progressLine(stage.recorder().gateProgress()));
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
    ichor::stage0::CameraTuning cameraTuning;
    if (options.lockOn) cameraTuning.framing = ichor::stage0::CameraFraming::LockOn;
    ichor::stage0::CameraRig cameraRig(cameraTuning);
    double cameraClock = platform.now();
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
            const double renderNow = platform.now();
            const ichor::stage0::CameraPose pose = cameraRig.update(p, f, renderNow - cameraClock);
            cameraClock = renderNow;
            view.camera.position = pose.position;
            view.camera.rotation = pose.rotation;
            view.camera.fovY = ichor::stage0::kCameraFovY;
            view.camera.nearPlane = ichor::stage0::kCameraNear;
            view.camera.farPlane = ichor::stage0::kCameraFar;
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
        ichor::log(ichor::LogLevel::Info, "usage: stage0 [--headless] [--lock-on] [--frames N] [--seed S] [--scene kit.iscene] [--fights out.csv] [--record in.iinp] [--replay in.iinp] [--review-fight fights.fights/fight_001.ifrd] [--knobs data_dir] [--session label] [--variable name]");
        return 2;
    }
    if (!options.review.empty()) {
        return ichor::stage0::reviewFight(options.review);
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
