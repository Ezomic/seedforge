#include "ichor/combat_bridge/combat_bridge.h"
#include "ichor/combat_bridge/role_bot.h"
#include "ichor/combat_bridge/role_view.h"
#include "ichor/combat_bridge/terrain_trials.h"
#include "ichor/world/forge_job.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;

namespace {

constexpr int kGrid = 64;
constexpr double kDt = 1.0 / 60.0;
constexpr int kMaxFightTicks = 60 * 90;
constexpr int kPassiveTicks = 60 * 25;
constexpr int kStuckTicks = 90; // longest run of a foe trying to move and not getting anywhere, as the stage0 harness allows
constexpr float kStuckProgress = 0.25f;
constexpr double kAnchorGap = 2.2;     // inside the slam radius: the anchor never walks closer than its slam range, so a still player outside it is never hit
constexpr double kArtilleryGap = 12.0; // inside its firing band
constexpr std::uint64_t kSeeds[] = {1, 2, 3, 4, 5, 6};

const ForgedGame& gameFor(std::uint64_t seed)
{
    static std::map<std::uint64_t, std::unique_ptr<ForgedGame>> games;
    auto& slot = games[seed];
    if (!slot) {
        WorldDesc desc;
        desc.seed = seed;
        ForgeJob job(desc, kGrid);
        REQUIRE(job.start());
        for (int i = 0; i < 12000 && job.state() == ForgeJobState::Running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        INFO(job.error());
        REQUIRE(job.state() == ForgeJobState::Done);
        slot = buildForgedGame(*job.result(), desc);
        REQUIRE(slot->ok());
    }
    return *slot;
}

// Terrain collision for the cells within `radius` of `near` (all of them when `near` is empty), as the app's streamer would have put it in.
void addGround(Scene& scene, const Heightfield& heightfield, std::optional<glm::dvec3> near = std::nullopt, double radius = 300.0)
{
    const WorldDesc& desc = heightfield.desc();
    for (std::int32_t z = 0; z < static_cast<std::int32_t>(desc.cellsPerSide()); ++z) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(desc.cellsPerSide()); ++x) {
            Heightfield::Cell cell;
            if (!heightfield.cell({x, z}, cell)) continue;
            if (near) {
                const double half = desc.cellMetres * 0.5;
                if (std::hypot(heightfield.cellOriginX(cell.id) + half - near->x, heightfield.cellOriginZ(cell.id) + half - near->z) > radius + half) continue;
            }
            scene.physics().addHeightField(StableId::of("cell", cellKey(cell.id)), cell.heights.data(), cell.samplesPerSide,
                {heightfield.cellOriginX(cell.id), 0.0, heightfield.cellOriginZ(cell.id)}, static_cast<float>(desc.metresPerSample));
        }
    }
}

// The foe's post and the player's start are both taken from a trial; the settlement is only where a death would respawn.
struct Fight {
    Scene scene;
    CombatSite site;
    std::unique_ptr<CombatBridge> bridge;

    Fight(const ForgedGame& game, std::uint64_t seed, stage0::FoeRole role, glm::dvec3 foe, glm::dvec3 player, bool playerHitsBleed = false)
        : scene(seed)
    {
        site.found = true;
        site.settlement = 0;
        site.enemyPost = foe;
        site.duelStart = player;
        CombatBridgeConfig config;
        config.role = role;
        config.legacyControls = true;
        config.playerHitsBleed = playerHitsBleed;
        addGround(scene, game.heightfield, foe);
        installFieldRules(scene, game);
        SpawnCommand spawn;
        spawn.id = StableId::of("player");
        spawn.name.value = "player";
        spawn.transform.position = player + glm::dvec3(0.0, 0.5, 0.0);
        spawn.player = Player{};
        spawn.character = CharacterBody{};
        scene.enqueue(spawn);
        bridge = std::make_unique<CombatBridge>(game, site, config);
        bridge->install(scene, StableId::of("player"), player + glm::dvec3(0.0, 0.5, 0.0));
    }

