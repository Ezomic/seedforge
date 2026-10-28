#include "ichor/combat_bridge/combat_bridge.h"
#include "ichor/stage0/fight_report.h"
#include "ichor/world/forge_job.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
#include <map>
#include <memory>
#include <thread>

using namespace ichor;

namespace {

constexpr int kGrid = 64;
constexpr double kDt = 1.0 / 60.0;

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

// Terrain collision for every cell, as the app's streamer would have put it in.
void addGround(Scene& scene, const Heightfield& heightfield)
{
    const WorldDesc& desc = heightfield.desc();
    for (std::int32_t z = 0; z < static_cast<std::int32_t>(desc.cellsPerSide()); ++z) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(desc.cellsPerSide()); ++x) {
            Heightfield::Cell cell;
            if (!heightfield.cell({x, z}, cell)) continue;
            scene.physics().addHeightField(StableId::of("cell", cellKey(cell.id)), cell.heights.data(), cell.samplesPerSide,
                {heightfield.cellOriginX(cell.id), 0.0, heightfield.cellOriginZ(cell.id)}, static_cast<float>(desc.metresPerSample));
        }
    }
}

struct Setup {
    Scene scene;
    CombatSite site;
    std::unique_ptr<CombatBridge> bridge;
    glm::dvec3 start{0.0};

    Setup(const ForgedGame& game, std::uint64_t seed, CombatBridgeConfig config = {})
        : scene(seed)
        , site(findCombatSite(game))
    {
        REQUIRE(site.found);
        config.legacyControls = true;
        start = site.duelStart + glm::dvec3(0.0, 0.5, 0.0);
        addGround(scene, game.heightfield);
        SpawnCommand player;
        player.id = StableId::of("player");
        player.name.value = "player";
        player.transform.position = start;
        player.player = Player{};
        player.character = CharacterBody{};
        scene.enqueue(player);
        bridge = std::make_unique<CombatBridge>(game, site, config);
        bridge->install(scene, StableId::of("player"), start);
    }

    entt::entity player() { return scene.find(StableId::of("player")); }
    const stage0::Combatant& fighter() { return scene.registry().get<stage0::Combatant>(player()); }
    const stage0::Combatant& enemy() { return scene.registry().get<stage0::Combatant>(scene.find(bridge->stage().foeId())); }
    glm::dvec3 playerAt() { return scene.registry().get<Transform>(player()).position; }
    glm::dvec3 enemyAt() { return scene.registry().get<Transform>(scene.find(bridge->stage().foeId())).position; }
    void tick(const InputFrame& input = {}) { scene.tick(kDt, input); }
    void tick(int count)
    {
        for (int i = 0; i < count; ++i) tick();
    }
};

struct TempDir {
    std::filesystem::path path;

