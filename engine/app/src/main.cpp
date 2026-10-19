#include "ichor/anim/animation.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/audio/miniaudio_device.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/ambience_layers.h"
#include "ichor/audio/music_player.h"
#include "ichor/audio/rain_ambience.h"
#include "ichor/audio/weather_ambience.h"
#include "ichor/audio/reverb.h"
#include "ichor/audio/wading_splash.h"
#include "ichor/core/file_watch.h"
#include "ichor/core/log.h"
#include "ichor/editor/nudge.h"
#include "ichor/editor/panels.h"
#include "ichor/editor/picking.h"
#include "ichor/ui/combat_hud_screen.h"
#include "ichor/ui/hud.h"
#include "ichor/world/palette.h"
#include "ichor/world/water_field.h"
#include "ichor/traversal/roads.h"
#include "ichor/ui/ledger.h"
#include "ichor/ui/character_screen.h"
#include "ichor/ui/journal_screen.h"
#include "ichor/ui/landmark_compass_screen.h"
#include "ichor/ui/journal_screen_bind.h"
#include "ichor/data/journal.h"
#include "ichor/script/journal_bindings.h"
#include "ichor/ui/map_screen.h"
#include "ichor/ui/map_screen_binding.h"
#include "ichor/ui/map_screen_model.h"
#include "ichor/ui/new_game_screen.h"
#include "ichor/ui/pause_menu.h"
#include "ichor/ui/ui_context.h"
#include "ichor/data/data_set.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/gfx/reference_view.h"
#include "ichor/gfx/vulkan_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/platform/sdl_platform.h"
#include "ichor/save/save_slots.h"
#include "ichor/save/settings.h"
#include "ichor/script/chronicle_bindings.h"
#include "ichor/script/world_bindings.h"
#include "ichor/script/data_bindings.h"
#include "ichor/script/scene_bindings.h"
#include "ichor/script/script_host.h"
#include "ichor/sim/scene_io.h"
#include "ichor/world/atmosphere.h"
#include "ichor/ui/loading_screen.h"
#include "ichor/ui/new_game_flow.h"
#include "ichor/world/forge.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/hydrology_cache.h"
#include "ichor/world/navmesh.h"
#include "ichor/world/vegetation.h"
#include "ichor/world/weather.h"
#include "ichor/world/streamer.h"
#include "ichor/world/world_io.h"

#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <utility>
#include <vector>