    entt::entity foeEntity() { return scene.find(bridge->stage().foeId()); }
    entt::entity playerEntity() { return scene.find(StableId::of("player")); }
    const stage0::Combatant& foe() { return scene.registry().get<stage0::Combatant>(foeEntity()); }
    const stage0::Combatant& fighter() { return scene.registry().get<stage0::Combatant>(playerEntity()); }
};

struct Result {
    stage0::FightOutcome outcome = stage0::FightOutcome::Ongoing;
    int ticks = 0;
    std::uint64_t hash = 0;
    float playerHealth = 0.0f;
    float foeHealth = 0.0f;
    int actions = 0;       // slams or shells the foe let go
    int longestBlocked = 0; // ticks in a row the foe asked to move and made no headway
    bool finite = true;
};

int actionsOf(Fight& fight)
{
    const auto& registry = fight.scene.registry();
    if (const auto* anchor = registry.try_get<stage0::AnchorBrain>(fight.foeEntity())) return anchor->slams;
    if (const auto* artillery = registry.try_get<stage0::ArtilleryBrain>(fight.foeEntity())) return artillery->shots;
    return 0;
}

// Plays until the fight is decided or the time is up. `bot` false is a player who stands still.
Result play(const ForgedGame& game, std::uint64_t seed, stage0::FoeRole role, const TerrainTrial& trial, bool bot, int maxTicks)
{
    Fight fight(game, seed, role, trial.foe, trial.player);
    Result result;
    int run = 0;
    glm::dvec3 last{0.0};
    bool tracking = false;
    for (; result.ticks < maxTicks; ++result.ticks) {
        if (fight.bridge->live() && fight.bridge->outcome(fight.scene) != stage0::FightOutcome::Ongoing) break;
        const InputFrame input = bot && fight.bridge->live() ? roleBotInput(fight.scene, fight.bridge->stage()) : InputFrame{};
        fight.scene.tick(kDt, input);
        if (!fight.bridge->live() || fight.foeEntity() == entt::null) continue;
        const auto& registry = fight.scene.registry();
        const glm::dvec3 at = registry.get<Transform>(fight.foeEntity()).position;
        const glm::vec3 intent = registry.get<CharacterBody>(fight.foeEntity()).intent;
        if (!std::isfinite(at.x + at.y + at.z) || !std::isfinite(intent.x + intent.y + intent.z)) result.finite = false;
        const float asked = std::hypot(intent.x, intent.z) * static_cast<float>(kDt);
        const float moved = tracking ? static_cast<float>(std::hypot(at.x - last.x, at.z - last.z)) : asked;
        if (asked > 1e-4f && moved < asked * kStuckProgress) {
            result.longestBlocked = std::max(result.longestBlocked, ++run);
        } else if (asked > 1e-4f) {
            run = 0;
        }
        last = at;
        tracking = true;
    }
    result.outcome = fight.bridge->outcome(fight.scene);
    result.hash = fight.scene.stateHash();
    result.playerHealth = fight.fighter().health;
    result.foeHealth = fight.foe().health;
    result.actions = actionsOf(fight);
    return result;
}

struct Located {
    std::uint64_t seed = 0;
    TerrainTrial trial;
};

// The first of the test seeds that has the condition; the search is part of the test, so no seed is pinned.
std::optional<Located> locate(FieldCondition condition, bool foeHigher, double separation)
{
    for (const std::uint64_t seed : kSeeds) {
        if (const auto trial = findTrial(gameFor(seed), condition, foeHigher, {separation})) return Located{seed, *trial};
    }
    return std::nullopt;
}

struct Scenario {
    FieldCondition condition;
    bool foeHigher;
    const char* label;
};

constexpr Scenario kScenarios[] = {
    {FieldCondition::OpenField, false, "open field"},
    {FieldCondition::DenseForest, false, "dense forest"},
    {FieldCondition::NarrowDefile, false, "narrow defile"},
    {FieldCondition::SteepSlope, true, "steep slope, foe above"},
    {FieldCondition::SteepSlope, false, "steep slope, foe below"},
    {FieldCondition::ShallowWater, true, "water, foe on the bank"},
    {FieldCondition::ShallowWater, false, "water, foe wading"},
};

}

