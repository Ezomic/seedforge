#include "exile_demo.h"

#include "ichor/anim/animation.h"
#include "ichor/anim/exile_avatar.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/audio/miniaudio_device.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/ambience_layers.h"
#include "ichor/combat_bridge/combat_bridge.h"
#include "ichor/audio/music_player.h"
#include "ichor/audio/rain_ambience.h"
#include "ichor/audio/weather_ambience.h"
#include "ichor/audio/reverb.h"
#include "ichor/audio/soundscape.h"
#include "ichor/audio/wading_splash.h"
#include "ichor/core/asset_roots.h"
#include "ichor/core/controls_overlay.h"
#include "ichor/core/file_watch.h"
#include "ichor/core/log.h"
#include "ichor/core/overlay_mode.h"
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
#include "ichor/ui/inventory_screen.h"
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
#include "ichor/gfx/walk_camera.h"
#include "ichor/gfx/vk_context.h"
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
#include "ichor/world/dungeon_entry.h"
#include "ichor/world/forge.h"
#include "ichor/world/forged_atmosphere.h"
#include "ichor/world/forged_campaign_hook.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/carry.h"
#include "ichor/world/forged_water_view.h"
#include "ichor/npc/schedule.h"
#include "ichor/ui/journal_screen_model.h"
#include "ichor/world/hydrology_cache.h"
#include "ichor/world/forged_walking.h"
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
#include <set>
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
    std::filesystem::path content = ICHOR_CONTENT_DIR; // cooked bundles and fonts; a packaged build points it beside the exe
    std::filesystem::path scene = std::filesystem::path(ICHOR_CONTENT_DIR) / "kit.iscene";
    std::filesystem::path actor = std::filesystem::path(ICHOR_CONTENT_DIR) / "arm.iscene";
    std::filesystem::path exile; // the art/exile folder; empty leaves the placeholder character out
    std::filesystem::path exileDefault = std::filesystem::path(ICHOR_DATA_DIR).parent_path() / "art" / "exile"; // the player's model in --forged when --exile is not given
    std::filesystem::path world = std::filesystem::path(ICHOR_CONTENT_DIR) / "world"; // empty runs without terrain
    std::filesystem::path kit = std::filesystem::path(ICHOR_DATA_DIR).parent_path() / "art" / "settlement_kit"; // the --forged settlements' textured pieces
    float overlook = 0.0f; // --settlement: look down on the settlement from this many metres up and back, instead of standing in it
    std::string waterView; // --forged: start on the bank of a large river or the biggest lake ("river" or "lake")
    std::optional<worldforge::BiomeId> biome; // --forged --biome NAME: start inside this biome (forest, desert, ...)
    int settlement = -1;   // --forged: start in this settlement (an index in the forge's order); -1 keeps the usual spawn
    int dungeon = -1;      // --forged: start at the entrance of this dungeon (an index among the dungeon POIs); -1 keeps the usual spawn
    bool enterDungeon = false; // --forged: go in as soon as an entrance is in reach, as if Enter were pressed
    std::filesystem::path script = std::filesystem::path(ICHOR_SCRIPT_DIR) / "main.lua";
    std::filesystem::path data = ICHOR_DATA_DIR;
    std::filesystem::path saves = "saves";
    std::filesystem::path exportChronicle; // written at exit when set
    std::string loadSlot;
    bool ironman = false;
    bool editor = false;
    bool showMap = false;
    int mapExplore = 0; // --forged --map: show the map as if the player had walked from the spawn through this many settlements
    bool showNewGame = false;
    bool standInAtmosphere = false; // a forged world keeps the stand-in sky and weather, for comparing
    bool noExilePlayer = false; // --forged walks as the Exile unless this is set, and then as the wire capsule from inside
    bool forged = false; // build the world from libworldforge instead of the stand-in terrain
    int lootIndex = -1;       // --forged: start beside this placed item (an index in the loot set's order)
    std::vector<std::string> give; // --forged: items granted at the start, as a reward would be (--give id,id)
    bool showInventory = false; // --inventory: start with the inventory screen open
    bool combat = false;      // --forged: one enemy at a settlement's outskirts and the stage0 fight with it (SEED-643)
    bool combatDuel = false;  // --combat: begin a few metres from the enemy instead of at the spawn
    std::string combatRole;   // --combat: the enemy's role, skirmisher (the default), anchor or artillery (SEED-661)
    std::string combatBot;    // --combat: a scripted player of this skill (novice, regular, expert) instead of the keyboard
    int forgedGrid = ichor::kForgedDefaultGridSize;
    std::string campaign; // --forged: keep the forged world in this campaign under --saves, and load it when it exists
    ichor::CampaignWorldPlan campaignPlan; // what main() decided for `campaign` before run()
    std::uint64_t frameStats = 0; // log the per-stage frame cost every this many frames; 0 is off
    bool rain = false; // --weather rain lets the seeded weather rain; off keeps the sky dry
    std::string screenshot; // where to save the last frame; empty is none. The window stays hidden and frames run unthrottled
    std::optional<ichor::OverlayMode> overlay; // --overlay [off|compact|full]; none is compact in play and off for a screenshot
    bool controls = false; // --controls: start with the F1 controls card open
    bool showWindow = false; // with --screenshot: a visible window, only to compare it with the hidden render
    float startHour = 9.0f;      // hour of the day when play starts
    std::optional<float> waterLevel; // world y of a sheet of water over the whole world; none is dry
    std::optional<std::pair<double, double>> compassTarget; // world x and z the compass points at; none shows no line
    float mouseSensitivity = 1.0f; // scales the right-button mouse look
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
        } else if (arg == "--mouse-sens" && hasValue) {
            if (!parseNumber(argv[++i], options.mouseSensitivity)) return false;
        } else if (arg == "--frames" && hasValue) {
            if (!parseNumber(argv[++i], options.frames)) return false;
        } else if (arg == "--seed" && hasValue) {
            if (!parseNumber(argv[++i], options.seed)) return false;
        } else if (arg == "--scene" && hasValue) {
            options.scene = argv[++i];
        } else if (arg == "--actor" && hasValue) {
            options.actor = argv[++i];
        } else if (arg == "--exile" && hasValue) {
            options.exile = argv[++i];
        } else if (arg == "--kit" && hasValue) {
            options.kit = argv[++i];
        } else if (arg == "--overlook" && hasValue) {
            if (!parseNumber(argv[++i], options.overlook) || !(options.overlook >= 0.0f) || options.overlook > 2000.0f) return false;
        } else if (arg == "--water-view" && hasValue) {
            options.waterView = argv[++i];
            if (options.waterView != "river" && options.waterView != "lake") return false;
        } else if (arg == "--biome" && hasValue) {
            options.biome = worldforge::biomeFromName(argv[++i]);
            if (!options.biome || options.settlement >= 0) return false;
        } else if (arg == "--loot" && hasValue) {
            if (!parseNumber(argv[++i], options.lootIndex) || options.lootIndex < 0) return false;
        } else if (arg == "--give" && hasValue) {
            for (std::string_view rest = argv[++i]; !rest.empty();) {
                const std::size_t comma = rest.find(',');
                options.give.emplace_back(rest.substr(0, comma));
                rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
            }
        } else if (arg == "--inventory") {
            options.showInventory = true;
        } else if (arg == "--dungeon" && hasValue) {
            if (!parseNumber(argv[++i], options.dungeon) || options.dungeon < 0) return false;
        } else if (arg == "--enter-dungeon") {
            options.enterDungeon = true;
        } else if (arg == "--settlement" && hasValue) {
            if (!parseNumber(argv[++i], options.settlement) || options.settlement < 0 || options.biome) return false;
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
        } else if (arg == "--overlay") {
            options.overlay = ichor::OverlayMode::Compact;
            if (i + 1 < argc) {
                if (const std::optional<ichor::OverlayMode> mode = ichor::parseOverlayMode(argv[i + 1])) {
                    options.overlay = mode;
                    ++i;
                }
            }
        } else if (arg == "--controls") {
            options.controls = true;
        } else if (arg == "--show-window") {
            options.showWindow = true;
        } else if (arg == "--screenshot" && hasValue) {
            options.screenshot = argv[++i];
        } else if (arg == "--frame-stats" && hasValue) {
            if (!parseNumber(argv[++i], options.frameStats)) return false;
        } else if (arg == "--weather" && hasValue) {
            const std::string_view mode = argv[++i];
            if (mode != "rain" && mode != "dry") return false;
            options.rain = mode == "rain";
        } else if (arg == "--editor") {
            options.editor = true;
        } else if (arg == "--map") {
            options.showMap = true;
        } else if (arg == "--map-explore" && hasValue) {
            if (!parseNumber(argv[++i], options.mapExplore) || options.mapExplore < 0) return false;
        } else if (arg == "--new-game") {
            options.showNewGame = true;
        } else if (arg == "--no-forged-atmosphere") {
            options.standInAtmosphere = true;
        } else if (arg == "--no-exile-player") {
            options.noExilePlayer = true;
        } else if (arg == "--forged") {
            options.forged = true;
        } else if (arg == "--combat") {
            options.combat = true;
        } else if (arg == "--combat-duel") {
            options.combatDuel = true;
        } else if (arg == "--combat-role" && hasValue) {
            options.combatRole = argv[++i];
            if (options.combatRole != "skirmisher" && options.combatRole != "anchor" && options.combatRole != "artillery") return false;
        } else if (arg == "--combat-bot" && hasValue) {
            options.combatBot = argv[++i];
            if (!ichor::stage0::findBotSkill(options.combatBot)) return false;
        } else if (arg == "--grid" && hasValue) {
            if (!parseNumber(argv[++i], options.forgedGrid) || options.forgedGrid < 16 || options.forgedGrid > 1024) return false;
        } else if (arg == "--campaign" && hasValue) {
            options.campaign = argv[++i];
            if (options.campaign.empty()) return false;
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

ForgeOutcome reportForgedGame(const ichor::ForgedGame& game)
{
    if (!game.ok()) {
        ichor::logf(ichor::LogLevel::Error, "the forged world could not be built: {}", game.error);
        return ForgeOutcome::Failed;
    }
    for (const std::string& problem : game.contentProblems) {
        ichor::logf(ichor::LogLevel::Warn, "forged content left out: {}", problem);
    }
    ichor::logf(ichor::LogLevel::Info, "forged content: {} items, {} cast, {} quests ({} refused), {} dungeon slots", game.loot ? game.loot->all().size() : 0, game.cast ? game.cast->all().size() : 0,
        game.quests ? game.quests->all().size() : 0, game.quests ? game.quests->refused().size() : 0, game.dungeons ? game.dungeons->poiIds().size() : 0);
    ichor::logf(ichor::LogLevel::Info, "forged world {:016x}, spawn at {:.0f}, {:.0f}, {:.0f} m{}", game.stateHash(), game.spawn.x, game.spawn.z, game.spawn.groundY, game.spawn.found ? "" : " (no gentle dry ground found)");
    return ForgeOutcome::Built;
}

// Forges the world for --forged through the new-game flow: a loading screen with the phase being
// forged when there is a window, a plain wait when there is none. Quitting mid-forge cancels the job
// and joins its thread before this returns.
ForgeOutcome forgeGame(ichor::Platform& platform, ichor::Renderer& renderer, const Options& options, const ichor::RenderView& blank, std::unique_ptr<ichor::ForgedGame>& out)
{
    ichor::WorldDesc desc;
    desc.seed = options.seed;
    // The manifest names the items, cast and dungeon themes the data layer has, so the forge is given them.
    ichor::DataSet contentData;
    if (!contentData.loadDirectory(options.data)) {
        ichor::logf(ichor::LogLevel::Warn, "data: {}", contentData.lastError());
    }
    ichor::ForgedContentSetup content = ichor::loadForgedContentSetup(contentData, options.data);
    for (const std::string& problem : content.problems) {
        ichor::logf(ichor::LogLevel::Warn, "forged content: {}", problem);
    }
    if (options.campaignPlan.action == ichor::CampaignWorldAction::Load) {
        out = ichor::buildStoredCampaignGame(options.campaignPlan, desc, content.inputs);
        return reportForgedGame(*out);
    }
    ichor::NewGameFlow flow(true, options.forgedGrid, content.config);
    flow.begin(desc);

    std::unique_ptr<ichor::UiContext> ui;
    std::unique_ptr<ichor::LoadingScreen> loading;
    if (!options.headless && renderer.uiRenderInterface() != nullptr) {
        const ichor::Extent size = platform.drawableSize();
        ui = std::make_unique<ichor::UiContext>(*renderer.uiRenderInterface(), static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height));
        const std::filesystem::path fonts = options.content / "fonts";
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
    out = ichor::buildForgedGame(*job->result(), desc, content.inputs);
    return reportForgedGame(*out);
}

// --campaign, before anything else runs: decide between forging anew and rebuilding the stored world. A stored world
// fixes the seed and grid for the whole run, so the engine and scripts see the campaign's seed, not the command line's.
bool planCampaign(Options& options)
{
    options.campaignPlan = ichor::planCampaignWorld(options.saves, options.campaign);
    const ichor::CampaignWorldPlan& plan = options.campaignPlan;
    if (plan.action == ichor::CampaignWorldAction::Refuse) {
        ichor::logf(ichor::LogLevel::Error, "{}", plan.message);
        return false;
    }
    if (plan.action != ichor::CampaignWorldAction::ForgeAndSave) {
        ichor::logf(plan.warn ? ichor::LogLevel::Warn : ichor::LogLevel::Info, "{}", plan.message);
        options.seed = plan.key.seed;
        options.forgedGrid = plan.key.gridSize;
    }
    return true;
}

// --campaign: after the forge, save the new world or hold the rebuilt one to what was stored.
bool finishCampaign(const Options& options, const ichor::ForgedGame& game)
{
    const ichor::CampaignWorldPlan& plan = options.campaignPlan;
    if (plan.action == ichor::CampaignWorldAction::Load) {
        ichor::logf(ichor::LogLevel::Info, "campaign '{}': built from the stored files, {} forges run", options.campaign, ichor::ForgeJob::forgesStarted());
        return true;
    }
    if (plan.action == ichor::CampaignWorldAction::Rebuild) {
        const std::string problem = ichor::verifyRebuiltWorld(plan, game);
        if (!problem.empty()) {
            ichor::logf(ichor::LogLevel::Error, "campaign '{}': {}", options.campaign, problem);
            return false;
        }
        ichor::logf(ichor::LogLevel::Info, "campaign '{}': the world matches the stored one", options.campaign);
        return true;
    }
    ichor::WorldDesc desc;
    desc.seed = options.seed;
    const ichor::CampaignStatus status = ichor::saveCampaignWorld(options.saves, options.campaign, desc, options.forgedGrid, game);
    if (status != ichor::CampaignStatus::Ok) {
        ichor::logf(ichor::LogLevel::Error, "campaign '{}': the world could not be saved ({})", options.campaign, ichor::campaignStatusText(status));
        return false;
    }
    ichor::logf(ichor::LogLevel::Info, "campaign '{}': the forged world is saved under {}", options.campaign, options.saves.string());
    return true;
}

// The "press F1" hint is for a person's first play: it shows until the marker file exists, which is written as it shows.
// A headless or screenshot run neither shows it nor writes the marker.
bool firstRunHint(const Options& options)
{
    if (options.headless || options.controls || !options.screenshot.empty()) {
        return false;
    }
    const std::filesystem::path marker = options.saves / ".controls_seen";
    std::error_code ec;
    if (std::filesystem::exists(marker, ec)) {
        return false;
    }
    std::filesystem::create_directories(options.saves, ec);
    std::ofstream(marker) << "seen\n";
    return true;
}

// A packaged build has its content, shaders, scripts, data and art beside the executable; point every default there.
void usePackagedRoots(Options& options, const ichor::AssetRoots& roots)
{
    options.content = roots.content;
    options.scene = roots.content / "kit.iscene";
    options.actor = roots.content / "arm.iscene";
    options.world = roots.content / "world";
    options.exileDefault = roots.art / "exile";
    options.saves = roots.content.parent_path() / "saves";
    options.kit = roots.art / "settlement_kit";
    options.script = roots.scripts / "main.lua";
    options.data = roots.data;
    ichor::setShaderDirectory(roots.shaders);
}

int run(ichor::Platform& platform,ichor::Renderer& renderer, ichor::AudioHost& device, const Options& options)
{
    const bool hidden = !options.screenshot.empty() && !options.showWindow;
    if (!platform.init({.hidden = hidden}) || !renderer.init()) {
        return 1;
    }
    if (!options.screenshot.empty()) {
        ichor::log(ichor::LogLevel::Info, hidden ? "screenshot: hidden window, fixed frame step, no pacing" : "screenshot: visible window, fixed frame step");
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
    ExileDemo exile;
    const bool exileByDefault = options.forged && !options.noExilePlayer && options.exile.empty();
    const std::filesystem::path exileFolder = exileByDefault ? options.exileDefault : options.exile;
    if (!exileFolder.empty() && !loadExileDemo(exileFolder, renderer, animations, exile)) {
        if (!exileByDefault) {
            return 1;
        }
        ichor::logf(ichor::LogLevel::Warn, "the Exile did not load from {} (git lfs pull --include=\"art/exile/*\"); the player stays the wire capsule", exileFolder.string());
        exile = ExileDemo{};
    }
    ichor::ExileAvatar exileAvatar;
    if (options.forged && !options.noExilePlayer && !exile.kit.bundles[0].meshes.empty() && !exileAvatar.prepare(exile.kit, exile.meshBase)) {
        ichor::log(ichor::LogLevel::Warn, "the Exile has no walking clips; the player stays the wire capsule");
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
    for (const ichor::SceneBundle& kit : exile.kit.bundles) {
        for (const ichor::Mesh& mesh : kit.meshes) {
            pickBounds.push_back({mesh.boundsMin, mesh.boundsMax});
        }
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
    bool waterViewed = false; // --water-view found a bank to stand on
    if (options.forged) {
        const ForgeOutcome outcome = forgeGame(platform, renderer, options, ichor::kitReferenceView(bundle, 16.0f / 9.0f), forged);
        if (outcome != ForgeOutcome::Built) {
            return outcome == ForgeOutcome::Quit ? 0 : 1;
        }
        if (!options.campaign.empty() && !finishCampaign(options, *forged)) {
            return 1;
        }
        if (!renderer.setTerrainMaterials(ichor::forgeTerrainMaterials(forged->desc.seed))) {
            return 1;
        }
        if (!renderer.setVegetationMeshes(ichor::treeBundle(), ichor::vegetationUnblendedKinds())) {
            ichor::log(ichor::LogLevel::Warn, "the tree meshes could not be uploaded; the world has no trees");
        }
        if (options.settlement >= 0 && !ichor::spawnAtSettlement(*forged, static_cast<std::size_t>(options.settlement))) {
            ichor::logf(ichor::LogLevel::Warn, "--settlement {} is past the forged world's {} settlements; the usual spawn is kept", options.settlement, forged->sites.size());
        }
        waterViewed = !options.waterView.empty() && ichor::spawnAtWater(*forged, options.waterView == "river" ? ichor::WaterViewKind::River : ichor::WaterViewKind::Lake);
        if (!options.waterView.empty() && !waterViewed) {
            ichor::logf(ichor::LogLevel::Warn, "--water-view {}: this world has no {} with a dry bank; the usual spawn is kept", options.waterView, options.waterView);
        }
        if (options.biome && !ichor::spawnInBiome(*forged, *options.biome)) {
            ichor::logf(ichor::LogLevel::Warn, "--biome {}: the forged world has no dry, gentle ground inside it; the usual spawn is kept", worldforge::kBiomeNames[static_cast<std::size_t>(*options.biome)]);
        }
        if (options.dungeon >= 0 && !ichor::spawnAtDungeon(*forged, static_cast<std::size_t>(options.dungeon))) {
            ichor::logf(ichor::LogLevel::Warn, "--dungeon {} has no entrance with open ground beside it; the usual spawn is kept", options.dungeon);
        }
        if (options.lootIndex >= 0) {
            if (forged->loot && static_cast<std::size_t>(options.lootIndex) < forged->loot->all().size()) {
                // A step beside and south of the item, facing north, so the item lies ahead and clear of the avatar.
                const ichor::PlacedLoot& item = forged->loot->all()[static_cast<std::size_t>(options.lootIndex)];
                forged->spawn = {true, item.x + 1.8, item.z + 1.0, forged->heightfield.heightAt(item.x + 1.8, item.z + 1.0), item.x + 1.8, item.z - 40.0};
            } else {
                ichor::logf(ichor::LogLevel::Warn, "--loot {} is past the forged world's items; the usual spawn is kept", options.lootIndex);
            }
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
    // SEED-643: the stage0 fight inside the forged world. Nothing below runs without --combat.
    std::unique_ptr<ichor::CombatBridge> combat;
    const bool restoring = !options.load.empty() || !options.loadSlot.empty();
    if (options.combat && forged && restoring) {
        ichor::log(ichor::LogLevel::Warn, "--combat starts a fresh fight and is ignored with --load and --load-slot");
    } else if (options.combat && forged) {
        const ichor::CombatSite site = ichor::findCombatSite(*forged);
        if (!site.found) {
            ichor::log(ichor::LogLevel::Error, "--combat: no clear ground was found beside a settlement for the enemy");
            return 1;
        }
        ichor::CombatBridgeConfig combatConfig;
        combatConfig.tuning.lightWindupCue = audio.findSound("A_WindupLight");
        combatConfig.tuning.heavyWindupCue = audio.findSound("A_WindupHeavy");
        combatConfig.tuning.tellCue = audio.findSound("A_Tell");
        combatConfig.legacyControls = !options.combatBot.empty();
        if (options.combatRole == "anchor") combatConfig.role = ichor::stage0::FoeRole::Anchor;
        else if (options.combatRole == "artillery") combatConfig.role = ichor::stage0::FoeRole::Artillery;
        combat = std::make_unique<ichor::CombatBridge>(*forged, site, combatConfig);
        if (!combat->loadVisuals(renderer, std::filesystem::path(ICHOR_DATA_DIR).parent_path() / "art")) {
            ichor::log(ichor::LogLevel::Warn, "--combat: the fighters' art did not load, so some of them are not drawn");
        }
        if (options.combatDuel) {
            anchor = site.duelStart + glm::dvec3(0.0, 0.5, 0.0);
        }
        ichor::logf(ichor::LogLevel::Info, "combat: enemy at {:.0f}, {:.0f} m beside settlement {}", site.enemyPost.x, site.enemyPost.z, site.settlement);
    }
    const ichor::Heightfield& heightfield = forged ? forged->heightfield : standInHeightfield;
    // The roads, points of interest and buildings are grey-box meshes uploaded once; the view draws the ones in resident cells.
    ichor::ForgedScenery scenery;
    std::set<std::string> bakedDungeons;
    std::unique_ptr<ichor::DungeonEntry> dungeonEntry;
    std::uint32_t sceneryBase = 0;
    bool sceneryDrawn = false;
    if (forged) {
        scenery = ichor::buildForgedScenery(*forged);
        if (!ichor::addSettlementKit(scenery, *forged, options.kit)) {
            ichor::logf(ichor::LogLevel::Warn, "the settlement kit could not be loaded from {}; the buildings are drawn as boxes", options.kit.string());
        }
        sceneryBase = renderer.meshCount();
        sceneryDrawn = renderer.uploadBundle(scenery.bundle);
        if (!sceneryDrawn) {
            ichor::log(ichor::LogLevel::Warn, "the roads and placed buildings could not be uploaded; the world is drawn without them");
        }
        const std::uint32_t dungeonKitBase = renderer.meshCount();
        if (renderer.uploadBundle(ichor::buildDungeonKit())) {
            dungeonEntry = std::make_unique<ichor::DungeonEntry>(*forged, dungeonKitBase, ichor::DungeonEntryParams{.autoEnter = options.enterDungeon});
        } else {
            ichor::log(ichor::LogLevel::Warn, "the dungeon kit could not be uploaded; dungeons cannot be entered");
        }
    }
    ichor::StreamerConfig streaming;
    streaming.synchronous = options.headless || !options.screenshot.empty(); // a replay or an unthrottled screenshot must see the same cells on the same frames
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
        streaming.scenery = ichor::forgedSceneryGuard(*forged);
        streaming.navSurface = forged->navSurface;
        streaming.pois = forged->pois;
        streaming.buildings = forged->buildings;
        streaming.loot = forged->loot;
        streaming.cast = forged->cast;
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
    engine.setOverlayMode(ichor::startingOverlayMode(!options.screenshot.empty(), options.overlay));
    engine.setControlsOverlay(ichor::startControlsOverlay(firstRunHint(options), options.controls));
    engine.scene().setAnimations(&animations);
    // The stand-in world has no roads, so its field is empty and the bonus never applies.
    const ichor::NullRoadField noRoads;
    const ichor::RoadField& roadField = forged ? static_cast<const ichor::RoadField&>(*forged->roads) : noRoads;
    int roadBonusPermille = ichor::kDefaultRoadBonusPermille;
    std::unique_ptr<ichor::CarrySystem> carry; // made below, once the data is loaded; the speed modifier reads it
    if (waterField) {
        engine.scene().setSpeedModifier([&](const glm::dvec3& position) {
            const double load = carry ? carry->speedFactor(engine.scene()) : 1.0;
            if (ichor::inDungeonPocket(position)) return load; // a dungeon is outside the world: no water or road there
            const float depth = waterField->at(position.x, position.z).depth;
            return load * static_cast<double>(ichor::wadingSpeedFactor(depth)) * ichor::roadSpeedFactor(roadField.coverageAt(position.x, position.z), depth, roadBonusPermille);
        });
    }
    // Walking rules for the forged world: ground too steep or water too deep to walk stops the player, and a player
    // who keeps trying to move without getting anywhere is reported. The report only reads the scene: a teleport out
    // of it would be state the replay and the snapshots cannot see.
    std::unique_ptr<ichor::ForgedWalking> walking;
    if (forged && waterField) {
        walking = std::make_unique<ichor::ForgedWalking>(heightfield, waterField, ichor::CapabilitySet{});
        engine.scene().setVelocityFilter([&](const glm::dvec3& position, const glm::dvec3& velocity) { return ichor::inDungeonPocket(position) ? velocity : walking->filterVelocity(position, velocity); });
        engine.scene().addSystem("walk_guard", [&, detector = ichor::StuckDetector{}](ichor::Scene& scene, double dt, const ichor::InputFrame& input) mutable {
            for (auto [entity, transform, player] : scene.registry().view<const ichor::Transform, const ichor::Player>().each()) {
                const glm::dvec3& feet = transform.position;
                if (ichor::inDungeonPocket(feet)) continue;
                const bool wantsToMove = input.moveX != 0.0f || input.moveY != 0.0f;
                const double probe = walking->params().probeMetres;
                const ichor::WalkAssessment ahead = walking->assess(feet.x + input.moveX * probe, feet.z - input.moveY * probe);
                const ichor::StuckReport stuck = detector.step(feet, wantsToMove, walking->assess(feet.x, feet.z), ahead.block, dt);
                if (stuck.stuck) {
                    ichor::logf(ichor::LogLevel::Warn, "walking: {}", ichor::describeStuck(stuck));
                }
            }
        });
    }
    engine.setAudio(&audio);
    // The player's splashes are heard, not simulated: this reads the scene and never writes it.
    if (waterField) {
        engine.scene().addSystem("wading_splash", [&, splash = ichor::WadingSplash{}](ichor::Scene& scene, double, const ichor::InputFrame&) mutable {
            for (auto [entity, transform, player] : scene.registry().view<const ichor::Transform, const ichor::Player>().each()) {
                const glm::dvec3& feet = transform.position;
                if (ichor::inDungeonPocket(feet)) continue;
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
    // With --campaign the slots live in the campaign's own folder, beside the world they belong to.
    const std::optional<ichor::Campaign> campaign = forged && !options.campaign.empty() ? ichor::Campaign::open(options.saves, options.campaign) : std::nullopt;
    const auto saveContext = [&] { return campaign ? campaign->slotContext(data.digest()) : ichor::SaveContext{worldSeed, kGenVersion, data.digest()}; };
    // Ironman keeps its own directory, so a normal run can never load its rolling slot.
    const ichor::SavePolicy savePolicy{options.ironman, 3};
    ichor::SaveSlots slots = campaign ? campaign->slots(savePolicy)
                                      : ichor::SaveSlots(options.saves / ((options.ironman ? "ironman-" : "campaign-") + std::to_string(worldSeed)) / "slots", savePolicy);
    // The player's settings live beside the campaigns, not in one, and never touch the simulation.
    const std::filesystem::path settingsPath = options.saves / "settings.cfg";
    ichor::Settings settings = ichor::loadSettings(settingsPath);
    ichor::Chronicle chronicle;
    const std::string quickSlot = options.ironman ? ichor::SaveSlots::kIronmanSlot : "quicksave";
    const std::string autoSlot = options.ironman ? ichor::SaveSlots::kIronmanSlot : "autosave";

    // What the player carries: registered before any slot loads, since its component is part of the snapshot.
    if (forged) {
        std::vector<std::string> carryProblems;
        carry = std::make_unique<ichor::CarrySystem>(forged->loot, ichor::CarryCatalogue::fromData(data, carryProblems));
        for (const std::string& problem : carryProblems) {
            ichor::logf(ichor::LogLevel::Warn, "carry: {}", problem);
        }
        carry->install(engine.scene());
    }

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
        for (const std::string& item : options.give) {
            carry->requestGrant(engine.scene(), item, 1);
        }
        if (combat) {
            combat->install(engine.scene(), ichor::StableId::of("player"), anchor);
            if (!options.combatBot.empty()) {
                combat->enableBot(*ichor::stage0::findBotSkill(options.combatBot), options.seed);
                engine.setInputSource([&combat](ichor::Scene& scene) { return combat->botInput(scene); });
            }
        }
    } else {
        populate(engine.scene(), bundle, armMesh, armClip, anchor);
        if (!options.exile.empty()) {
            spawnExileDemo(engine.scene(), exile, anchor + glm::dvec3(0.0, -0.45, 0.0));
        }
    }

    struct ResidentCell {
        std::uint32_t handle = ichor::Renderer::kNoHandle;
        std::uint32_t grove = ichor::Renderer::kNoHandle; // the cell's trees
        std::uint32_t water = ichor::Renderer::kNoHandle; // the cell's rivers and lakes
        glm::dvec3 origin{0.0};
        std::vector<ichor::PlacedPoi> pois;
        std::vector<ichor::PlacedBuilding> buildings;
        std::size_t plants = 0;
        std::vector<ichor::PlacedLoot> loot;
        std::vector<ichor::NpcSpawn> cast;
    };
    std::map<ichor::CellId, ResidentCell> cells;
    if (combat) {
        combat->setGroundCheck([&](glm::dvec3 where) { return cells.contains(heightfield.cellAt(where.x, where.z)); });
    }
    streamer.onLoaded([&](const ichor::CellPayload& payload) {
        ResidentCell cell;
        cell.origin = {heightfield.cellOriginX(payload.cell.id), 0.0, heightfield.cellOriginZ(payload.cell.id)};
        {
            ichor::FrameStages::Scope timer(engine.stages(), "up.terrain", true);
            cell.handle = renderer.uploadTerrainCell(payload.lods);
        }
        if (!payload.plants.empty()) {
            ichor::FrameStages::Scope timer(engine.stages(), "up.trees", true);
            cell.grove = renderer.uploadVegetationCell(payload.plants);
        }
        {
            ichor::FrameStages::Scope timer(engine.stages(), "up.water", true);
            cell.water = renderer.uploadWaterCell(payload.water);
        }
        cell.pois = payload.pois;
        cell.buildings = payload.buildings;
        cell.plants = payload.plants.size();
        cell.loot = payload.loot;
        cell.cast = payload.cast;
        {
            ichor::FrameStages::Scope timer(engine.stages(), "up.physics", true);
            engine.scene().physics().addHeightField(ichor::StableId::of("cell", ichor::cellKey(payload.cell.id)), payload.cell.heights.data(), payload.cell.samplesPerSide, cell.origin,
                static_cast<float>(heightfield.desc().metresPerSample));
        }
        cells[payload.cell.id] = cell;
        ichor::FrameStages::Scope navTimer(engine.stages(), "up.nav", true);
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
    // The forged world's soundscape: a bed per biome that crossfades along the borders, day and night variants, the
    // sound of a settlement near it, and footsteps by surface. Audio only reads the scene, so none of it is sim state.
    // A bed or step whose clip does not exist is silent.
    ichor::Soundscape soundscape;
    ichor::BedMixer bedMixer;
    std::vector<ichor::SettlementSoundType> settlementTypes;
    if (forged && forged->biomes) {
        std::string soundscapeError;
        if (!ichor::Soundscape::load(data, soundscape, soundscapeError)) {
            ichor::logf(ichor::LogLevel::Warn, "data: {}; the forged world stays silent", soundscapeError);
        }
        for (const ichor::SettlementSite& site : forged->sites) {
            settlementTypes.push_back(ichor::settlementTypeForBiome(worldforge::kBiomeNames[static_cast<std::size_t>(forged->biomes->biomeAt(site.x, site.z))]));
        }
        engine.scene().addSystem("footsteps", [&, pacer = ichor::FootstepPacer{}](ichor::Scene& scene, double, const ichor::InputFrame&) mutable {
            for (auto [entity, transform, player] : scene.registry().view<const ichor::Transform, const ichor::Player>().each()) {
                const glm::dvec3& feet = transform.position;
                if (!pacer.step(feet.x, feet.z)) {
                    continue;
                }
                const glm::vec3 splat = forged->biomes->splatAt(feet.x, feet.z);
                const ichor::SoundSurface surface = ichor::soundSurface(splat.x, splat.y, splat.z, roadField.coverageAt(feet.x, feet.z), waterField ? waterField->at(feet.x, feet.z).depth : 0.0f);
                // Wading has its own splash.
                if (surface == ichor::SoundSurface::Water) {
                    continue;
                }
                if (const auto step = ichor::footstepFor(soundscape, surface)) {
                    const std::uint32_t clip = audio.findSound(step->clip);
                    if (clip != ichor::Mixer::kNoSound) {
                        audio.play(clip, feet, false, step->gain);
                    }
                }
            }
        });
    }
    if (!heightfield.empty()) {
        // Streaming advances once per tick, so residency follows the path and
        // the budget alone. While the gate holds no ticks run, so it advances
        // the streamer itself until the required ring is in.
        if (dungeonEntry) {
            dungeonEntry->install(engine.scene());
        }
        // Inside a dungeon the player is outside the world: the cells around the entrance stay as they were.
        engine.scene().addSystem("streaming", [&](ichor::Scene& scene, double, const ichor::InputFrame&) {
            if (ichor::inDungeonPocket(playerPosition(scene))) return;
            ichor::FrameStages::Scope timer(engine.stages(), "stream", true);
            streamer.update(playerPosition(scene));
        });
        engine.setTickGate([&](const ichor::Scene& scene) {
            const glm::dvec3 position = playerPosition(scene);
            if (ichor::inDungeonPocket(position) || streamer.ready(position)) {
                return true;
            }
            ichor::FrameStages::Scope timer(engine.stages(), "stream", true);
            streamer.update(position);
            return streamer.ready(position);
        });
        engine.setOverlayBuilder([&](const ichor::Scene& scene, std::vector<std::string>& lines) {
            lines.push_back(std::format("cells {} resident, {} pending, {} nav tiles", streamer.resident().size(), streamer.pending(), navigation.tileCount()));
            if (forged) {
                const glm::dvec3 feet = playerPosition(scene);
                lines.push_back(std::format("region tier {} of {}", forged->tiers.tierAt(feet.x, feet.z), forged->tiers.tierCount));
                // A dungeon bakes its layout when the player comes within reach, once; the result is cached.
                constexpr double kApproachM = 80.0;
                if (forged->dungeons && forged->pois) {
                    for (const ichor::PlacedPoi& poi : forged->pois->all()) {
                        if (poi.kind != ichor::PlacedPoiKind::Dungeon || std::hypot(poi.x - feet.x, poi.z - feet.z) > kApproachM) continue;
                        const ichor::DungeonLayoutResult layout = forged->dungeons->layout(poi.id);
                        if (bakedDungeons.insert(poi.id).second) {
                            if (layout.ok()) {
                                ichor::logf(ichor::LogLevel::Info, "dungeon {} ({}) baked: skeleton {}, {} rooms", poi.id, poi.archetype, layout.dungeon->skeletonId, layout.dungeon->cells.size());
                            } else {
                                ichor::logf(ichor::LogLevel::Warn, "dungeon {} could not be baked: {}", poi.id, layout.error);
                            }
                        }
                        if (layout.ok()) {
                            lines.push_back(std::format("dungeon {}: {}, {} rooms", poi.id, layout.dungeon->skeletonId, layout.dungeon->cells.size()));
                        }
                        break;
                    }
                }
                if (forged->quests && !forged->quests->all().empty()) {
                    lines.push_back(std::format("tracking {} ({} quests; J lists them)", forged->quests->all().front().id, forged->quests->all().size()));
                }
            }
            const float hour = ichor::hourOfDay(scene.tickCount(), config.fixedDt, options.daySeconds, options.startHour);
            lines.push_back(std::format("time {:02}:{:02}", static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f)));
            if (combat) {
                combat->overlay(scene, lines);
            }
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
    const ichor::ForgedAtmosphereConfig forgedAtmosphere;
    const bool forgedSky = forged && !options.standInAtmosphere;
    const ichor::ForgedBiomeLook biomeLook = forgedSky && forged->biomes ? ichor::forgedBiomeLook(forged->biomes->biomeAt(forged->spawn.x, forged->spawn.z)) : ichor::ForgedBiomeLook{};
    const auto weatherAtTick = [&](std::uint64_t tick) {
        ichor::Weather weather = ichor::weatherAt(worldSeed, options.startHour + static_cast<double>(tick) * config.fixedDt / options.daySeconds * 24.0);
        if (!options.rain) {
            weather.rain = 0.0f;
        }
        return forgedSky ? ichor::forgedWeather(forgedAtmosphere, weather) : weather;
    };
    const auto weatherNow = [&] { return weatherAtTick(engine.scene().tickCount()); };
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
    std::unique_ptr<ichor::InventoryScreen> inventoryScreen;
    std::unique_ptr<ichor::MapScreen> mapScreen;
    std::unique_ptr<ichor::LandmarkCompassScreen> landmarkCompass;
    std::unique_ptr<ichor::MapScreenBinding> mapBinding;
    ichor::MapDiscovery mapFog(forged && !forged->mapLayers.empty() ? forged->mapLayers.base().width : ichor::kMapSide);
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
    // Without --combat the app has no combatants, so the bars show full until the sim supplies one.
    float hudHealth = 100.0f;
    float hudMaxHealth = 100.0f;
    float hudStamina = 100.0f;
    float hudMaxStamina = 100.0f;
    if (!options.headless && renderer.uiRenderInterface() != nullptr) {
        const ichor::Extent size = platform.drawableSize();
        ui = std::make_unique<ichor::UiContext>(*renderer.uiRenderInterface(), static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height));
        const std::filesystem::path fonts = options.content / "fonts";
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
            inventoryScreen = std::make_unique<ichor::InventoryScreen>(*ui);
            mapScreen = std::make_unique<ichor::MapScreen>(*ui);
            landmarkCompass = std::make_unique<ichor::LandmarkCompassScreen>(*ui);
            if (forged) {
                mapScreen->setLayerStack(forged->mapLayers);
                for (const ichor::MapLayer layer : {ichor::MapLayer::Biomes, ichor::MapLayer::Roads, ichor::MapLayer::Rivers}) {
                    mapScreen->setLayer(layer, true);
                }
                mapScreen->setSettlements(ichor::mapSettlements(forged->desc, forged->desc.seed, forged->sites));
                ichor::MapPoint from = ichor::mapPosition(forged->desc, forged->spawn.x, forged->spawn.z);
                for (std::size_t i = 0; i < static_cast<std::size_t>(options.mapExplore) && i < forged->sites.size(); ++i) {
                    const ichor::MapPoint to = ichor::mapPosition(forged->desc, forged->sites[i].x, forged->sites[i].z);
                    mapFog.revealSegment(from, to, ichor::kMapRevealRadius);
                    from = to;
                }
            } else {
                mapScreen->setImage(ichor::renderMapImage(heightfield));
            }
            mapBinding = std::make_unique<ichor::MapScreenBinding>(*ui);
            pauseMenu = std::make_unique<ichor::PauseMenu>(*ui);
            pauseMenu->setFovDegrees(settings.fovDegrees);
            newGameScreen = std::make_unique<ichor::NewGameScreen>(*ui);
            combatHud = std::make_unique<ichor::CombatHudScreen>(*ui);
        }
        if (!fontsLoaded || !hud->ok() || !ledger->ok() || !journalScreen->ok() || !characterScreen->ok() || !inventoryScreen->ok() || !mapScreen->ok() || !mapBinding->ok() || !landmarkCompass->ok() || !pauseMenu->ok() || !newGameScreen->ok()) {
            ichor::log(ichor::LogLevel::Warn, "the game UI could not start; the HUD is off");
            combatHud.reset();
            newGameScreen.reset();
            pauseMenu.reset();
            landmarkCompass.reset();
            mapBinding.reset();
            mapScreen.reset();
            inventoryScreen.reset();
            characterScreen.reset();
            journalScreen.reset();
            ledger.reset();
            hud.reset();
            ui.reset();
        }
    }
    if (ui) {
        platform.setEscapeQuits(false); // Esc opens the pause menu instead
        platform.enableLook(true);
        platform.setMouseSensitivity(options.mouseSensitivity);
        if (options.showInventory && carry && !editing) {
            inventoryScreen->setVisible(true);
        }
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
                if (combat) {
                    const ichor::CombatHudState fighter = combat->hud(engine.scene());
                    hudHealth = fighter.health;
                    hudMaxHealth = fighter.maxHealth;
                    hudStamina = fighter.stamina;
                    hudMaxStamina = fighter.maxStamina;
                }
                combatHud->bind(ichor::buildCombatHudView({.health = hudHealth, .maxHealth = hudMaxHealth, .stamina = hudStamina, .maxStamina = hudMaxStamina}));
                if (ledger->visible()) {
                    std::vector<ichor::LedgerRow> rows;
                    for (auto& line : chronicle.recent(config.fixedDt, ichor::Ledger::kMaxRows)) {
                        rows.push_back({std::move(line.time), std::move(line.category), std::move(line.text)});
                    }
                    ledger->setRows(rows);
                }
                if (journalScreen->visible()) {
                    std::vector<ichor::JournalModelEntry> entries = ichor::journalEntries(chronicle.entries(), config.fixedDt);
                    if (forged && forged->quests) {
                        // The forged world's quests, as prose lines the journal lists; the first is the one tracked.
                        for (const ichor::QuestInstance& quest : forged->quests->all()) {
                            entries.push_back({0, "quest", std::string(ichor::kJournalCategory),
                                std::format("{} ({}): {} between {} and {}, {} beats.", quest.id, quest.id == forged->quests->all().front().id ? "tracked" : "open", quest.tensionKind, quest.poiA, quest.poiB, quest.beats.size()),
                                static_cast<std::uint8_t>(quest.intensity)});
                        }
                    }
                    ichor::bindJournalModel(*journalScreen, entries);
                }
                mapBinding->setVisible(mapScreen->visible());
                if (inventoryScreen->visible() && carry) {
                    inventoryScreen->bind(ichor::buildInventoryScreen(carry->view(engine.scene()), carry->notice(engine.scene())));
                }
                if (characterScreen->visible()) {
                    // The app has no progression state yet, so only health is known.
                    characterScreen->bind({{{ichor::CharacterStat::Health, hudHealth, hudMaxHealth}}, {}, {}, {}});
                }
                if (forged) {
                    const glm::dvec3 walker = playerPosition(engine.scene());
                    mapFog.reveal(ichor::mapPosition(forged->desc, walker.x, walker.z), ichor::kMapRevealRadius);
                    if (mapScreen->visible()) {
                        mapScreen->setDiscovery(mapFog);
                    }
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
    // A forged world starts facing the place the spawn was searched around.
    float viewYaw = forged ? static_cast<float>(std::atan2(forgedLook.x, -forgedLook.y)) : 0.0f;
    exileAvatar.face(viewYaw);
    engine.scene().addSystem("walk_look", [&](ichor::Scene& scene, double dt, const ichor::InputFrame& input) {
        if (combat && combat->live()) return; // the fight steers and aims the camera itself
        viewYaw = ichor::nextWalkYaw(viewYaw, input.lookX, dt);
        for (auto [entity, player, body] : scene.registry().view<const ichor::Player, ichor::CharacterBody>().each()) {
            body.intent = ichor::walkRelativeIntent(body.intent, viewYaw);
        }
    });
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
            ichor::Atmosphere atmosphere = ichor::atmosphereAt(hour, forgedSky ? forgedAtmosphere.sunPath : ichor::SunPath{});
            ichor::applyWeather(editing && editorEnvironment.overrideWeather ? editorEnvironment.weather : weatherAtTick(scene.tickCount()), atmosphere);
            if (forgedSky) {
                ichor::applyForgedAtmosphere(forgedAtmosphere, biomeLook, atmosphere);
            }
            ichor::applyAtmosphere(atmosphere, view);
            view.wetness = surfaceWetness.update(atmosphere.rain, scene.tickCount(), config.fixedDt);
            ichor::applyPalette(palette, view);
            if (forgedSky) {
                view.groundColor = biomeLook.ground;
            }
            view.time = static_cast<float>(std::fmod(scene.time(), 3600.0));
            // The flag moves only the drawn water, a debugging aid; trees follow the world's own level.
            view.waterEnabled = options.waterLevel.has_value() || heightfield.desc().hasWater();
            view.waterLevel = options.waterLevel.value_or(heightfield.desc().waterLevel());
            view.forgedWaterLook = forged != nullptr;
        }
        const bool inDungeon = dungeonEntry && ichor::inDungeonPocket(playerPosition(scene));
        if (inDungeon) {
            ichor::applyDungeonInterior(view);
            dungeonEntry->appendInstances(view);
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
            glm::dvec3 feet = blend(previous, transform).first;
            glm::dvec3 eye = feet + glm::dvec3(0.0, 1.6, 0.0);
            if (exileAvatar.loaded() && !(combat && combat->live())) {
                // The Exile walks in front of a camera behind it; the avatar is drawn only, so the sim never sees it.
                feet = exileAvatar.appendPlayer(previous.position, transform.position, alpha, config.fixedDt, scene.time() + alpha * config.fixedDt, heightfield.heightAt(feet.x, feet.z), view);
                eye = ichor::walkFollowPosition(feet, viewYaw);
                eye.y = ichor::followClearY(eye.y, heightfield.heightAt(eye.x, eye.z));
            }
            view.camera.position = eye;
            hudWater = waterField ? waterField->at(feet.x, feet.z).depth : 0.0f;
            if (options.compassTarget) {
                hudGuide = ichor::targetGuide(feet.x, feet.z, options.compassTarget->first, options.compassTarget->second);
            }
            view.camera.rotation = ichor::walkCameraRotation(viewYaw);
            if (forged && options.overlook > 0.0f && waterViewed) {
                const glm::dvec3 target(forged->spawn.faceX, forged->heightfield.heightAt(forged->spawn.faceX, forged->spawn.faceZ), forged->spawn.faceZ);
                view.camera.position = glm::dvec3(forged->spawn.x, forged->spawn.groundY + options.overlook, forged->spawn.z);
                view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - view.camera.position)), glm::vec3(0.0f, 1.0f, 0.0f));
            }
            const bool overlookSettlement = forged && options.settlement >= 0 && static_cast<std::size_t>(options.settlement) < forged->sites.size();
            if (forged && options.overlook > 0.0f && (overlookSettlement || options.biome)) {
                const double lookX = overlookSettlement ? forged->sites[static_cast<std::size_t>(options.settlement)].x : forged->spawn.x;
                const double lookZ = overlookSettlement ? forged->sites[static_cast<std::size_t>(options.settlement)].z : forged->spawn.z;
                const glm::dvec3 centre(lookX, forged->heightfield.heightAt(lookX, lookZ), lookZ);
                view.camera.position = centre + glm::dvec3(0.0, options.overlook, options.overlook * 2.0);
                view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(centre - view.camera.position)), glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }
        if (combat && !inDungeon) {
            combat->buildView(scene, alpha, view);
        }
        const double cellMetres = heightfield.desc().cellMetres;
        const ichor::Carried* carried = carry ? carry->carried(scene) : nullptr;
        for (const auto& [id, cell] : cells) {
            if (inDungeon || cell.handle == ichor::Renderer::kNoHandle) continue;
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
            for (const ichor::PlacedLoot& item : cell.loot) {
                if (ichor::CarrySystem::isTaken(carried, item.itemId)) continue;
                ichor::RenderInstance instance;
                instance.mesh = sceneryBase + scenery.lootMesh;
                instance.position = {item.x, static_cast<double>(item.groundY), item.z};
                instance.basis = glm::mat3(glm::vec3(0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.0f, 0.0f, 0.5f));
                view.instances.push_back(instance);
            }
            for (const ichor::NpcSpawn& npc : cell.cast) {
                // Whoever is asleep by their schedule is indoors and not drawn.
                const auto state = ichor::stateAt(npc.schedule, hour);
                if (state && *state == ichor::ScheduleState::Sleep) continue;
                ichor::RenderInstance instance;
                instance.mesh = sceneryBase + scenery.castMesh;
                instance.position = {npc.x, static_cast<double>(npc.groundY), npc.z};
                instance.basis = glm::mat3(glm::vec3(0.6f, 0.0f, 0.0f), glm::vec3(0.0f, 1.8f, 0.0f), glm::vec3(0.0f, 0.0f, 0.6f));
                view.instances.push_back(instance);
            }
            if (scenery.kitLoaded) {
                for (const ichor::KitInstance& piece : forged->kit->inCell(id)) {
                    ichor::RenderInstance instance;
                    instance.mesh = sceneryBase + scenery.kitMesh.mesh[static_cast<std::size_t>(piece.piece)];
                    instance.position = {piece.x, static_cast<double>(piece.groundY), piece.z};
                    instance.basis = ichor::kitBasis(piece.quarterTurns);
                    view.instances.push_back(instance);
                }
            }
            for (const ichor::PlacedBuilding& building : cell.buildings) {
                if (scenery.kitLoaded) break; // the kit pieces above stand in for the boxes
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
        if (!inDungeon && !exileAvatar.loaded()) {
            scene.physics().debugLines(colliders);
        }
        for (const ichor::LineSegment& line : colliders) {
            view.lines.push_back({line.a, line.b, line.color});
        }
        if (carried != nullptr && sceneryDrawn && !inDungeon) {
            for (const ichor::CarryDrop& drop : carried->dropped) {
                if (!std::isfinite(drop.x) || !std::isfinite(drop.z)) continue;
                ichor::RenderInstance instance;
                instance.mesh = sceneryBase + scenery.lootMesh;
                instance.position = {drop.x, static_cast<double>(heightfield.heightAt(drop.x, drop.z)), drop.z};
                instance.basis = glm::mat3(glm::vec3(0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.0f, 0.0f, 0.5f));
                view.instances.push_back(instance);
            }
        }
        if (dungeonEntry) {
            ichor::applyFade(view, dungeonEntry->fade(scene));
            if (const std::string prompt = dungeonEntry->prompt(scene); !prompt.empty()) {
                hudGuide = prompt;
            } else if (!options.compassTarget) {
                hudGuide.clear();
            }
            if (const std::optional<double> yaw = dungeonEntry->takeViewYaw()) {
                viewYaw = static_cast<float>(*yaw);
            }
        }
        if (carry && !inDungeon && (!dungeonEntry || dungeonEntry->prompt(scene).empty())) {
            if (const std::string prompt = carry->prompt(scene); !prompt.empty()) {
                hudGuide = prompt;
            }
        }
    });

    ichor::FileWatcher watcher;
    watcher.watch(options.scene);
    watcher.watchDirectory(ichor::shaderDirectory(), ".spv");
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
    bool inventoryHeld = false;
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
            if (inventoryScreen && inventoryScreen->visible()) {
                inventoryScreen->setVisible(false);
            } else if (mapScreen && mapScreen->visible()) {
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
        const bool inventoryKey = (buttons & ichor::InputFrame::Inventory) != 0;
        if (inventoryKey && !inventoryHeld && inventoryScreen && carry && !editing) {
            inventoryScreen->setVisible(!inventoryScreen->visible());
        }
        inventoryHeld = inventoryKey;
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
            if (inventoryScreen->takeCloseRequest()) {
                inventoryScreen->setVisible(false);
            }
            for (const ichor::InventoryAction& action : inventoryScreen->takeActions()) {
                if (!carry) continue;
                if (action.kind == ichor::InventoryAction::Kind::Use) {
                    carry->requestUse(engine.scene(), action.itemId);
                } else {
                    carry->requestDrop(engine.scene(), action.itemId);
                }
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
        const double dt = options.headless || !options.screenshot.empty() ? config.fixedDt : paused || (mapScreen && mapScreen->visible()) || (newGameScreen && newGameScreen->visible()) ? 0.0 : now - last;
        last = now;
        // With a frame count, the screenshot is of the last frame, as the player would see it.
        if (!options.screenshot.empty() && options.frames != 0 && engine.frameCount() + 1 == options.frames) {
            renderer.requestScreenshot(options.screenshot);
        }
        if (!engine.frame(dt)) {
            break;
        }
        if (options.frameStats != 0 && engine.frameCount() % options.frameStats == 0) {
            std::size_t plantTotal = 0;
            for (const auto& [id, cell] : cells) plantTotal += cell.plants;
            ichor::logf(ichor::LogLevel::Info, "frame {} cells {} plants {} stages (mean/peak ms): {}", engine.frameCount(), cells.size(), plantTotal, engine.stages().summary());
            engine.stages().reset();
        }
        // The audio thread holds a lock while it renders, so only a change is worth taking it for.
        const ichor::ReverbSettings reverb = reverbField.at(playerPosition(engine.scene()));
        if (reverb != lastReverb) {
            audio.setReverb(reverb);
            lastReverb = reverb;
        }
        const ichor::Weather sky = weatherNow();
        const ichor::WeatherAudioTargets weatherAudio = ichor::weatherAudioTargets(sky.overcast, sky.rain);
        if (forged && forged->biomes && !soundscape.empty()) {
            const glm::dvec3 feet = playerPosition(engine.scene());
            const float hour = ichor::hourOfDay(engine.scene().tickCount(), config.fixedDt, options.daySeconds, options.startHour);
            const ichor::BiomeBlend here = forged->biomes->blendAt(feet.x, feet.z);
            std::vector<ichor::BedGain> beds = ichor::blendedBeds(soundscape, worldforge::kBiomeNames[static_cast<std::size_t>(here.primary)], worldforge::kBiomeNames[static_cast<std::size_t>(here.secondary)], here.weight, hour);
            ichor::addBeds(beds, {{"A_Wind", weatherAudio.wind}, {"A_Rain", weatherAudio.rain}});
            for (std::size_t i = 0; i < forged->sites.size(); ++i) {
                const ichor::SettlementSite& site = forged->sites[i];
                const float distance = static_cast<float>(std::hypot(feet.x - site.x, feet.z - site.z));
                if (!(distance < ichor::kSettlementMaxRangeM)) {
                    continue;
                }
                const float size = static_cast<float>(std::min<std::size_t>(site.tier, ichor::kSettlementTierCount - 1)) / static_cast<float>(ichor::kSettlementTierCount - 1);
                if (const auto bed = ichor::settlementBed(soundscape, site.id, size, settlementTypes[i], distance, 0.0f, 0.0f)) {
                    ichor::addBeds(beds, {*bed});
                }
            }
            ichor::budgetBeds(beds);
            for (const ichor::BedGain& change : bedMixer.step(beds, dt)) {
                const std::uint32_t clip = audio.findSound(change.clip);
                if (clip != ichor::Mixer::kNoSound) {
                    audio.setAmbience(clip, change.gain);
                }
            }
        } else {
            ambience.setGain(ichor::AmbienceLayer::Bed, weatherAudio.wind);
            ambience.setGain(ichor::AmbienceLayer::Weather, weatherAudio.rain);
            ambience.apply(audio);
        }
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
    if (const std::optional<ichor::AssetRoots> roots = ichor::packagedAssetRoots(ichor::executableDirectory())) {
        usePackagedRoots(options, *roots);
        ichor::logf(ichor::LogLevel::Info, "packaged build: assets are read from {}", ichor::executableDirectory().string());
    }
    if (!parse(argc, argv, options)) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor [--headless] [--frames N] [--seed S] [--scene file.iscene] [--world dir] [--exile art/exile] [--kit art/settlement_kit] [--water-view river|lake] [--settlement N | --biome NAME] [--overlook M] [--dungeon N [--enter-dungeon]] [--record in.iinp] [--replay in.iinp] [--load save.isav] [--save save.isav] [--saves dir] [--load-slot name] [--autosave seconds] [--ironman] [--hour h] [--day-seconds s] [--water y] [--target x z] [--editor] [--map [--map-explore N]] [--new-game] [--forged [--grid N] [--campaign NAME] [--no-exile-player] [--combat [--combat-duel] [--combat-role skirmisher|anchor|artillery] [--combat-bot novice|regular|expert]]] [--overlay [off|compact|full]] [--loot N] [--give id,id] [--inventory] [--controls] [--screenshot file.png [--show-window]]");
        return 2;
    }

    if (!options.forged && options.forgedGrid != ichor::kForgedDefaultGridSize) {
        ichor::log(ichor::LogLevel::Warn, "--grid only applies with --forged and is ignored");
    }
    if (!options.forged && !options.campaign.empty()) {
        ichor::log(ichor::LogLevel::Warn, "--campaign only applies with --forged and is ignored");
    }
    if (!options.forged && (options.combat || options.combatDuel || !options.combatBot.empty())) {
        ichor::log(ichor::LogLevel::Warn, "--combat only applies with --forged and is ignored");
    }
    if (!options.combat && (options.combatDuel || !options.combatBot.empty() || !options.combatRole.empty())) {
        ichor::log(ichor::LogLevel::Warn, "--combat-duel, --combat-role and --combat-bot only apply with --combat and are ignored");
    }
    if (options.forged && !options.campaign.empty() && !planCampaign(options)) {
        return 1;
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
    if (!options.screenshot.empty()) {
        ichor::NullAudio audio; // a screenshot run plays nothing on the owner's speakers
        return run(platform, renderer, audio, options);
    }
    ichor::MiniaudioDevice audio;
    return run(platform, renderer, audio, options);
}