    TempDir()
    {
        static int counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("ichor_combat_fights_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::string slurp(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Played {
    stage0::FightOutcome outcome = stage0::FightOutcome::Ongoing;
    int ticks = 0;
    std::uint64_t hash = 0;
    float playerHealth = 0.0f;
    float enemyHealth = 0.0f;
    float damageDealt = 0.0f;
    float damageTaken = 0.0f;
};

// Plays until the fight is decided (or the time is up). A skill-less player stands still.
Played play(const ForgedGame& game, std::uint64_t worldSeed, const stage0::BotSkill* skill, std::uint64_t botSeed)
{
    Setup setup(game, worldSeed);
    if (skill != nullptr) setup.bridge->enableBot(*skill, botSeed);
    Played played;
    constexpr int kMaxTicks = 60 * 120;
    for (; played.ticks < kMaxTicks && setup.bridge->outcome(setup.scene) == stage0::FightOutcome::Ongoing; ++played.ticks) {
        setup.tick(setup.bridge->botInput(setup.scene));
    }
    played.outcome = setup.bridge->outcome(setup.scene);
    played.hash = setup.scene.stateHash();
    played.playerHealth = setup.fighter().health;
    played.enemyHealth = setup.enemy().health;
    played.damageDealt = setup.bridge->stage().recorder().player().damageDealt;
    played.damageTaken = setup.bridge->stage().recorder().player().damageTaken;
    return played;
}

bool same(const Played& a, const Played& b)
{
    return a.outcome == b.outcome && a.ticks == b.ticks && a.hash == b.hash && a.playerHealth == b.playerHealth && a.enemyHealth == b.enemyHealth;
}

// The first bot seed whose fight ends the given way; the search is part of the test, so a retuned fight finds new seeds.
bool findBotSeed(const ForgedGame& game, std::uint64_t worldSeed, const stage0::BotSkill& skill, stage0::FightOutcome wanted, std::uint64_t& seed)
{
    for (seed = 1; seed <= 40; ++seed) {
        if (play(game, worldSeed, &skill, seed).outcome == wanted) return true;
    }
    return false;
}

}

TEST_CASE("the combat site is dry gentle ground beside a settlement, and the same every time", "[combat_bridge]")
{
    for (const std::uint64_t seed : {1u, 2u}) {
        INFO("seed " << seed);
        const ForgedGame& game = gameFor(seed);
        const CombatSite site = findCombatSite(game);
        REQUIRE(site.found);
        REQUIRE(site.settlement < game.sites.size());
        const SettlementSite& settlement = game.sites[site.settlement];
        const double fromSettlement = std::hypot(site.enemyPost.x - settlement.x, site.enemyPost.z - settlement.z);
        CHECK(fromSettlement > 20.0);
        CHECK(fromSettlement < 200.0);
        for (const glm::dvec3 spot : {site.enemyPost, site.duelStart}) {
            CHECK(game.heightfield.heightAt(spot.x, spot.z) > game.desc.waterLevel());
            CHECK(game.heightfield.normalAt(spot.x, spot.z).y >= 0.85f);
            if (game.waterField) CHECK_FALSE(game.waterField->at(spot.x, spot.z).wet());
            CHECK(spot.y == Catch::Approx(game.heightfield.heightAt(spot.x, spot.z)));
        }
        const double apart = std::hypot(site.duelStart.x - site.enemyPost.x, site.duelStart.z - site.enemyPost.z);
        CHECK(apart >= 6.0);
        CHECK(apart <= stage0::Tuning{}.aggroRange);
        const CombatSite again = findCombatSite(game);
        CHECK(again.enemyPost == site.enemyPost);
        CHECK(again.duelStart == site.duelStart);
    }
}

TEST_CASE("a world with no settlements has no combat site and a bridge on it does nothing", "[combat_bridge]")
{
    ForgedGame empty;
    CHECK_FALSE(findCombatSite(empty).found);

    Scene scene(1);
    CombatBridge bridge(empty, CombatSite{});
    bridge.install(scene, StableId::of("player"), glm::dvec3(0.0));
    for (int i = 0; i < 5; ++i) scene.tick(kDt);
    CHECK_FALSE(bridge.live());
    CHECK(bridge.outcome(scene) == stage0::FightOutcome::Ongoing);
    CHECK(bridge.hud(scene).health == bridge.hud(scene).maxHealth);
    CHECK(bridge.botInput(scene).buttons == 0);
}

TEST_CASE("the enemy waits for its ground and the fight sets up once it is there", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    Setup setup(game, 1);
    bool ready = false;
    setup.bridge->setGroundCheck([&](glm::dvec3) { return ready; });
    setup.tick(10);
    CHECK_FALSE(setup.bridge->live());
    CHECK((setup.scene.find(setup.bridge->stage().foeId()) == entt::null));
    ready = true;
    setup.tick(4);
    REQUIRE(setup.bridge->live());
    CHECK(setup.enemy().health == setup.bridge->stage().tuning().maxHealth);
    CHECK(std::hypot(setup.enemyAt().x - setup.site.enemyPost.x, setup.enemyAt().z - setup.site.enemyPost.z) < 1.0);
    CHECK(setup.enemyAt().y == Catch::Approx(setup.site.enemyPost.y).margin(0.5));
}

TEST_CASE("a player who does nothing loses the fight, the same way twice, on two seeds", "[combat_bridge]")
{
    for (const std::uint64_t seed : {1u, 2u}) {
        INFO("seed " << seed);
        const ForgedGame& game = gameFor(seed);
        const Played first = play(game, seed, nullptr, 0);
        const Played second = play(game, seed, nullptr, 0);
        CHECK(first.outcome == stage0::FightOutcome::PlayerDied);
        CHECK(first.playerHealth == 0.0f);
        CHECK(first.enemyHealth == stage0::Tuning{}.maxHealth);
        CHECK(first.damageTaken >= stage0::Tuning{}.maxHealth);
        CHECK(first.ticks > 60);
        CHECK(same(first, second));
    }
}

TEST_CASE("a scripted bot wins a fight in the forged world, the same way twice, on two seeds", "[combat_bridge]")
{
    const stage0::BotSkill expert = *stage0::findBotSkill("expert");
    for (const std::uint64_t seed : {1u, 2u}) {
        INFO("seed " << seed);
        const ForgedGame& game = gameFor(seed);
        std::uint64_t botSeed = 0;
        REQUIRE(findBotSeed(game, seed, expert, stage0::FightOutcome::FoeDied, botSeed));
        const Played first = play(game, seed, &expert, botSeed);
        const Played second = play(game, seed, &expert, botSeed);
        CHECK(first.outcome == stage0::FightOutcome::FoeDied);
        CHECK(first.enemyHealth == 0.0f);
        CHECK(first.playerHealth > 0.0f);
        CHECK(first.damageDealt >= stage0::Tuning{}.maxHealth);
        CHECK(same(first, second));
    }
}

TEST_CASE("a scripted bot loses a fight in the forged world, the same way twice, on two seeds", "[combat_bridge]")
{
    const stage0::BotSkill novice = *stage0::findBotSkill("novice");
    for (const std::uint64_t seed : {1u, 2u}) {
        INFO("seed " << seed);
        const ForgedGame& game = gameFor(seed);
        std::uint64_t botSeed = 0;
        REQUIRE(findBotSeed(game, seed, novice, stage0::FightOutcome::PlayerDied, botSeed));
        const Played first = play(game, seed, &novice, botSeed);
        const Played second = play(game, seed, &novice, botSeed);
        CHECK(first.outcome == stage0::FightOutcome::PlayerDied);
        CHECK(first.playerHealth == 0.0f);
        CHECK(first.enemyHealth > 0.0f);
        CHECK(same(first, second));
    }
}

TEST_CASE("the fight is a different fight in another world", "[combat_bridge]")
{
    const Played one = play(gameFor(1), 1, nullptr, 0);
    const Played two = play(gameFor(2), 2, nullptr, 0);
    CHECK(one.hash != two.hash);
}

TEST_CASE("a dead player stands at the settlement again with full bars and the enemy is reset", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    Setup setup(game, 1);
    setup.tick(4);
    REQUIRE(setup.bridge->live());
    const glm::dvec3 settlement = respawnPoint(game, setup.site.settlement);

    setup.bridge->stage().receiveHit(setup.scene, setup.player(), setup.scene.find(setup.bridge->stage().foeId()), 500.0f, 0.0f, true);
    REQUIRE(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::PlayerDied);
    CHECK(setup.bridge->hud(setup.scene).health == 0.0f);

    setup.tick(static_cast<int>(2.0 / kDt));
    CHECK(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::PlayerDied);
    CHECK(setup.bridge->respawns() == 0);

    setup.tick(static_cast<int>(2.0 / kDt));
    CHECK(setup.bridge->respawns() == 1);
    CHECK(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::Ongoing);
    CHECK(setup.fighter().health == setup.bridge->stage().tuning().maxHealth);
    CHECK(std::hypot(setup.playerAt().x - settlement.x, setup.playerAt().z - settlement.z) < 2.0);
    CHECK(std::hypot(setup.enemyAt().x - setup.site.enemyPost.x, setup.enemyAt().z - setup.site.enemyPost.z) < 2.0);
}

TEST_CASE("the player respawns at the last settlement they were in", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    Setup setup(game, 1);
    setup.tick(4);
    REQUIRE(setup.bridge->live());
    std::size_t other = game.sites.size();
    for (std::size_t i = 0; i < game.sites.size(); ++i) {
        if (i != setup.site.settlement) other = i;
    }
    if (other == game.sites.size()) {
        SKIP("this world has only one settlement");
    }

    const glm::dvec3 there = respawnPoint(game, other);
    setup.scene.enqueue(MoveCommand{StableId::of("player"), there + glm::dvec3(0.0, 0.5, 0.0) - setup.playerAt()});
    setup.tick(4);
    CHECK(setup.bridge->lastSettlement() == other);

    setup.bridge->stage().receiveHit(setup.scene, setup.player(), setup.scene.find(setup.bridge->stage().foeId()), 500.0f, 0.0f, true);
    setup.tick(static_cast<int>(4.0 / kDt));
    CHECK(setup.bridge->respawns() == 1);
    CHECK(std::hypot(setup.playerAt().x - there.x, setup.playerAt().z - there.z) < 2.0);
}

TEST_CASE("a nonsense respawn delay or step falls back to a usable one", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    CombatBridgeConfig config;
    config.respawnDelaySeconds = std::nan("");
    Setup setup(game, 1, config);
    setup.tick(4);
    REQUIRE(setup.bridge->live());
    setup.bridge->stage().receiveHit(setup.scene, setup.player(), setup.scene.find(setup.bridge->stage().foeId()), 500.0f, 0.0f, true);
    setup.tick(static_cast<int>(5.0 / kDt));
    CHECK(setup.bridge->respawns() == 1);
}

TEST_CASE("a lost fight writes a fights.csv row with the world seed and the site that stage0_report reads", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(2);
    TempDir dir;
    CombatBridgeConfig config;
    config.fightsCsv = dir.path / "fights.csv";
    Setup setup(game, 2, config);
    for (int i = 0; i < 60 * 120 && setup.bridge->outcome(setup.scene) == stage0::FightOutcome::Ongoing; ++i) setup.tick();
    REQUIRE(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::PlayerDied);

    std::string text = slurp(config.fightsCsv);
    std::erase(text, '\r');
    const std::string siteId = game.sites[setup.site.settlement].id;
    CHECK(text.starts_with("fight,seconds,gap_to_retry,loser,cause,"));
    CHECK(text.find(",kill_onscreen,world_seed,site\n") != std::string::npos);
    CHECK(text.find("," + std::to_string(game.desc.seed) + "," + siteId + "\n") != std::string::npos);

    const auto rows = stage0::parseFightLog(text);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].fight == 1);
    CHECK(rows[0].playerLost);
    CHECK(rows[0].cause == static_cast<stage0::DeathCause>(setup.fighter().deathCause));
    CHECK(rows[0].cause != stage0::DeathCause::Unknown);
    CHECK(rows[0].seconds > 1.0);
    const stage0::FightSummary summary = stage0::summarise(rows);
    CHECK(summary.playerLosses == 1);
    CHECK(summary.playerDeaths.count(rows[0].cause) == 1);

    CHECK(std::filesystem::exists(dir.path / "fights.fights" / "fight_001.ifrd"));
}