TEST_CASE("the bridge gives the foe the role it is configured with", "[combat_bridge][roles]")
{
    const ForgedGame& game = gameFor(1);
    const CombatSite site = findCombatSite(game);
    REQUIRE(site.found);
    struct Case {
        stage0::FoeRole role;
        float health;
    };
    const stage0::AnchorTuning anchor;
    const stage0::ArtilleryTuning artillery;
    for (const Case& c : {Case{stage0::FoeRole::Skirmisher, stage0::Tuning{}.maxHealth}, Case{stage0::FoeRole::Anchor, anchor.maxHealth},
             Case{stage0::FoeRole::Artillery, artillery.maxHealth}}) {
        Scene scene(1);
        CombatBridgeConfig config;
        config.role = c.role;
        addGround(scene, game.heightfield);
        SpawnCommand spawn;
        spawn.id = StableId::of("player");
        spawn.name.value = "player";
        spawn.transform.position = site.duelStart + glm::dvec3(0.0, 0.5, 0.0);
        spawn.player = Player{};
        spawn.character = CharacterBody{};
        scene.enqueue(spawn);
        CombatBridge bridge(game, site, config);
        bridge.install(scene, StableId::of("player"), site.duelStart + glm::dvec3(0.0, 0.5, 0.0));
        for (int i = 0; i < 6; ++i) scene.tick(kDt);
        REQUIRE(bridge.live());
        const auto& registry = scene.registry();
        const entt::entity foe = scene.find(bridge.stage().foeId());
        CHECK(registry.all_of<stage0::SkirmisherBrain>(foe) == (c.role == stage0::FoeRole::Skirmisher));
        CHECK(registry.all_of<stage0::AnchorBrain>(foe) == (c.role == stage0::FoeRole::Anchor));
        CHECK(registry.all_of<stage0::ArtilleryBrain>(foe) == (c.role == stage0::FoeRole::Artillery));
        CHECK(registry.get<stage0::Combatant>(foe).health == Catch::Approx(c.health));
    }
}

TEST_CASE("every one of the five terrain conditions exists in a forged world, and the search is deterministic", "[combat_bridge][roles]")
{
    for (const Scenario& s : kScenarios) {
        INFO(s.label);
        const auto found = locate(s.condition, s.foeHigher, 10.0);
        REQUIRE(found.has_value());
        const ForgedGame& game = gameFor(found->seed);
        const auto again = findTrial(game, s.condition, s.foeHigher, {10.0});
        REQUIRE(again.has_value());
        CHECK(again->foe == found->trial.foe);
        CHECK(again->player == found->trial.player);
        CHECK(std::hypot(found->trial.foe.x - found->trial.player.x, found->trial.foe.z - found->trial.player.z) == Catch::Approx(10.0));
        CHECK(found->trial.foe.y == Catch::Approx(game.heightfield.heightAt(found->trial.foe.x, found->trial.foe.z)));
    }
}

TEST_CASE("the trial search refuses nonsense and an empty world", "[combat_bridge][roles]")
{
    ForgedGame empty;
    CHECK_FALSE(findTrial(empty, FieldCondition::OpenField).has_value());
    const ForgedGame& game = gameFor(1);
    CHECK_FALSE(findTrial(game, FieldCondition::OpenField, false, {std::nan("")}).has_value());
    CHECK_FALSE(findTrial(game, FieldCondition::OpenField, false, {0.0}).has_value());
    CHECK_FALSE(findTrial(game, FieldCondition::OpenField, false, {1.0e9}).has_value());
}

