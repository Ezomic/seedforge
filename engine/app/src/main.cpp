#include "ichor/assets/bundle_io.h"
#include "ichor/core/file_watch.h"
#include "ichor/core/log.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/gfx/vulkan_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/platform/sdl_platform.h"
#include "ichor/sim/scene_io.h"

#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace {

struct Options {
    bool headless = false;
    std::uint64_t frames = 0; // 0 runs until quit
    std::uint64_t seed = 1;
    std::filesystem::path scene = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
    std::filesystem::path record;
    std::filesystem::path replay;
    std::filesystem::path load;
    std::filesystem::path save;
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
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--frames" && hasValue) {
            if (!parseNumber(argv[++i], options.frames)) return false;
        } else if (arg == "--seed" && hasValue) {
            if (!parseNumber(argv[++i], options.seed)) return false;
        } else if (arg == "--scene" && hasValue) {
            options.scene = argv[++i];
        } else if (arg == "--record" && hasValue) {
            options.record = argv[++i];
        } else if (arg == "--replay" && hasValue) {
            options.replay = argv[++i];
        } else if (arg == "--load" && hasValue) {
            options.load = argv[++i];
        } else if (arg == "--save" && hasValue) {
            options.save = argv[++i];
        } else {
            ichor::logf(ichor::LogLevel::Error, "unknown argument: {}", arg);
            return false;
        }
    }
    return true;
}

// The kit laid out as entities: the three pieces, a slowly turning helmet,
// and the player, whose transform is the camera.
void populate(ichor::Scene& scene, const ichor::SceneBundle& bundle)
{
    const ichor::RenderView reference = ichor::kitReferenceView(bundle, 16.0f / 9.0f);
    for (std::size_t i = 0; i < bundle.nodes.size(); ++i) {
        const ichor::Node& node = bundle.nodes[i];
        ichor::SpawnCommand spawn;
        spawn.id = ichor::StableId::of(node.name, i);
        spawn.name.value = node.name;
        spawn.transform.position = reference.instances[i].position;
        spawn.transform.rotation = glm::quat_cast(node.world);
        spawn.transform.scale = glm::vec3(glm::length(node.world[0]), glm::length(node.world[1]), glm::length(node.world[2]));
        spawn.mesh = ichor::MeshRef{node.mesh};
        if (node.name.starts_with("helm_")) {
            spawn.spin = ichor::Spin{glm::vec3(0.0f, 1.0f, 0.0f), 0.6f};
        }
        scene.enqueue(spawn);
    }

    ichor::SpawnCommand player;
    player.id = ichor::StableId::of("player");
    player.name.value = "player";
    player.transform.position = reference.camera.position;
    player.player = ichor::Player{};
    scene.enqueue(player);
}

int run(ichor::Platform& platform, ichor::Renderer& renderer, const Options& options)
{
    if (!platform.init({}) || !renderer.init()) {
        return 1;
    }

    ichor::SceneBundle bundle;
    if (!ichor::readBundle(options.scene, bundle) || !renderer.uploadBundle(bundle)) {
        return 1;
    }

    ichor::EngineConfig config;
    config.seed = options.seed;
    ichor::Engine engine(platform, renderer, config);

    if (!options.load.empty()) {
        if (!ichor::loadScene(options.load, engine.scene())) {
            return 1;
        }
    } else {
        populate(engine.scene(), bundle);
    }
    if (!options.replay.empty()) {
        ichor::InputStream stream;
        if (!stream.load(options.replay)) {
            return 1;
        }
        engine.replayInputs(std::move(stream));
    }
    engine.recordInputs(!options.record.empty());

    const ichor::RenderView reference = ichor::kitReferenceView(bundle, 16.0f / 9.0f);
    engine.setViewBuilder([&](const ichor::Scene& scene, double, ichor::RenderView& view) {
        view.sunDirection = reference.sunDirection;
        view.lines = reference.lines;
        view.camera = reference.camera;
        for (auto [entity, transform, mesh] : scene.registry().view<const ichor::Transform, const ichor::MeshRef>().each()) {
            ichor::RenderInstance instance;
            instance.mesh = mesh.mesh;
            instance.position = transform.position;
            instance.basis = glm::mat3_cast(transform.rotation) * glm::mat3(glm::vec3(transform.scale.x, 0.0f, 0.0f), glm::vec3(0.0f, transform.scale.y, 0.0f), glm::vec3(0.0f, 0.0f, transform.scale.z));
            view.instances.push_back(instance);
        }
        for (auto [entity, transform, player] : scene.registry().view<const ichor::Transform, const ichor::Player>().each()) {
            const glm::dvec3 target(0.0, 0.55, 0.0);
            view.camera.position = transform.position;
            view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - transform.position)), glm::vec3(0.0f, 1.0f, 0.0f));
        }
    });

    ichor::FileWatcher watcher;
    watcher.watch(options.scene);
    watcher.watchDirectory(ICHOR_SHADER_DIR, ".spv");

    double last = platform.now();
    for (;;) {
        ZoneScopedN("frame");
        const double now = platform.now();
        const double dt = options.headless ? config.fixedDt : now - last;
        last = now;
        if (!engine.frame(dt)) {
            break;
        }
        if (options.frames != 0 && engine.frameCount() >= options.frames) {
            break;
        }
        if (engine.replayFinished() && options.frames == 0) {
            break;
        }
        if (engine.frameCount() % 30 == 0) {
            bool shaders = false;
            bool content = false;
            for (const auto& path : watcher.poll()) {
                shaders = shaders || path.extension() == ".spv";
                content = content || path == options.scene;
            }
            if (shaders) {
                renderer.reloadShaders();
            }
            if (content && ichor::readBundle(options.scene, bundle)) {
                renderer.uploadBundle(bundle);
            }
        }
    }

    if (!options.record.empty() && !engine.recordedInputs().save(options.record)) {
        return 1;
    }
    if (!options.save.empty() && !ichor::saveScene(options.save, engine.scene())) {
        return 1;
    }
    ichor::logf(ichor::LogLevel::Info, "exit after {} frames, {} ticks, state {:016x}",
        engine.frameCount(), engine.scene().tickCount(), engine.scene().stateHash());
    return 0;
}

}

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor [--headless] [--frames N] [--seed S] [--scene file.iscene] [--record in.iinp] [--replay in.iinp] [--load save.isav] [--save save.isav]");
        return 2;
    }

    if (options.headless) {
        ichor::NullPlatform platform;
        ichor::NullRenderer renderer;
        return run(platform, renderer, options);
    }

    ichor::SdlPlatform platform;
    ichor::VulkanRenderer renderer(platform);
    return run(platform, renderer, options);
}