TEST_CASE("fights across a respawn are numbered in order and a second session appends to the same log", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    TempDir dir;
    CombatBridgeConfig config;
    config.fightsCsv = dir.path / "fights.csv";
    for (int session = 0; session < 2; ++session) {
        Setup setup(game, 1, config);
        setup.tick(4);
        REQUIRE(setup.bridge->live());
        for (int i = 0; i < 60 * 120 && setup.bridge->outcome(setup.scene) == stage0::FightOutcome::Ongoing; ++i) setup.tick();
        REQUIRE(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::PlayerDied);
    }
    const auto rows = stage0::parseFightLog(slurp(config.fightsCsv));
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].fight == 1);
    CHECK(rows[1].fight == 2);
    CHECK(stage0::summarise(rows).playerLosses == 2);
}

TEST_CASE("logging the fights does not change the fight", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    TempDir dir;
    CombatBridgeConfig config;
    config.fightsCsv = dir.path / "fights.csv";
    const Played plain = play(game, 1, nullptr, 0);
    Setup logged(game, 1, config);
    Played with;
    for (; with.ticks < 60 * 120 && logged.bridge->outcome(logged.scene) == stage0::FightOutcome::Ongoing; ++with.ticks) logged.tick();
    CHECK(logged.scene.stateHash() == plain.hash);
    CHECK(with.ticks == plain.ticks);
}