// One role on one piece of ground: it acts, it is never stuck, a bot beats it, and the same bot beats it the same way twice.
void validate(stage0::FoeRole role, const Scenario& s)
{
    INFO(s.label);
    const auto found = locate(s.condition, s.foeHigher, role == stage0::FoeRole::Anchor ? kAnchorGap : kArtilleryGap);
    REQUIRE(found.has_value());
    const ForgedGame& game = gameFor(found->seed);

    const Result passive = play(game, found->seed, role, found->trial, false, kPassiveTicks);
    CHECK(passive.finite);
    CHECK(passive.actions >= 1);
    CHECK(passive.playerHealth < stage0::Tuning{}.maxHealth);
    CHECK(passive.longestBlocked <= kStuckTicks);

    const Result first = play(game, found->seed, role, found->trial, true, kMaxFightTicks);
    const Result second = play(game, found->seed, role, found->trial, true, kMaxFightTicks);
    CHECK(first.finite);
    CHECK(first.outcome == stage0::FightOutcome::FoeDied);
    CHECK(first.actions >= 1);
    CHECK(first.longestBlocked <= kStuckTicks);
    CHECK(first.hash == second.hash);
    CHECK(first.ticks == second.ticks);
}

TEST_CASE("the anchor works on all five terrain conditions and a bot can beat it", "[combat_bridge][roles]")
{
    for (const Scenario& s : kScenarios) validate(stage0::FoeRole::Anchor, s);
}

TEST_CASE("the artillery works on all five terrain conditions and a bot can beat it", "[combat_bridge][roles]")
{
    for (const Scenario& s : kScenarios) validate(stage0::FoeRole::Artillery, s);
}

TEST_CASE("each role works in the open field and on a steep slope, the pull request variant of the five conditions", "[combat_bridge][roles]")
{
    for (const stage0::FoeRole role : {stage0::FoeRole::Anchor, stage0::FoeRole::Artillery}) {
        validate(role, kScenarios[0]);
        validate(role, kScenarios[3]);
    }
}

TEST_CASE("a player who stands still loses to each role and the scripted player wins, so the bot tells the roles from nothing", "[combat_bridge][roles]")
{
    for (const stage0::FoeRole role : {stage0::FoeRole::Anchor, stage0::FoeRole::Artillery}) {
        const auto open = locate(FieldCondition::OpenField, false, role == stage0::FoeRole::Anchor ? kAnchorGap : kArtilleryGap);
        REQUIRE(open.has_value());
        const ForgedGame& game = gameFor(open->seed);
        const Result passive = play(game, open->seed, role, open->trial, false, kMaxFightTicks);
        const Result bot = play(game, open->seed, role, open->trial, true, kMaxFightTicks);
        CHECK(passive.outcome == stage0::FightOutcome::PlayerDied);
        CHECK(passive.foeHealth > 0.0f);
        CHECK(bot.outcome == stage0::FightOutcome::FoeDied);
        CHECK(bot.playerHealth > passive.playerHealth);
    }
}

TEST_CASE("the artillery cannot aim through a hill, and the hill is real terrain", "[combat_bridge][roles]")
{
    const ForgedGame& game = gameFor(1);
    const CombatSite found = findCombatSite(game);
    REQUIRE(found.found);
    CombatBridgeConfig config;
    config.role = stage0::FoeRole::Artillery;
    CombatBridge bridge(game, found, config);
    Scene scene(1);
    addGround(scene, game.heightfield);
    bridge.install(scene, StableId::of("player"), found.duelStart);
    const stage0::Stage0::Ground& ground = bridge.stage().ground();
    REQUIRE(ground.lineOfSight);

    const double size = game.heightfield.desc().sizeMetres;
    const glm::dvec3 low(size * 0.5, static_cast<double>(game.heightfield.heightAt(size * 0.5, size * 0.5)) + 1.5, size * 0.5);
    CHECK(ground.lineOfSight(low, low + glm::dvec3(0.0, 0.0, 0.5)));
    // A point far above the ground in the middle of the line sees clear, the same two points with the line pushed under the terrain do not.
    const glm::dvec3 up = low + glm::dvec3(40.0, 400.0, 0.0);
    CHECK(ground.lineOfSight(up, up + glm::dvec3(30.0, 0.0, 0.0)));
    const glm::dvec3 under = low + glm::dvec3(40.0, -400.0, 0.0);
    CHECK_FALSE(ground.lineOfSight(under, under + glm::dvec3(30.0, 0.0, 0.0)));
    CHECK_FALSE(ground.lineOfSight(low, glm::dvec3(std::nan(""), 0.0, 0.0)));
}

