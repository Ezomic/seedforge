#include "ichor/anim/animation.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/audio/miniaudio_device.h"
#include "ichor/audio/null_audio.h"
#include "ichor/core/file_watch.h"
#include "ichor/core/log.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/gfx/vulkan_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/platform/sdl_platform.h"
#include "ichor/sim/scene_io.h"
#include "ichor/world/navmesh.h"
#include "ichor/world/streamer.h"
#include "ichor/world/world_io.h"

#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Options {
    bool headless = false;
    bool audioProbe = false; // open the device, play nothing, exit
    std::uint64_t frames = 0; // 0 runs until quit
    std::uint64_t seed = 1;
    std::filesystem::path scene = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
    std::filesystem::path actor = std::filesystem::path(ICHOR_CONTENT_DIR) / "arm.iscene";
    std::filesystem::path world = std::filesystem::path(ICHOR_CONTENT_DIR) / "world"; // empty runs without terrain
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
        } else if (arg == "--audio-probe") {
            options.audioProbe = true;
        } else if (arg == "--frames" && hasValue) {
            if (!parseNumber(argv[++i], options.frames)) return false;
        } else if (arg == "--seed" && hasValue) {
            if (!parseNumber(argv[++i], options.seed)) return false;
        } else if (arg == "--scene" && hasValue) {
            options.scene = argv[++i];
        } else if (arg == "--actor" && hasValue) {
            options.actor = argv[++i];
        } else if (arg == "--world" && hasValue) {
            options.world = argv[++i];
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
void populate(ichor::Scene& scene, const ichor::SceneBundle& bundle, std::uint32_t armMesh, std::uint32_t armClip, glm::dvec3 anchor)
{
    const ichor::RenderView reference = ichor::kitReferenceView(bundle, 16.0f / 9.0f);
    for (std::size_t i = 0; i < bundle.nodes.size(); ++i) {
        const ichor::Node& node = bundle.nodes[i];
        ichor::SpawnCommand spawn;
        spawn.id = ichor::StableId::of(node.name, i);
        spawn.name.value = node.name;
        spawn.transform.position = anchor + reference.instances[i].position;
        spawn.transform.rotation = glm::quat_cast(node.world);
        spawn.transform.scale = glm::vec3(glm::length(node.world[0]), glm::length(node.world[1]), glm::length(node.world[2]));
        spawn.mesh = ichor::MeshRef{node.mesh};
        if (node.name.starts_with("helm_")) {
            spawn.spin = ichor::Spin{glm::vec3(0.0f, 1.0f, 0.0f), 0.6f};
        }
        scene.enqueue(spawn);
    }

    // The arena: a floor whose top meets the kit's ground plane, and four walls.
    const auto box = [&](std::string_view name, glm::dvec3 position, glm::vec3 half) {
        ichor::SpawnCommand spawn;
        spawn.id = ichor::StableId::of(name);
        spawn.name.value = std::string(name);
        spawn.transform.position = anchor + position;
        spawn.staticBox = ichor::StaticBox{half};
        scene.enqueue(spawn);
    };
    box("floor", {0.0, -0.95, 0.0}, {10.0f, 0.5f, 10.0f});
    box("wall-east", {10.25, 0.5, 0.0}, {0.25f, 1.5f, 10.0f});
    box("wall-west", {-10.25, 0.5, 0.0}, {0.25f, 1.5f, 10.0f});
    box("wall-north", {0.0, 0.5, -10.25}, {10.0f, 1.5f, 0.25f});
    box("wall-south", {0.0, 0.5, 10.25}, {10.0f, 1.5f, 0.25f});

    // The waving arm stands beside the shield. Root motion is off here so it
    // stays in view; the simulation tests cover it walking.
    ichor::SpawnCommand arm;
    arm.id = ichor::StableId::of("arm");
    arm.name.value = "arm";
    arm.transform.position = anchor + glm::dvec3{-1.3, -0.45, -0.7};
    arm.mesh = ichor::MeshRef{armMesh};
    arm.animator = ichor::Animator{armClip, 0.0f, 1.0f, true, false};
    scene.enqueue(arm);

    ichor::SpawnCommand player;
    player.id = ichor::StableId::of("player");
    player.name.value = "player";
    player.transform.position = anchor + glm::dvec3{reference.camera.position.x, 0.5, reference.camera.position.z};
    player.player = ichor::Player{};
    player.character = ichor::CharacterBody{};
    scene.enqueue(player);
}

int run(ichor::Platform& platform, ichor::Renderer& renderer, ichor::AudioHost& device, const Options& options)
{
    if (!platform.init({}) || !renderer.init()) {
        return 1;
    }
    // No playback device is not a reason not to run; cues then go to the null host.
    ichor::NullAudio silent;
    ichor::AudioHost& audio = device.init() ? device : static_cast<ichor::AudioHost&>(silent);
    if (&audio == &silent) {
        ichor::log(ichor::LogLevel::Warn, "audio: running silent");
    }

    ichor::SceneBundle bundle;
    if (!ichor::readBundle(options.scene, bundle) || !renderer.uploadBundle(bundle)) {
        return 1;
    }
    ichor::AnimationSet animations;
    ichor::SceneBundle actor;
    const std::uint32_t armMesh = renderer.meshCount();
    if (!ichor::readBundle(options.actor, actor) || !renderer.uploadBundle(actor) || !animations.load(actor)) {
        return 1;
    }
    audio.addBundle(bundle);

    // The forged world, if there is one: its splat layers go to the renderer
    // and its cells stream around the player. The arena sits on a plateau at
    // the world's centre, above the terrain under it.
    ichor::Heightfield heightfield;
    glm::dvec3 anchor(0.0);
    if (!options.world.empty()) {
        ichor::SceneBundle layers;
        if (!ichor::readWorld(options.world, heightfield) || !ichor::readBundle(options.world / "terrain.iscene", layers) || !renderer.setTerrainMaterials(layers)) {
            return 1;
        }
        const double centre = heightfield.desc().sizeMetres * 0.5;
        float plateau = -1e30f;
        for (int z = -12; z <= 12; z += 2) {
            for (int x = -12; x <= 12; x += 2) {
                plateau = std::max(plateau, heightfield.heightAt(centre + x, centre + z));
            }
        }
        anchor = {centre, plateau + 0.5, centre};
    }
    ichor::StreamerConfig streaming;
    streaming.synchronous = options.headless; // a replay must see the same cells on the same ticks
    streaming.navmesh = true;
    ichor::NavWorld navigation;
    if (!heightfield.empty() && !navigation.init(heightfield.desc(), 128)) {
        return 1;
    }
    ichor::WorldStreamer streamer(heightfield, streaming);

    const std::uint32_t armClip = animations.findClip("wave");
    const std::uint32_t windupCue = audio.findSound("A_WindupLight");
    const std::uint32_t tellCue = audio.findSound("A_Tell");
    if (armClip == ichor::AnimationSet::kNoClip) {
        ichor::log(ichor::LogLevel::Error, "the actor bundle has no clip named wave");
        return 1;
    }

    ichor::EngineConfig config;
    config.seed = options.seed;
    ichor::Engine engine(platform, renderer, config);
    engine.scene().setAnimations(&animations);
    engine.setAudio(&audio);

    if (!options.load.empty()) {
        if (!ichor::loadScene(options.load, engine.scene())) {
            return 1;
        }
    } else {
        populate(engine.scene(), bundle, armMesh, armClip, anchor);
    }

    struct ResidentCell {
        std::uint32_t handle = ichor::Renderer::kNoHandle;
        glm::dvec3 origin{0.0};
    };
    std::map<ichor::CellId, ResidentCell> cells;
    streamer.onLoaded([&](const ichor::CellPayload& payload) {
        ResidentCell cell;
        cell.origin = {heightfield.cellOriginX(payload.cell.id), 0.0, heightfield.cellOriginZ(payload.cell.id)};
        cell.handle = renderer.uploadTerrainCell(payload.lods);
        engine.scene().physics().addHeightField(ichor::StableId::of("cell", ichor::cellKey(payload.cell.id)), payload.cell.heights.data(), payload.cell.samplesPerSide, cell.origin,
            static_cast<float>(heightfield.desc().metresPerSample));
        cells[payload.cell.id] = cell;
        ichor::NavTile tile = payload.nav;
        if (!tile.empty()) {
            navigation.addTile(payload.cell.id, std::move(tile));
        }
    });
    streamer.onUnloaded([&](ichor::CellId id) {
        if (const auto found = cells.find(id); found != cells.end()) {
            renderer.removeTerrainCell(found->second.handle);
            cells.erase(found);
        }
        engine.scene().physics().remove(ichor::StableId::of("cell", ichor::cellKey(id)));
        navigation.removeTile(id);
    });
    // Until the player exists the arena's anchor stands in, so the first
    // tick, which spawns the player there, finds its ground resident.
    const auto playerPosition = [anchor](const ichor::Scene& scene) {
        glm::dvec3 position = anchor;
        for (auto [entity, transform, player] : scene.registry().view<const ichor::Transform, const ichor::Player>().each()) {
            position = transform.position;
        }
        return position;
    };
    if (!heightfield.empty()) {
        // Streaming advances once per tick, so residency follows the path and
        // the budget alone. While the gate holds no ticks run, so it advances
        // the streamer itself until the required ring is in.
        engine.scene().addSystem("streaming", [&](ichor::Scene& scene, double, const ichor::InputFrame&) {
            streamer.update(playerPosition(scene));
        });
        engine.setTickGate([&](const ichor::Scene& scene) {
            const glm::dvec3 position = playerPosition(scene);
            if (streamer.ready(position)) {
                return true;
            }
            streamer.update(position);
            return streamer.ready(position);
        });
        engine.setOverlayBuilder([&](const ichor::Scene&, std::vector<std::string>& lines) {
            lines.push_back(std::format("cells {} resident, {} pending, {} nav tiles", streamer.resident().size(), streamer.pending(), navigation.tileCount()));
        });
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
    engine.setViewBuilder([&](const ichor::Scene& scene, double alpha, ichor::RenderView& view) {
        view.sunDirection = reference.sunDirection;
        view.lines = reference.lines;
        view.camera = reference.camera;
        if (!heightfield.empty()) {
            view.camera.farPlane = 2500.0f;
        }
        // Rendering sits between the last two fixed steps.
        const auto blend = [alpha](const ichor::PreviousTransform& previous, const ichor::Transform& current) {
            return std::pair{glm::mix(previous.position, current.position, alpha), glm::slerp(previous.rotation, current.rotation, static_cast<float>(alpha))};
        };
        for (auto [entity, previous, transform, mesh] : scene.registry().view<const ichor::PreviousTransform, const ichor::Transform, const ichor::MeshRef>().each()) {
            const auto [position, rotation] = blend(previous, transform);
            ichor::RenderInstance instance;
            instance.mesh = mesh.mesh;
            instance.position = position;
            instance.basis = glm::mat3_cast(rotation) * glm::mat3(glm::vec3(transform.scale.x, 0.0f, 0.0f), glm::vec3(0.0f, transform.scale.y, 0.0f), glm::vec3(0.0f, 0.0f, transform.scale.z));
            if (const auto* pose = scene.registry().try_get<ichor::SkinPose>(entity); pose != nullptr && !pose->matrices.empty()) {
                instance.jointOffset = static_cast<std::uint32_t>(view.jointPalette.size());
                instance.jointCount = static_cast<std::uint32_t>(pose->matrices.size());
                view.jointPalette.insert(view.jointPalette.end(), pose->matrices.begin(), pose->matrices.end());
            }
            view.instances.push_back(instance);
        }
        for (auto [entity, previous, transform, player] : scene.registry().view<const ichor::PreviousTransform, const ichor::Transform, const ichor::Player>().each()) {
            const glm::dvec3 target = anchor + glm::dvec3(0.0, 0.55, 0.0);
            const glm::dvec3 eye = blend(previous, transform).first + glm::dvec3(0.0, 1.6, 0.0);
            view.camera.position = eye;
            view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        const double cellMetres = heightfield.desc().cellMetres;
        for (const auto& [id, cell] : cells) {
            if (cell.handle == ichor::Renderer::kNoHandle) continue;
            const glm::dvec3 centre = cell.origin + glm::dvec3(cellMetres * 0.5, 0.0, cellMetres * 0.5);
            const double distance = glm::length(glm::dvec2(centre.x - view.camera.position.x, centre.z - view.camera.position.z)) / cellMetres;
            const std::uint32_t lod = distance < 1.2 ? 0u : distance < 2.2 ? 1u : 2u;
            view.terrain.push_back({cell.handle, cell.origin, lod});
        }
        std::vector<ichor::LineSegment> colliders;
        scene.physics().debugLines(colliders);
        for (const ichor::LineSegment& line : colliders) {
            view.lines.push_back({line.a, line.b, line.color});
        }
    });

    ichor::FileWatcher watcher;
    watcher.watch(options.scene);
    watcher.watchDirectory(ICHOR_SHADER_DIR, ".spv");

    double last = platform.now();
    bool confirmHeld = false;
    for (;;) {
        ZoneScopedN("frame");
        // Enter starts the light attack on the arm, with its wind-up and tell cues.
        const bool confirm = (platform.input().buttons & ichor::InputFrame::Confirm) != 0;
        if (confirm && !confirmHeld) {
            engine.scene().enqueue(ichor::PlayMontageCommand{ichor::StableId::of("arm"), armClip, 0.35f, 0.5f, 0.9f, windupCue, tellCue});
        }
        confirmHeld = confirm;
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
        ichor::log(ichor::LogLevel::Info, "usage: ichor [--headless] [--frames N] [--seed S] [--scene file.iscene] [--world dir] [--record in.iinp] [--replay in.iinp] [--load save.isav] [--save save.isav]");
        return 2;
    }

    if (options.audioProbe) {
        ichor::MiniaudioDevice device;
        if (!device.init()) {
            return 1;
        }
        ichor::log(ichor::LogLevel::Info, "audio probe: device opened and started");
        return 0;
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