TEST_CASE("the forged view hides the Stage 0 debug lines by default and still shows the enemy telegraph", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    Setup setup(game, 1);
    setup.bridge->setGroundCheck([](glm::dvec3) { return true; });
    setup.tick(4);
    REQUIRE(setup.bridge->live());
    auto& foe = setup.scene.registry().get<stage0::Combatant>(setup.scene.find(setup.bridge->stage().foeId()));
    foe.state = static_cast<std::uint8_t>(stage0::CombatState::Windup);
    foe.stateTime = setup.bridge->stage().tuning().lightWindup;
    const auto hasColour = [](const RenderView& view, glm::vec3 colour) {
        return std::any_of(view.lines.begin(), view.lines.end(), [&](const DebugLine& line) { return line.color == colour; });
    };
    const glm::vec3 spacingRing(0.2f, 0.9f, 0.9f);
    const glm::vec3 windup(1.0f, 0.7f, 0.15f);

    RenderView quiet;
    setup.bridge->buildView(setup.scene, 1.0, quiet);
    CHECK_FALSE(hasColour(quiet, spacingRing));
    CHECK_FALSE(hasColour(quiet, glm::vec3(0.9f)));
    CHECK(quiet.lines.size() > 10);

    RenderView debug;
    setup.bridge->buildView(setup.scene, 1.0, debug, combatDebugAids(OverlayMode::Full, false));
    CHECK(hasColour(debug, spacingRing));
    CHECK(debug.lines.size() > quiet.lines.size());
    CHECK(hasColour(quiet, windup));
}