TEST_CASE("placeFoe swaps the live foe to the asked role and back to the configured one, and a parked foe has no brain", "[combat_bridge][roles]")
{
    const ForgedGame& game = gameFor(1);
    const CombatSite post = findCombatSite(game);
    REQUIRE(post.found);
    Fight fight(game, 1, stage0::FoeRole::Skirmisher, post.enemyPost, post.duelStart);
    for (int i = 0; i < 6; ++i) fight.scene.tick(kDt);
    REQUIRE(fight.bridge->live());
    const auto& registry = fight.scene.registry();

    fight.bridge->placeFoe(fight.scene, post.enemyPost, stage0::FoeRole::Anchor);
    CHECK(registry.all_of<stage0::AnchorBrain>(fight.foeEntity()));
    CHECK_FALSE(registry.all_of<stage0::SkirmisherBrain>(fight.foeEntity()));
    CHECK(fight.bridge->stage().foeRole() == stage0::FoeRole::Anchor);
    CHECK(fight.foe().health == Catch::Approx(fight.bridge->stage().anchorTuning().maxHealth));
    CHECK(fight.foe().poise == Catch::Approx(fight.bridge->stage().anchorTuning().maxPoise));

    fight.bridge->placeFoe(fight.scene, post.enemyPost, stage0::FoeRole::Artillery);
    CHECK(registry.all_of<stage0::ArtilleryBrain>(fight.foeEntity()));
    CHECK_FALSE(registry.all_of<stage0::AnchorBrain>(fight.foeEntity()));
    CHECK(fight.foe().health == Catch::Approx(fight.bridge->stage().artilleryTuning().maxHealth));

    fight.bridge->parkFoe(fight.scene, post.enemyPost);
    CHECK_FALSE(registry.all_of<stage0::ArtilleryBrain>(fight.foeEntity()));
    CHECK_FALSE(fight.foe().alive());

    fight.bridge->placeFoe(fight.scene, post.enemyPost);
    CHECK(registry.all_of<stage0::SkirmisherBrain>(fight.foeEntity()));
    CHECK(fight.foe().health == Catch::Approx(stage0::Tuning{}.maxHealth));
}

namespace {

bool foeBleedsAfterBotHit(const ForgedGame& game, const CombatSite& post, bool playerHitsBleed)
{
    Fight fight(game, 1, stage0::FoeRole::Skirmisher, post.enemyPost, post.duelStart, playerHitsBleed);
    const float full = stage0::Tuning{}.maxHealth;
    for (int i = 0; i < 6; ++i) fight.scene.tick(kDt);
    REQUIRE(fight.bridge->live());
    for (int i = 0; i < kMaxFightTicks && fight.foe().health >= full; ++i) {
        fight.scene.tick(kDt, roleBotInput(fight.scene, fight.bridge->stage()));
    }
    REQUIRE(fight.foe().health < full);
    return fight.foe().status.find(stage0::StatusId::Bleed) != nullptr;
}

void dragFoe(Fight& fight, glm::dvec3 post, double metres)
{
    Transform moved = fight.scene.registry().get<Transform>(fight.foeEntity());
    moved.position = post + glm::dvec3(metres, 0.5, 0.0);
    fight.scene.enqueue(SetTransformCommand{fight.bridge->stage().foeId(), moved});
}

}

TEST_CASE("a hit from the player leaves the foe bleeding only when the config asks for it", "[combat_bridge][roles][status]")
{
    const ForgedGame& game = gameFor(1);
    const CombatSite post = findCombatSite(game);
    REQUIRE(post.found);
    CHECK_FALSE(CombatBridgeConfig{}.playerHitsBleed);
    CHECK_FALSE(foeBleedsAfterBotHit(game, post, false));
    CHECK(foeBleedsAfterBotHit(game, post, true));
}