namespace {

constexpr float kFovStepDegrees = 5.0f;

struct Options {
    bool headless = false;
    bool audioProbe = false; // open the device, play nothing, exit
    std::uint64_t frames = 0; // 0 runs until quit
    std::uint64_t seed = 1;
    std::filesystem::path scene = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
    std::filesystem::path actor = std::filesystem::path(ICHOR_CONTENT_DIR) / "arm.iscene";
    std::filesystem::path world = std::filesystem::path(ICHOR_CONTENT_DIR) / "world"; // empty runs without terrain
    std::filesystem::path script = std::filesystem::path(ICHOR_SCRIPT_DIR) / "main.lua";
    std::filesystem::path data = ICHOR_DATA_DIR;
    std::filesystem::path saves = "saves";
    std::filesystem::path exportChronicle; // written at exit when set
    std::string loadSlot;
    bool ironman = false;
    bool editor = false;
    bool showMap = false;
    bool showNewGame = false;
    bool forged = false; // build the world from libworldforge instead of the stand-in terrain
    int forgedGrid = ichor::kForgedDefaultGridSize;
    std::string screenshot; // where to save the last frame of a windowed run; empty is none
    float startHour = 9.0f;      // hour of the day when play starts
    std::optional<float> waterLevel; // world y of a sheet of water over the whole world; none is dry
    std::optional<std::pair<double, double>> compassTarget; // world x and z the compass points at; none shows no line
    double daySeconds = 1200.0; // simulated seconds in a whole day
    double autosaveSeconds = 0.0; // 0 is off
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
        } else if (arg == "--script" && hasValue) {
            options.script = argv[++i];
        } else if (arg == "--data" && hasValue) {
            options.data = argv[++i];
        } else if (arg == "--saves" && hasValue) {
            options.saves = argv[++i];
        } else if (arg == "--export-chronicle" && hasValue) {
            options.exportChronicle = argv[++i];
        } else if (arg == "--load-slot" && hasValue) {
            options.loadSlot = argv[++i];
        } else if (arg == "--water" && hasValue) {
            float level = 0.0f;
            if (!parseNumber(argv[++i], level)) return false;
            options.waterLevel = level;
        } else if (arg == "--target" && i + 2 < argc) {
            double x = 0.0;
            double z = 0.0;
            if (!parseNumber(argv[i + 1], x) || !parseNumber(argv[i + 2], z) || !std::isfinite(x) || !std::isfinite(z)) return false;
            i += 2;
            options.compassTarget = std::make_pair(x, z);
        } else if (arg == "--hour" && hasValue) {
            if (!parseNumber(argv[++i], options.startHour)) return false;
        } else if (arg == "--day-seconds" && hasValue) {
            if (!parseNumber(argv[++i], options.daySeconds) || options.daySeconds <= 0.0) return false;
        } else if (arg == "--screenshot" && hasValue) {
            options.screenshot = argv[++i];
        } else if (arg == "--editor") {
            options.editor = true;
        } else if (arg == "--map") {
            options.showMap = true;
        } else if (arg == "--new-game") {
            options.showNewGame = true;
        } else if (arg == "--forged") {
            options.forged = true;
        } else if (arg == "--grid" && hasValue) {
            if (!parseNumber(argv[++i], options.forgedGrid) || options.forgedGrid < 16 || options.forgedGrid > 1024) return false;
        } else if (arg == "--ironman") {
            options.ironman = true;
        } else if (arg == "--autosave" && hasValue) {
            if (!parseNumber(argv[++i], options.autosaveSeconds) || options.autosaveSeconds < 0.0) return false;
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

// The forged world has no arena: the player stands where the spawn search put them.
void populateForged(ichor::Scene& scene, glm::dvec3 position)
{
    ichor::SpawnCommand player;
    player.id = ichor::StableId::of("player");
    player.name.value = "player";
    player.transform.position = position;
    player.player = ichor::Player{};
    player.character = ichor::CharacterBody{};
    scene.enqueue(player);
}

enum class ForgeOutcome { Built, Quit, Failed };

// Forges the world for --forged through the new-game flow: a loading screen with the phase being
// forged when there is a window, a plain wait when there is none. Quitting mid-forge cancels the job
// and joins its thread before this returns.
ForgeOutcome forgeGame(ichor::Platform& platform, ichor::Renderer& renderer, const Options& options, const ichor::RenderView& blank, std::unique_ptr<ichor::ForgedGame>& out)
{
    ichor::WorldDesc desc;
    desc.seed = options.seed;
    ichor::NewGameFlow flow(true, options.forgedGrid);
    flow.begin(desc);

    std::unique_ptr<ichor::UiContext> ui;
    std::unique_ptr<ichor::LoadingScreen> loading;
    if (!options.headless && renderer.uiRenderInterface() != nullptr) {
        const ichor::Extent size = platform.drawableSize();
        ui = std::make_unique<ichor::UiContext>(*renderer.uiRenderInterface(), static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height));
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
        bool fontsLoaded = ui->ok();
        for (const char* file : {"AlegreyaSans-Regular.ttf", "Cinzel.ttf"}) {
            fontsLoaded = fontsLoaded && ui->loadFontFace(fonts / file);
        }
        if (fontsLoaded) {
            loading = std::make_unique<ichor::LoadingScreen>(*ui);
        }
        if (!loading || !loading->ok()) {
            ichor::log(ichor::LogLevel::Warn, "the loading screen could not start; the forge runs without it");
            loading.reset();
            ui.reset();
        }
    }
    if (loading) {
        loading->setVisible(true);
        renderer.setUiHooks(
            [&](std::uint32_t width, std::uint32_t height) {
                ui->setSize(width, height);
                loading->setView(flow.loadingView());
                ui->update(platform.now());
            },
            [&] { ui->render(); });
    }
    ichor::logf(ichor::LogLevel::Info, "forging seed {} on a {} cell grid", options.seed, options.forgedGrid);
    ForgeOutcome outcome = ForgeOutcome::Built;
    while (flow.stage() == ichor::NewGameStage::Forging) {
        platform.pumpEvents();
        if (platform.quitRequested()) {
            flow.cancel();
            outcome = ForgeOutcome::Quit;
            break;
        }
        flow.update();
        if (loading) {
            renderer.beginFrame();
            renderer.submit(blank);
            renderer.endFrame();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    renderer.setUiHooks({}, {});
    loading.reset();
    ui.reset();
    if (outcome == ForgeOutcome::Quit) {
        return outcome;
    }
    if (flow.stage() != ichor::NewGameStage::Ready) {
        ichor::logf(ichor::LogLevel::Error, "the forge failed: {}", flow.error());
        return ForgeOutcome::Failed;
    }
    const std::unique_ptr<ichor::ForgeJob> job = flow.takeJob();
    if (!job || job->result() == nullptr) {
        ichor::log(ichor::LogLevel::Error, "the forge finished with no world");
        return ForgeOutcome::Failed;
    }
    out = ichor::buildForgedGame(*job->result(), desc);
    if (!out->ok()) {
        ichor::logf(ichor::LogLevel::Error, "the forged world could not be built: {}", out->error);
        return ForgeOutcome::Failed;
    }
    ichor::logf(ichor::LogLevel::Info, "forged world {:016x}, spawn at {:.0f}, {:.0f}, {:.0f} m{}", out->stateHash(), out->spawn.x, out->spawn.z, out->spawn.groundY, out->spawn.found ? "" : " (no gentle dry ground found)");
    return ForgeOutcome::Built;
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

    ichor::Palette palette = ichor::defaultPalette();
    {
        ichor::MaterialLimits limits;
        std::vector<std::string> problems;
        if (ichor::loadMaterialLimits(options.data / "materials" / "response.json", limits, problems)) {
            palette.response = limits;
        }
        for (const std::string& problem : problems) {
            ichor::logf(ichor::LogLevel::Warn, "material limits: {}", problem);
        }
        ichor::setActiveMaterialLimits(palette.response);
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
    // Mesh indices run across the bundles in upload order, which is what a MeshRef holds.
    std::vector<ichor::PickBounds> pickBounds;
    for (const ichor::Mesh& mesh : bundle.meshes) {
        pickBounds.push_back({mesh.boundsMin, mesh.boundsMax});
    }
    pickBounds.resize(armMesh);
    for (const ichor::Mesh& mesh : actor.meshes) {
        pickBounds.push_back({mesh.boundsMin, mesh.boundsMax});
    }
    ichor::SceneBundle splashSounds;
    splashSounds.sounds.push_back(ichor::makeSplashClip());
    splashSounds.sounds.push_back(ichor::makeRainClip());
    splashSounds.sounds.push_back(ichor::makeWindClip());
    audio.addBundle(splashSounds);

    // The forged world, if there is one: its splat layers go to the renderer
    // and its cells stream around the player. The arena sits on a plateau at
    // the world's centre, above the terrain under it.
    ichor::Heightfield standInHeightfield;
    glm::dvec3 anchor(0.0);
    std::unique_ptr<ichor::ForgedGame> forged;
    glm::dvec2 forgedLook(0.0, -30.0);
    if (options.forged) {
        const ForgeOutcome outcome = forgeGame(platform, renderer, options, ichor::kitReferenceView(bundle, 16.0f / 9.0f), forged);
        if (outcome != ForgeOutcome::Built) {
            return outcome == ForgeOutcome::Quit ? 0 : 1;
        }
        if (!renderer.setTerrainMaterials(ichor::forgeTerrainMaterials(forged->desc.seed))) {
            return 1;
        }
        if (!renderer.setVegetationMeshes(ichor::treeBundle(), ichor::vegetationUnblendedKinds())) {
            ichor::log(ichor::LogLevel::Warn, "the tree meshes could not be uploaded; the world has no trees");
        }
        anchor = {forged->spawn.x, forged->spawn.groundY + 0.5, forged->spawn.z};
        // The view looks 30 m toward the place the spawn was searched around, north when the spawn is on top of it.
        const glm::dvec2 toward(forged->spawn.faceX - forged->spawn.x, forged->spawn.faceZ - forged->spawn.z);
        forgedLook = glm::length(toward) < 8.0 ? glm::dvec2(0.0, -30.0) : glm::normalize(toward) * 30.0;
    } else if (!options.world.empty()) {
        ichor::SceneBundle layers;
        if (!ichor::readWorld(options.world, standInHeightfield) || !ichor::readBundle(options.world / "terrain.iscene", layers) || !renderer.setTerrainMaterials(layers)) {
            return 1;
        }
        // Trees are decoration: without them the world is bare, not broken.
        if (!renderer.setVegetationMeshes(ichor::treeBundle(), ichor::vegetationUnblendedKinds())) {
            ichor::log(ichor::LogLevel::Warn, "the tree meshes could not be uploaded; the world has no trees");
        }
        const double centre = standInHeightfield.desc().sizeMetres * 0.5;
        float plateau = -1e30f;
        for (int z = -12; z <= 12; z += 2) {
            for (int x = -12; x <= 12; x += 2) {
                plateau = std::max(plateau, standInHeightfield.heightAt(centre + x, centre + z));
            }
        }
        anchor = {centre, plateau + 0.5, centre};
    }
    const ichor::Heightfield& heightfield = forged ? forged->heightfield : standInHeightfield;
    // The roads, points of interest and buildings are grey-box meshes uploaded once; the view draws the ones in resident cells.
    ichor::ForgedScenery scenery;
    std::uint32_t sceneryBase = 0;
    bool sceneryDrawn = false;
    if (forged) {
        scenery = ichor::buildForgedScenery(*forged);
        sceneryBase = renderer.meshCount();
        sceneryDrawn = renderer.uploadBundle(scenery.bundle);
        if (!sceneryDrawn) {
            ichor::log(ichor::LogLevel::Warn, "the roads and placed buildings could not be uploaded; the world is drawn without them");
        }
    }
    ichor::StreamerConfig streaming;
    streaming.synchronous = options.headless; // a replay must see the same cells on the same ticks
    streaming.navmesh = !options.headless; // nothing consumes tiles on the null backends, and they are slow to build
    streaming.vegetation = !options.headless;
    streaming.water = !options.headless;
    streaming.rivers.maxLakeDepth = ichor::kPlayableLakeDepth; // deeper basins are the terrain's artefacts, not lakes
    // The world's water, worked out once and shared: the streamer draws it and the player wades
    // through it. Headless runs have it too, so the simulation is the same with or without a window.
    std::shared_ptr<const ichor::Hydrology> hydrology;
    std::unique_ptr<ichor::WaterField> standInWater;
    const ichor::WaterField* waterField = nullptr;
    if (forged) {
        // The forged world's own rivers and lakes, drawn and waded through with the parameters they were adapted with.
        streaming.rivers = forged->water.rivers;
        streaming.hydrology = forged->water.hydrology;
        streaming.biomes = forged->biomes;
        streaming.navSurface = forged->navSurface;
        streaming.pois = forged->pois;
        streaming.buildings = forged->buildings;
        waterField = forged->waterField.get();
    } else if (!heightfield.empty()) {
        hydrology = std::make_shared<const ichor::Hydrology>(ichor::loadOrComputeHydrology(options.world, heightfield));
        standInWater = std::make_unique<ichor::WaterField>(heightfield, hydrology, streaming.rivers, streaming.waterMesh);
        streaming.hydrology = hydrology;
        waterField = standInWater.get();
    }
    const auto ringSide = static_cast<std::uint32_t>(2 * (streaming.radiusCells + streaming.hysteresisCells) + 1);
    ichor::NavWorld navigation;
    if (!heightfield.empty() && !navigation.init(heightfield.desc(), ringSide * ringSide)) {
        return 1;
    }
    ichor::WorldStreamer streamer(heightfield, streaming);

    const std::uint32_t armClip = animations.findClip("wave");
    const std::uint32_t windupCue = audio.findSound("A_WindupLight");
    const std::uint32_t tellCue = audio.findSound("A_Tell");
    const std::uint32_t splashCue = audio.findSound("A_Splash");
    const std::uint32_t rainBed = audio.findSound("A_Rain");
    ichor::AmbienceLayers ambience;
    ambience.assign(ichor::AmbienceLayer::Weather, rainBed);
    // No biome beds exist yet, so the wind takes the bed layer.
    ambience.assign(ichor::AmbienceLayer::Bed, audio.findSound("A_Wind"));
    if (armClip == ichor::AnimationSet::kNoClip) {
        ichor::log(ichor::LogLevel::Error, "the actor bundle has no clip named wave");
        return 1;
    }

    ichor::EngineConfig config;
    config.seed = options.seed;
    ichor::Engine engine(platform, renderer, config);
    engine.scene().setAnimations(&animations);
    // The stand-in world has no roads, so its field is empty and the bonus never applies.
    const ichor::NullRoadField noRoads;
    const ichor::RoadField& roadField = forged ? static_cast<const ichor::RoadField&>(*forged->roads) : noRoads;
    int roadBonusPermille = ichor::kDefaultRoadBonusPermille;
    if (waterField) {
        engine.scene().setSpeedModifier([&](const glm::dvec3& position) {
            const float depth = waterField->at(position.x, position.z).depth;
            return static_cast<double>(ichor::wadingSpeedFactor(depth)) * ichor::roadSpeedFactor(roadField.coverageAt(position.x, position.z), depth, roadBonusPermille);
        });
    }
    engine.setAudio(&audio);
    // The player's splashes are heard, not simulated: this reads the scene and never writes it.
    if (waterField) {
        engine.scene().addSystem("wading_splash", [&, splash = ichor::WadingSplash{}](ichor::Scene& scene, double, const ichor::InputFrame&) mutable {
            for (auto [entity, transform, player] : scene.registry().view<const ichor::Transform, const ichor::Player>().each()) {
                const glm::dvec3& feet = transform.position;
                if (const auto cue = splash.step(feet.x, feet.z, waterField->at(feet.x, feet.z).depth)) {
                    audio.play(splashCue, feet, false, cue->gain);
                }
            }
        });
    }

    // Content scripts and the data layer. A broken script or data file is
    // logged and the game runs on without it; the watcher below reloads both.
    ichor::DataSet data;
    if (!data.loadDirectory(options.data)) {
        ichor::logf(ichor::LogLevel::Warn, "data: {}", data.lastError());
    }
    std::string roadNote;
    std::string roadError;
    if (!ichor::loadRoadBonus(data, roadBonusPermille, roadNote, roadError)) {
        ichor::logf(ichor::LogLevel::Warn, "data: {}", roadError);
    } else if (!roadNote.empty()) {
        ichor::logf(ichor::LogLevel::Warn, "data: {}", roadNote);
    }
    // The reverb of the places the listener walks into, from the "reverb" table of the data layer.
    std::optional<ichor::JournalTemplates> journalTemplates;
    const auto loadJournalTemplates = [&] {
        const ichor::DataTable* table = data.table("journal");
        std::string error = "there is no journal table";
        std::optional<ichor::JournalTemplates> loaded;
        if (table != nullptr) {
            loaded = ichor::JournalTemplates::fromTable(*table, error);
        }
        if (loaded) {
            journalTemplates = std::move(loaded);
        } else {
            ichor::logf(ichor::LogLevel::Warn, "data: journal prose templates not loaded: {}", error);
        }
    };
    loadJournalTemplates();
    ichor::ReverbField reverbField;
    if (!ichor::loadReverbStamps(data, reverbField)) {
        ichor::log(ichor::LogLevel::Warn, "data: a reverb stamp is malformed, so none were loaded");
    }

    // A campaign owns the world; its slots are snapshots inside it (doc 14).
    const std::uint64_t worldSeed = heightfield.empty() ? options.seed : heightfield.desc().seed;
    // Event-driven music: stingers for vistas, discoveries, combat and death, with silence between. The
    // game systems that raise those events call musicPlayer.trigger(event, seconds); none exist yet.
    std::vector<ichor::MusicCue> musicCues;
    if (!ichor::loadMusicCues(data, musicCues)) {
        ichor::log(ichor::LogLevel::Warn, "data: a music cue is malformed, so none were loaded");
    }
    ichor::MusicPlayer musicPlayer(std::move(musicCues), ichor::MusicConfig{}, worldSeed);
    audio.addBundle(musicPlayer.makeBundle());
    musicPlayer.bind(audio);

    constexpr std::uint32_t kGenVersion = 1;
    const auto saveContext = [&] { return ichor::SaveContext{worldSeed, kGenVersion, data.digest()}; };
    // Ironman keeps its own directory, so a normal run can never load its rolling slot.
    ichor::SaveSlots slots(options.saves / ((options.ironman ? "ironman-" : "campaign-") + std::to_string(worldSeed)) / "slots", ichor::SavePolicy{options.ironman, 3});
    // The player's settings live beside the campaigns, not in one, and never touch the simulation.
    const std::filesystem::path settingsPath = options.saves / "settings.cfg";
    ichor::Settings settings = ichor::loadSettings(settingsPath);
    ichor::Chronicle chronicle;
    const std::string quickSlot = options.ironman ? ichor::SaveSlots::kIronmanSlot : "quicksave";
    const std::string autoSlot = options.ironman ? ichor::SaveSlots::kIronmanSlot : "autosave";

    if (!options.loadSlot.empty()) {
        const ichor::LoadResult loaded = slots.load(options.loadSlot, engine.scene(), saveContext());
        if (loaded.status != ichor::LoadStatus::Loaded) {
            ichor::logf(ichor::LogLevel::Error, "slot '{}' did not load (status {})", options.loadSlot, static_cast<int>(loaded.status));
            return 1;
        }
        chronicle = loaded.chronicle;
        if (loaded.chronicleDamaged) {
            ichor::log(ichor::LogLevel::Warn, "the slot's chronicle was unreadable and starts empty");
        }
        ichor::logf(ichor::LogLevel::Info, "slot '{}' loaded at tick {}{}{}", options.loadSlot, loaded.info.tick, loaded.fromBackup ? ", from a backup" : "",
            loaded.dataChanged ? ", data has changed since it was saved" : "");
    } else if (!options.load.empty()) {
        if (!ichor::loadScene(options.load, engine.scene())) {
            return 1;
        }
    } else if (forged) {
        populateForged(engine.scene(), anchor);
    } else {
        populate(engine.scene(), bundle, armMesh, armClip, anchor);
    }

    struct ResidentCell {
        std::uint32_t handle = ichor::Renderer::kNoHandle;
        std::uint32_t grove = ichor::Renderer::kNoHandle; // the cell's trees
        std::uint32_t water = ichor::Renderer::kNoHandle; // the cell's rivers and lakes
        glm::dvec3 origin{0.0};
        std::vector<ichor::PlacedPoi> pois;
        std::vector<ichor::PlacedBuilding> buildings;
    };
    std::map<ichor::CellId, ResidentCell> cells;
    streamer.onLoaded([&](const ichor::CellPayload& payload) {
        ResidentCell cell;
        cell.origin = {heightfield.cellOriginX(payload.cell.id), 0.0, heightfield.cellOriginZ(payload.cell.id)};
        cell.handle = renderer.uploadTerrainCell(payload.lods);
        if (!payload.plants.empty()) {
            cell.grove = renderer.uploadVegetationCell(payload.plants);
        }
        cell.water = renderer.uploadWaterCell(payload.water);
        cell.pois = payload.pois;
        cell.buildings = payload.buildings;
        engine.scene().physics().addHeightField(ichor::StableId::of("cell", ichor::cellKey(payload.cell.id)), payload.cell.heights.data(), payload.cell.samplesPerSide, cell.origin,
            static_cast<float>(heightfield.desc().metresPerSample));
        cells[payload.cell.id] = cell;
        ichor::NavTile tile = payload.nav;
        if (!tile.empty() && !navigation.addTile(payload.cell.id, std::move(tile))) {
            ichor::logf(ichor::LogLevel::Warn, "navmesh: tile {},{} was not added", payload.cell.id.x, payload.cell.id.z);
        }
    });
    streamer.onUnloaded([&](ichor::CellId id) {
        if (const auto found = cells.find(id); found != cells.end()) {
            renderer.removeTerrainCell(found->second.handle);
            if (found->second.grove != ichor::Renderer::kNoHandle) {
                renderer.removeVegetationCell(found->second.grove);
            }
            renderer.removeWaterCell(found->second.water);
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
        engine.setOverlayBuilder([&](const ichor::Scene& scene, std::vector<std::string>& lines) {
            lines.push_back(std::format("cells {} resident, {} pending, {} nav tiles", streamer.resident().size(), streamer.pending(), navigation.tileCount()));
            if (forged) {
                const glm::dvec3 feet = playerPosition(scene);
                lines.push_back(std::format("region tier {} of {}", forged->tiers.tierAt(feet.x, feet.z), forged->tiers.tierCount));
            }
            const float hour = ichor::hourOfDay(scene.tickCount(), config.fixedDt, options.daySeconds, options.startHour);
            lines.push_back(std::format("time {:02}:{:02}", static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f)));
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

    ichor::ScriptHost scripts(options.seed);
    ichor::bindScene(scripts, engine.scene());
    ichor::bindData(scripts, data);
    ichor::bindChronicle(scripts, chronicle, engine.scene());
    ichor::bindJournal(scripts, chronicle, engine.scene(), journalTemplates);
    const auto weatherNow = [&] {
        return ichor::weatherAt(worldSeed, options.startHour + static_cast<double>(engine.scene().tickCount()) * config.fixedDt / options.daySeconds * 24.0);
    };
    // The hour is read from the tick, as the sky is, so a script sees what the player sees.
    ichor::bindWorld(scripts, {
        [&](double x, double z) { return waterField ? static_cast<double>(waterField->at(x, z).depth) : 0.0; },
        [&]() { return static_cast<double>(ichor::hourOfDay(engine.scene().tickCount(), config.fixedDt, options.daySeconds, options.startHour)); },
        [&]() { return static_cast<double>(weatherNow().overcast); },
        [&]() { return static_cast<double>(weatherNow().rain); },
    });
    const auto loadScript = [&] {
        if (!scripts.runFile(options.script)) {
            ichor::logf(ichor::LogLevel::Warn, "script: {}", scripts.lastError());
            return false;
        }
        return true;
    };
    if (loadScript() && scripts.has("on_start") && !scripts.call("on_start", {!options.load.empty() || !options.loadSlot.empty()})) {
        ichor::logf(ichor::LogLevel::Warn, "script on_start: {}", scripts.lastError());
    }
    // Logged when it starts failing or the message changes, not every tick, and
    // given up on after a run of failures until the next reload.
    constexpr int kMaxFailures = 60;
    std::string lastScriptError;
    int failures = 0;
    engine.scene().addSystem("script", [&](ichor::Scene& scene, double, const ichor::InputFrame&) {
        if (failures >= kMaxFailures || !scripts.has("on_tick")) {
            return;
        }
        if (scripts.call("on_tick", {static_cast<double>(scene.tickCount())})) {
            lastScriptError.clear();
            failures = 0;
            return;
        }
        if (scripts.lastError() != lastScriptError) {
            lastScriptError = scripts.lastError();
            ichor::logf(ichor::LogLevel::Warn, "script on_tick: {}", lastScriptError);
        }
        if (++failures == kMaxFailures) {
            ichor::log(ichor::LogLevel::Warn, "script on_tick disabled after repeated failures until the script reloads");
        }
    });
    // A reload mid-replay or mid-recording would change what the script asks
    // for from that tick on, and the recording keeps no record of the version.
    const bool reloadAllowed = options.replay.empty() && options.record.empty();
    if (!options.screenshot.empty() && (options.headless || options.frames == 0)) {
        ichor::log(ichor::LogLevel::Error, "--screenshot needs a window and a frame count: it saves the last of --frames N frames");
        return 1;
    }

    // The editor's edits are scene commands: they would diverge from a recording, and a
    // headless run has no window to show them in.
    const bool editing = options.editor && !options.headless && options.replay.empty() && options.record.empty();
    if (options.editor && !editing) {
        ichor::log(ichor::LogLevel::Warn, "the editor needs a window and neither recording nor replaying; it is off");
    }
    ichor::EditorSession editorSession(engine.scene());
    ichor::EditorEnvironment editorEnvironment;
    ichor::EditorPanels editorPanels(editorSession, editorEnvironment);
    if (editing) {
        renderer.setGuiHook([&] { editorPanels.draw(); });
    }
    if (!reloadAllowed) {
        ichor::log(ichor::LogLevel::Info, "hot reload of scripts and data is off while recording or replaying");
    }

    // The game UI draws through the window renderer; headless runs have none. The context goes
    // before the HUD so the HUD, which borrows it, goes first.
    std::unique_ptr<ichor::UiContext> ui;
    std::unique_ptr<ichor::Hud> hud;
    std::unique_ptr<ichor::Ledger> ledger;
    std::unique_ptr<ichor::JournalScreen> journalScreen;
    std::unique_ptr<ichor::CharacterScreen> characterScreen;
    std::unique_ptr<ichor::MapScreen> mapScreen;
    std::unique_ptr<ichor::LandmarkCompassScreen> landmarkCompass;
    std::unique_ptr<ichor::MapScreenBinding> mapBinding;
    ichor::MapAnnotationStore mapMarks;
    ichor::WaypointSet mapWaypoints;
    std::unique_ptr<ichor::PauseMenu> pauseMenu;
    std::unique_ptr<ichor::NewGameScreen> newGameScreen;
    std::unique_ptr<ichor::CombatHudScreen> combatHud;
    bool paused = false;
    float hudHeading = 0.0f;
    float hudHour = options.startHour;
    float hudWater = 0.0f;
    std::string hudGuide;
    // The app has no combatants yet, so the bar shows full health until the sim supplies one.
    float hudHealth = 100.0f;
    float hudMaxHealth = 100.0f;
    if (!options.headless && renderer.uiRenderInterface() != nullptr) {
        const ichor::Extent size = platform.drawableSize();
        ui = std::make_unique<ichor::UiContext>(*renderer.uiRenderInterface(), static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height));
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_CONTENT_DIR) / "fonts";
        bool fontsLoaded = ui->ok();
        for (const char* file : {"AlegreyaSans-Regular.ttf", "Cinzel.ttf"}) {
            fontsLoaded = fontsLoaded && ui->loadFontFace(fonts / file);
        }
        if (fontsLoaded) {
            const ichor::HudLayoutResult hudLayout = ichor::loadHudLayout(options.data / "ui" / "hud.json");
            for (const std::string& problem : hudLayout.problems) {
                ichor::logf(ichor::LogLevel::Warn, "the HUD layout is not used: {}", problem);
            }
            hud = std::make_unique<ichor::Hud>(*ui, hudLayout.layout);
            ledger = std::make_unique<ichor::Ledger>(*ui);
            journalScreen = std::make_unique<ichor::JournalScreen>(*ui);
            characterScreen = std::make_unique<ichor::CharacterScreen>(*ui);
            mapScreen = std::make_unique<ichor::MapScreen>(*ui);
            landmarkCompass = std::make_unique<ichor::LandmarkCompassScreen>(*ui);
            if (forged) {
                mapScreen->setLayerStack(forged->mapLayers);
            } else {
                mapScreen->setImage(ichor::renderMapImage(heightfield));
            }
            mapBinding = std::make_unique<ichor::MapScreenBinding>(*ui);
            pauseMenu = std::make_unique<ichor::PauseMenu>(*ui);
            pauseMenu->setFovDegrees(settings.fovDegrees);
            newGameScreen = std::make_unique<ichor::NewGameScreen>(*ui);
            combatHud = std::make_unique<ichor::CombatHudScreen>(*ui);
        }
        if (!fontsLoaded || !hud->ok() || !ledger->ok() || !journalScreen->ok() || !characterScreen->ok() || !mapScreen->ok() || !mapBinding->ok() || !landmarkCompass->ok() || !pauseMenu->ok() || !newGameScreen->ok()) {
            ichor::log(ichor::LogLevel::Warn, "the game UI could not start; the HUD is off");
            combatHud.reset();
            newGameScreen.reset();
            pauseMenu.reset();
            landmarkCompass.reset();
            mapBinding.reset();
            mapScreen.reset();
            characterScreen.reset();
            journalScreen.reset();
            ledger.reset();
            hud.reset();
            ui.reset();
        }
    }
    if (ui) {
        platform.setEscapeQuits(false); // Esc opens the pause menu instead
        if (options.showMap && !editing) {
            mapScreen->setVisible(true);
        }
        if (options.showNewGame && !editing) {
            ichor::AxiomSet axioms;
            if (!axioms.loadDirectory(options.data)) {
                ichor::log(ichor::LogLevel::Warn, "data: the axioms could not be loaded, so the new-game screen shows no titles");
            }
            std::vector<ichor::Axiom> pool;
            for (const auto& [id, axiom] : axioms.axioms()) {
                pool.push_back(axiom);
            }
            const ichor::SlotRollResult roll = ichor::rollSlots(worldSeed, pool);
            if (!roll.ok()) {
                ichor::log(ichor::LogLevel::Warn, "the world's axioms could not be rolled from the data");
            }
            newGameScreen->setView(ichor::buildNewGameView(worldSeed, roll, axioms));
            newGameScreen->setVisible(true);
        }
        // The simulation clock drives the UI, so a replay sees the same animations.
        renderer.setUiHooks(
            [&](std::uint32_t width, std::uint32_t height) {
                ui->setSize(width, height);
                hud->update(hudHeading, hudHour, hudWater, hudHealth, hudMaxHealth);
                hud->setGuide(hudGuide);
                {
                    // The only landmark the app knows is the compass target; z is south, the compass wants y north.
                    std::vector<ichor::NavLandmark> marks;
                    if (options.compassTarget) {
                        marks.push_back({"target", "the marked place", {static_cast<float>(options.compassTarget->first), static_cast<float>(-options.compassTarget->second)}});
                    }
                    const glm::dvec3 feet = playerPosition(engine.scene());
                    ichor::bindLandmarkCompass(*landmarkCompass, {static_cast<float>(feet.x), static_cast<float>(-feet.z)}, {std::sin(hudHeading), std::cos(hudHeading)}, marks);
                }
                combatHud->bind(ichor::buildCombatHudView({.health = hudHealth, .maxHealth = hudMaxHealth, .stamina = 100.0f, .maxStamina = 100.0f}));
                if (ledger->visible()) {
                    std::vector<ichor::LedgerRow> rows;
                    for (auto& line : chronicle.recent(config.fixedDt, ichor::Ledger::kMaxRows)) {
                        rows.push_back({std::move(line.time), std::move(line.category), std::move(line.text)});
                    }
                    ledger->setRows(rows);
                }
                if (journalScreen->visible()) {
                    ichor::bindJournalModel(*journalScreen, ichor::journalEntries(chronicle.entries(), config.fixedDt));
                }
                mapBinding->setVisible(mapScreen->visible());
                if (characterScreen->visible()) {
                    // The app has no progression state yet, so only health is known.
                    characterScreen->bind({{{ichor::CharacterStat::Health, hudHealth, hudMaxHealth}}, {}, {}, {}});
                }
                if (mapScreen->visible()) {
                    const glm::dvec3 feet = playerPosition(engine.scene());
                    mapScreen->setPlayer(ichor::mapPosition(heightfield.desc(), feet.x, feet.z), hudHeading);
                    mapBinding->bind(ichor::buildMapScreen({.viewport = {static_cast<float>(feet.x), static_cast<float>(-feet.z), 1.0f, static_cast<float>(width), static_cast<float>(height)}, .annotations = &mapMarks, .player = ichor::WaypointPosition{static_cast<float>(feet.x), static_cast<float>(-feet.z)}, .playerHeading = hudHeading, .waypoints = &mapWaypoints}));
                }
                ui->update(engine.scene().time());
            },
            [&] { ui->render(); });
    }

    const ichor::RenderView reference = ichor::kitReferenceView(bundle, 16.0f / 9.0f);
    ichor::PickCamera pickCamera;
    ichor::SurfaceWetness surfaceWetness;
    engine.setViewBuilder([&](const ichor::Scene& scene, double alpha, ichor::RenderView& view) {
        view.sunDirection = reference.sunDirection;
        view.lines = reference.lines;
        view.camera = reference.camera;
        view.camera.fovY = glm::radians(settings.fovDegrees);
        const float hour = editing && editorEnvironment.overrideTime ? editorEnvironment.hour : ichor::hourOfDay(scene.tickCount(), config.fixedDt, options.daySeconds, options.startHour);
        hudHour = hour;
        if (!heightfield.empty()) {
            view.camera.farPlane = 2500.0f;
            // Time of day follows the tick, so it needs no saved state and a replay sees the same sky.
            // Weather follows the hours since the world began, so it needs no saved state either.
            // The editor can pin either one.
            ichor::Atmosphere atmosphere = ichor::atmosphereAt(hour);
            ichor::applyWeather(editing && editorEnvironment.overrideWeather ? editorEnvironment.weather : ichor::weatherAt(worldSeed, options.startHour + static_cast<double>(scene.tickCount()) * config.fixedDt / options.daySeconds * 24.0), atmosphere);
            ichor::applyAtmosphere(atmosphere, view);
            view.wetness = surfaceWetness.update(atmosphere.rain, scene.tickCount(), config.fixedDt);
            ichor::applyPalette(palette, view);
            view.time = static_cast<float>(std::fmod(scene.time(), 3600.0));
            // The flag moves only the drawn water, a debugging aid; trees follow the world's own level.
            view.waterEnabled = options.waterLevel.has_value() || heightfield.desc().hasWater();
            view.waterLevel = options.waterLevel.value_or(heightfield.desc().waterLevel());
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
            const glm::dvec3 target = forged ? anchor + glm::dvec3(forgedLook.x, 1.1, forgedLook.y) : anchor + glm::dvec3(0.0, 0.55, 0.0);
            const glm::dvec3 feet = blend(previous, transform).first;
            const glm::dvec3 eye = feet + glm::dvec3(0.0, 1.6, 0.0);
            view.camera.position = eye;
            hudWater = waterField ? waterField->at(feet.x, feet.z).depth : 0.0f;
            if (options.compassTarget) {
                hudGuide = ichor::targetGuide(feet.x, feet.z, options.compassTarget->first, options.compassTarget->second);
            }
            view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        const double cellMetres = heightfield.desc().cellMetres;
        for (const auto& [id, cell] : cells) {
            if (cell.handle == ichor::Renderer::kNoHandle) continue;
            const glm::dvec3 centre = cell.origin + glm::dvec3(cellMetres * 0.5, 0.0, cellMetres * 0.5);
            const double distance = glm::length(glm::dvec2(centre.x - view.camera.position.x, centre.z - view.camera.position.z)) / cellMetres;
            const std::uint32_t lod = distance < 1.2 ? 0u : distance < 2.2 ? 1u : 2u;
            view.terrain.push_back({cell.handle, cell.origin, lod});
            if (cell.grove != ichor::Renderer::kNoHandle && distance < 2.2) {
                view.vegetation.push_back({cell.grove, cell.origin});
            }
            if (cell.water != ichor::Renderer::kNoHandle) {
                view.waterCells.push_back({cell.water, cell.origin});
            }
            if (!sceneryDrawn || distance >= 2.2) continue;
            if (const auto road = scenery.roadMesh.find(id); road != scenery.roadMesh.end()) {
                view.instances.push_back({sceneryBase + road->second, cell.origin});
            }
            for (const ichor::PlacedPoi& poi : cell.pois) {
                if (const auto mesh = scenery.poiMesh.find(poi.id); mesh != scenery.poiMesh.end()) {
                    view.instances.push_back({sceneryBase + mesh->second, {poi.x, static_cast<double>(poi.groundY), poi.z}});
                }
            }
            for (const ichor::PlacedBuilding& building : cell.buildings) {
                // The box is 1 m cubed from its base up; it sinks half a metre so a foundation never floats on a rise.
                constexpr float kSink = 0.5f;
                ichor::RenderInstance instance;
                instance.mesh = sceneryBase + scenery.buildingMesh;
                instance.position = {building.x, static_cast<double>(building.groundY) - kSink, building.z};
                instance.basis = glm::mat3(glm::vec3(2.0f * building.halfWidthM, 0.0f, 0.0f), glm::vec3(0.0f, building.heightM + kSink, 0.0f), glm::vec3(0.0f, 0.0f, 2.0f * building.halfDepthM));
                view.instances.push_back(instance);
            }
        }
        {
            // Looking down -Z is north, and the heading turns clockwise from it.
            const glm::vec3 forward = view.camera.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            hudHeading = ichor::headingOf(forward.x, forward.z);
        }
        pickCamera = {view.camera.position, view.camera.rotation, view.camera.fovY};
        std::vector<ichor::LineSegment> colliders;
        scene.physics().debugLines(colliders);
        for (const ichor::LineSegment& line : colliders) {
            view.lines.push_back({line.a, line.b, line.color});
        }
    });

    ichor::FileWatcher watcher;
    watcher.watch(options.scene);
    watcher.watchDirectory(ICHOR_SHADER_DIR, ".spv");
    watcher.watch(options.script);
    watcher.watchTree(options.data, ".json");

    double last = platform.now();
    ichor::ReverbSettings lastReverb;
    bool confirmHeld = false;
    bool quickSaveHeld = false;
    bool quickLoadHeld = false;
    bool journalHeld = false;
    bool notesHeld = false;
    bool characterHeld = false;
    bool mapHeld = false;
    bool menuHeld = false;
    bool pointerHeld = false;
    bool pickHeld = false;
    ichor::NavKeys navHeld;
    const std::uint64_t autosaveTicks = options.autosaveSeconds > 0.0 ? std::max<std::uint64_t>(1, static_cast<std::uint64_t>(options.autosaveSeconds / config.fixedDt)) : 0;
    std::uint64_t lastAutosave = engine.scene().tickCount();
    for (;;) {
        ZoneScopedN("frame");
        // Enter starts the light attack on the arm, with its wind-up and tell cues.
        const bool confirm = (platform.input().buttons & ichor::InputFrame::Confirm) != 0;
        if (confirm && !confirmHeld) {
            engine.scene().enqueue(ichor::PlayMontageCommand{ichor::StableId::of("arm"), armClip, 0.35f, 0.5f, 0.9f, windupCue, tellCue});
        }
        confirmHeld = confirm;
        const std::uint32_t buttons = platform.input().buttons;
        const bool quickSave = (buttons & ichor::InputFrame::QuickSave) != 0;
        const bool quickLoad = (buttons & ichor::InputFrame::QuickLoad) != 0;
        // The journal is only a view, so it works in a replay too.
        const bool menu = (buttons & ichor::InputFrame::Menu) != 0;
        if (menu && !menuHeld && pauseMenu) {
            if (mapScreen && mapScreen->visible()) {
                mapScreen->setVisible(false); // Esc first puts the map away
            } else if (newGameScreen && newGameScreen->visible()) {
                newGameScreen->setVisible(false);
            } else {
                paused = !paused;
                pauseMenu->setVisible(paused);
            }
        }
        menuHeld = menu;
        // A map in view holds the world still, as a pause does, so the player is not moved from behind it.
        const bool mapKey = (buttons & ichor::InputFrame::Map) != 0;
        if (mapKey && !mapHeld && mapScreen && !editing) {
            mapScreen->setVisible(!mapScreen->visible());
        }
        mapHeld = mapKey;
        const bool journal = (buttons & ichor::InputFrame::Journal) != 0;
        // Tab moves between the editor's fields, so there it does not open the journal.
        if (journal && !journalHeld && ledger && !editing) {
            ledger->setVisible(!ledger->visible());
        }
        journalHeld = journal;
        const bool notes = (buttons & ichor::InputFrame::Notes) != 0;
        if (notes && !notesHeld && journalScreen && !editing) {
            journalScreen->setVisible(!journalScreen->visible());
        }
        notesHeld = notes;
        const bool characterKey = (buttons & ichor::InputFrame::Character) != 0;
        if (characterKey && !characterHeld && characterScreen && !editing) {
            characterScreen->setVisible(!characterScreen->visible());
        }
        characterHeld = characterKey;
        // The pointer drives the game UI (the game itself does not use the mouse); the editor's
        // own windows have it while they are up. Away from the window, or under the editor, a
        // press the UI still holds is let go and nothing stays hovered.
        if (ui) {
            const ichor::Pointer pointer = platform.pointer();
            if (pointer.inside && !editing) {
                ui->mouseMove(pointer.x, pointer.y);
                if (pointer.primary != pointerHeld) {
                    ui->mouseButton(0, pointer.primary);
                    pointerHeld = pointer.primary;
                }
            } else {
                if (pointerHeld) {
                    ui->mouseButton(0, false);
                    pointerHeld = false;
                }
                ui->mouseLeave();
            }
            if (mapScreen->takeCloseRequest()) {
                mapScreen->setVisible(false);
            }
            if (newGameScreen->takeBegin()) {
                newGameScreen->setVisible(false);
            }
            if (journalScreen->takeCloseRequest()) {
                journalScreen->setVisible(false);
            }
            if (characterScreen->takeCloseRequest()) {
                characterScreen->setVisible(false);
            }
            if (ledger->takeCloseRequest()) {
                ledger->setVisible(false);
            }
            if (pauseMenu->takeResume()) {
                paused = false;
                pauseMenu->setVisible(false);
            }
            if (const int steps = pauseMenu->takeFovSteps(); steps != 0) {
                settings.fovDegrees += static_cast<float>(steps) * kFovStepDegrees;
                settings = settings.clamped();
                pauseMenu->setFovDegrees(settings.fovDegrees);
                if (!ichor::saveSettings(settingsPath, settings)) {
                    ichor::logf(ichor::LogLevel::Warn, "the settings could not be saved to {}", settingsPath.string());
                }
            }
            if (pauseMenu->takeQuit()) {
                platform.requestQuit();
            }
        }
        // A click in the editor picks what is under the cursor; one on empty space clears the pick.
        // A press that began on an editor window is not a pick, nor is the rest of its drag.
        if (editing) {
            const ichor::Pointer pointer = platform.pointer();
            if (pointer.primary && !pickHeld && pointer.inside && !editorPanels.wantsPointer()) {
                const ichor::Extent size = platform.drawableSize();
                const auto ray = ichor::pickRay({static_cast<double>(pointer.x), static_cast<double>(pointer.y)}, {static_cast<double>(size.width), static_cast<double>(size.height)}, pickCamera);
                editorSession.select(ray ? ichor::pickEntity(engine.scene(), *ray, pickBounds) : std::nullopt);
            }
            pickHeld = pointer.primary;
            // An arrow or page key moves the selection one step per press (shift: a long one),
            // unless a field has the keyboard.
            const ichor::NavKeys nav = platform.navKeys();
            const auto selected = editorSession.selection();
            const bool nudging = selected && !editorPanels.wantsKeyboard();
            platform.reserveArrowKeys(nudging);
            if (nudging) {
                const ichor::NudgeKeys pressed{.left = nav.left && !navHeld.left,
                    .right = nav.right && !navHeld.right,
                    .forward = nav.up && !navHeld.up,
                    .back = nav.down && !navHeld.down,
                    .up = nav.pageUp && !navHeld.pageUp,
                    .down = nav.pageDown && !navHeld.pageDown,
                    .fast = nav.shift};
                if (const auto offset = ichor::nudgeOffset(pressed, pickCamera)) {
                    editorSession.nudge(*selected, *offset);
                }
            }
            navHeld = nav;
        }
        // Saving over the player's quicksave while a replay drives the run would be a surprise.
        if (quickSave && !quickSaveHeld && options.replay.empty()) {
            if (slots.save(quickSlot, engine.scene(), saveContext(), chronicle)) {
                ichor::logf(ichor::LogLevel::Info, "saved '{}' at tick {}", quickSlot, engine.scene().tickCount());
            } else {
                ichor::logf(ichor::LogLevel::Warn, "save failed: {}", slots.lastError());
            }
        }
        // A restore mid-replay or mid-recording would diverge from the recording, and in ironman
        // a reload would undo a death, so loading waits for a live, unrecorded, non-ironman run.
        if (quickLoad && !quickLoadHeld && options.replay.empty() && options.record.empty() && !options.ironman) {
            const ichor::LoadResult loaded = slots.load(quickSlot, engine.scene(), saveContext());
            if (loaded.status == ichor::LoadStatus::Loaded) {
                // The restore cleared the physics world, terrain collision included, so the streamer starts over.
                streamer.clear();
                editorSession.reset(); // its history describes a scene that was just replaced
                chronicle = loaded.chronicle;
                lastAutosave = engine.scene().tickCount(); // the clock just moved, usually backwards
                ichor::logf(ichor::LogLevel::Info, "loaded '{}' at tick {}", quickSlot, loaded.info.tick);
            } else {
                ichor::logf(ichor::LogLevel::Warn, "load of '{}' failed (status {})", quickSlot, static_cast<int>(loaded.status));
            }
        }
        quickSaveHeld = quickSave;
        quickLoadHeld = quickLoad;
        const std::uint64_t tickNow = engine.scene().tickCount();
        if (autosaveTicks > 0 && (tickNow < lastAutosave || tickNow - lastAutosave >= autosaveTicks)) {
            lastAutosave = engine.scene().tickCount();
            if (!slots.save(autoSlot, engine.scene(), saveContext(), chronicle)) {
                ichor::logf(ichor::LogLevel::Warn, "autosave failed: {}", slots.lastError());
            }
        }
        const double now = platform.now();
        // A pause stops the simulation, and with it the clock, the weather and the walk, but not the picture.
        const double dt = options.headless ? config.fixedDt : paused || (mapScreen && mapScreen->visible()) || (newGameScreen && newGameScreen->visible()) ? 0.0 : now - last;
        last = now;
        // With a frame count, the screenshot is of the last frame, as the player would see it.
        if (!options.screenshot.empty() && options.frames != 0 && engine.frameCount() + 1 == options.frames) {
            renderer.requestScreenshot(options.screenshot);
        }
        if (!engine.frame(dt)) {
            break;
        }
        // The audio thread holds a lock while it renders, so only a change is worth taking it for.
        const ichor::ReverbSettings reverb = reverbField.at(playerPosition(engine.scene()));
        if (reverb != lastReverb) {
            audio.setReverb(reverb);
            lastReverb = reverb;
        }
        const ichor::Weather sky = weatherNow();
        const ichor::WeatherAudioTargets weatherAudio = ichor::weatherAudioTargets(sky.overcast, sky.rain);
        ambience.setGain(ichor::AmbienceLayer::Bed, weatherAudio.wind);
        ambience.setGain(ichor::AmbienceLayer::Weather, weatherAudio.rain);
        ambience.apply(audio);
        musicPlayer.update(static_cast<double>(engine.scene().tickCount()) * config.fixedDt, audio);
        if (editing) {
            // New objects appear a few metres in front of the player.
            editorEnvironment.spawnAt = playerPosition(engine.scene()) + glm::dvec3(0.0, 0.0, -3.0);
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
            bool scriptChanged = false;
            bool dataChanged = false;
            for (const auto& path : watcher.poll()) {
                shaders = shaders || path.extension() == ".spv";
                content = content || path == options.scene;
                scriptChanged = scriptChanged || path == options.script;
                dataChanged = dataChanged || path.extension() == ".json";
            }
            if (reloadAllowed && scriptChanged && loadScript()) {
                ichor::log(ichor::LogLevel::Info, "script reloaded");
                lastScriptError.clear();
                failures = 0;
            }
            if (reloadAllowed && dataChanged) {
                if (data.loadDirectory(options.data)) {
                    ichor::logf(ichor::LogLevel::Info, "data reloaded, digest {:016x}", data.digest());
                    loadJournalTemplates();
                    reverbField.clear();
                    if (!ichor::loadReverbStamps(data, reverbField)) {
                        ichor::log(ichor::LogLevel::Warn, "data: a reverb stamp is malformed, so none were loaded");
                    }
                } else {
                    ichor::logf(ichor::LogLevel::Warn, "data: {}", data.lastError());
                }
            }
            if (shaders) {
                renderer.reloadShaders();
            }
            if (content && ichor::readBundle(options.scene, bundle)) {
                renderer.uploadBundle(bundle);
            }
        }
    }

    renderer.setGuiHook({}); // the panels are about to go out of scope
    renderer.setUiHooks({}, {}); // and so are the UI, the HUD and the ledger
    if (!options.screenshot.empty() && renderer.screenshotPending()) {
        ichor::logf(ichor::LogLevel::Error, "the screenshot {} was asked for and not saved", options.screenshot);
        return 1;
    }
    if (!options.record.empty() && !engine.recordedInputs().save(options.record)) {
        return 1;
    }
    if (!options.save.empty() && !ichor::saveScene(options.save, engine.scene())) {
        return 1;
    }
    if (!options.exportChronicle.empty()) {
        const std::string text = chronicle.exportText(config.fixedDt);
        std::ofstream out(options.exportChronicle, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            ichor::logf(ichor::LogLevel::Error, "could not write the chronicle to {}", options.exportChronicle.string());
            return 1;
        }
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
        ichor::log(ichor::LogLevel::Info, "usage: ichor [--headless] [--frames N] [--seed S] [--scene file.iscene] [--world dir] [--record in.iinp] [--replay in.iinp] [--load save.isav] [--save save.isav] [--saves dir] [--load-slot name] [--autosave seconds] [--ironman] [--hour h] [--day-seconds s] [--water y] [--target x z] [--editor] [--map] [--new-game] [--forged [--grid N]] [--screenshot file.png]");
        return 2;
    }

    if (!options.forged && options.forgedGrid != ichor::kForgedDefaultGridSize) {
        ichor::log(ichor::LogLevel::Warn, "--grid only applies with --forged and is ignored");
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