TEST_CASE("a fight in a dungeon is logged with the dungeon as its site and the next one in the world with the settlement", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    TempDir dir;
    CombatBridgeConfig config;
    config.fightsCsv = dir.path / "fights.csv";
    Setup setup(game, 1, config);
    setup.tick(4);
    REQUIRE(setup.bridge->live());
    setup.bridge->setFightSite("dungeon:cellar,x");
    const auto dieOnce = [&] {
        for (int i = 0; i < 60 * 120 && setup.bridge->outcome(setup.scene) == stage0::FightOutcome::Ongoing; ++i) setup.tick();
        REQUIRE(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::PlayerDied);
    };
    dieOnce();
    setup.tick(static_cast<int>(4.0 / kDt));
    REQUIRE(setup.bridge->respawns() == 1);
    setup.bridge->returnToWorld();
    setup.tick(4);
    setup.scene.enqueue(MoveCommand{StableId::of("player"), setup.site.duelStart + glm::dvec3(0.0, 0.5, 0.0) - setup.playerAt()});
    setup.tick(4);
    dieOnce();

    std::string text = slurp(config.fightsCsv);
    std::erase(text, '\r');
    const std::string settlement = game.sites[setup.site.settlement].id;
    CHECK(text.find("," + std::to_string(game.desc.seed) + ",dungeon:cellarx\n") != std::string::npos);
    CHECK(text.find("," + std::to_string(game.desc.seed) + "," + settlement + "\n") != std::string::npos);
    CHECK(stage0::parseFightLog(text).size() == 2);
}

TEST_CASE("a death in the forged world reports its cause, and the guarded Enter retries only after the window", "[combat_bridge]")
{
    const ForgedGame& game = gameFor(1);
    Setup setup(game, 1);
    CHECK_FALSE(setup.bridge->playerDeath(setup.scene).has_value());
    setup.tick(4);
    REQUIRE(setup.bridge->live());
    CHECK_FALSE(setup.bridge->playerDeath(setup.scene).has_value());

    setup.bridge->stage().receiveHit(setup.scene, setup.player(), setup.scene.find(setup.bridge->stage().foeId()), 500.0f, 0.0f, true);
    REQUIRE(setup.bridge->playerDeath(setup.scene).has_value());
    CHECK(*setup.bridge->playerDeath(setup.scene) == static_cast<stage0::DeathCause>(setup.fighter().deathCause));

    stage0::RetryGuard guard;
    const auto feed = [&](std::uint32_t pressed) {
        InputFrame frame;
        frame.buttons = pressed;
        setup.tick(guard.filter(frame, setup.bridge->playerDeath(setup.scene).has_value(), setup.scene.time()));
    };
    feed(InputFrame::Confirm);
    CHECK(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::PlayerDied);
    feed(0);
    for (int i = 0; i < static_cast<int>(stage0::RetryGuard::kArmSeconds / kDt) + 1; ++i) feed(0);
    feed(InputFrame::Confirm);
    CHECK(setup.bridge->outcome(setup.scene) == stage0::FightOutcome::Ongoing);
    CHECK(setup.bridge->respawns() == 0);
    CHECK_FALSE(setup.bridge->playerDeath(setup.scene).has_value());
}