TEST_CASE("the HUD state lists the player's effects and the foe's tint follows its own", "[combat_bridge][roles][status]")
{
    const ForgedGame& game = gameFor(1);
    const CombatSite post = findCombatSite(game);
    REQUIRE(post.found);
    Fight fight(game, 1, stage0::FoeRole::Skirmisher, post.enemyPost, post.duelStart);
    for (int i = 0; i < 6; ++i) fight.scene.tick(kDt);
    REQUIRE(fight.bridge->live());
    CHECK(fight.bridge->hud(fight.scene).statuses.empty());

    REQUIRE(fight.bridge->stage().applyStatus(fight.scene, fight.playerEntity(), stage0::StatusId::Bleed));
    REQUIRE(fight.bridge->stage().applyStatus(fight.scene, fight.playerEntity(), stage0::StatusId::Bleed));
    REQUIRE(fight.bridge->stage().applyStatus(fight.scene, fight.playerEntity(), stage0::StatusId::Slow));
    const CombatHudState state = fight.bridge->hud(fight.scene);
    REQUIRE(state.statuses.size() == 2);
    CHECK(state.statuses[0].id == stage0::StatusId::Bleed);
    CHECK(state.statuses[0].stacks == 2);
    CHECK(state.statuses[0].remaining == Catch::Approx(1.0f));
    CHECK(state.statuses[1].id == stage0::StatusId::Slow);

    stage0::StatusSet none;
    CHECK(statusTint(none) == glm::vec3(1.0f));
    stage0::StatusSet bleeding;
    bleeding.apply(stage0::StatusId::Bleed);
    CHECK(statusTint(bleeding).r > statusTint(bleeding).g);
    stage0::StatusSet slowed;
    slowed.apply(stage0::StatusId::Slow);
    CHECK(statusTint(slowed).b > statusTint(slowed).r);
}

TEST_CASE("a foe dragged past its leash walks home and heals, then guards again", "[combat_bridge][roles]")
{
    const ForgedGame& game = gameFor(1);
    const CombatSite post = findCombatSite(game);
    REQUIRE(post.found);
    Fight fight(game, 1, stage0::FoeRole::Skirmisher, post.enemyPost, post.duelStart);
    for (int i = 0; i < 6; ++i) fight.scene.tick(kDt);
    REQUIRE(fight.bridge->live());
    const auto& registry = fight.scene.registry();
    fight.scene.registry().get<stage0::Combatant>(fight.foeEntity()).health = 20.0f;

    dragFoe(fight, post.enemyPost, CombatBridgeConfig{}.foeLeashRadiusM + 15.0);
    for (int i = 0; i < 3; ++i) fight.scene.tick(kDt);
    CHECK_FALSE(registry.all_of<stage0::SkirmisherBrain>(fight.foeEntity()));

    const auto groundDistance = [&] {
        const glm::dvec3 at = registry.get<Transform>(fight.foeEntity()).position;
        return std::hypot(at.x - post.enemyPost.x, at.z - post.enemyPost.z);
    };
    for (int i = 0; i < 60 * 40 && !registry.all_of<stage0::SkirmisherBrain>(fight.foeEntity()); ++i) fight.scene.tick(kDt);
    CHECK(registry.all_of<stage0::SkirmisherBrain>(fight.foeEntity()));
    CHECK(groundDistance() < 3.0);
    CHECK(fight.foe().health == Catch::Approx(stage0::Tuning{}.maxHealth));
    CHECK(fight.foe().alive());
}

TEST_CASE("a foe inside its leash keeps its brain and its wounds", "[combat_bridge][roles]")
{
    const ForgedGame& game = gameFor(1);
    const CombatSite post = findCombatSite(game);
    REQUIRE(post.found);
    Fight fight(game, 1, stage0::FoeRole::Skirmisher, post.enemyPost, post.duelStart);
    for (int i = 0; i < 6; ++i) fight.scene.tick(kDt);
    REQUIRE(fight.bridge->live());
    fight.scene.registry().get<stage0::Combatant>(fight.foeEntity()).health = 20.0f;

    dragFoe(fight, post.enemyPost, CombatBridgeConfig{}.foeLeashRadiusM - 15.0);
    for (int i = 0; i < 120; ++i) fight.scene.tick(kDt);
    CHECK(fight.scene.registry().all_of<stage0::SkirmisherBrain>(fight.foeEntity()));
    CHECK(fight.foe().health <= 20.0f);
}
