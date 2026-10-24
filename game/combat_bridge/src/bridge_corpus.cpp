#include "ichor/combat_bridge/bridge_corpus.h"

#include "ichor/combat_bridge/combat_bridge.h"
#include "ichor/stage0/fight_bot.h"
#include "ichor/stage0/fight_report.h"
#include "ichor/world/forge_job.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace ichor::bridge_corpus {

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr std::string_view kHeader = "ichor-combat-bridge-corpus";
constexpr int kForgeWaitMs = 120'000;
constexpr int kMaxGrid = 4096;

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

// One fight's scene: the forged world's terrain, the player, and the bridge around them.
struct Run {
    Scene scene;
    CombatSite site;
    std::unique_ptr<CombatBridge> bridge;

    Run(const ForgedGame& game, std::uint64_t sceneSeed, const stage0::Tuning& tuning)
        : scene(sceneSeed)
        , site(findCombatSite(game))
    {
        if (!site.found) return;
        CombatBridgeConfig config;
        config.tuning = tuning;
        config.legacyControls = true;
        const glm::dvec3 start = site.duelStart + glm::dvec3(0.0, 0.5, 0.0);
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

    bool ready() const { return bridge != nullptr; }
    bool decided() const { return bridge->outcome(scene) != stage0::FightOutcome::Ongoing; }

    Outcome outcome(int ticks)
    {
        Outcome result;
        result.ticks = ticks;
        result.stateHash = scene.stateHash();
        const stage0::FightRecorder& recorder = bridge->stage().recorder();
        if (recorder.fightsWritten() < 1) return result;
        const std::vector<stage0::FightRow> rows = stage0::parseFightLog(recorder.lastRow());
        if (rows.size() != 1) return result;
        result.finished = true;
        result.playerLost = rows[0].playerLost;
        result.cause = rows[0].cause;
        result.playerHits = rows[0].playerHits;
        result.damageTakenTenths = static_cast<int>(std::lround(rows[0].playerDamageTaken * 10.0));
        return result;
    }
};

template <typename T>
bool parseNumber(std::string_view text, T& out, int base = 10)
{
    T value{};
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return false;
    out = value;
    return true;
}

std::optional<std::string_view> fieldValue(std::string_view& text, std::string_view key)
{
    const std::size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.starts_with(key) || line.size() <= key.size() + 1 || line[key.size()] != ' ') return std::nullopt;
    text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    return line.substr(key.size() + 1);
}

bool finishedAs(const Entry& entry, bool playerWins)
{
    return entry.expected.finished && entry.expected.playerLost != playerWins;
}

}

std::span<const Slot> plan()
{
    static constexpr Slot slots[] = {
        {"novice", 1, false, 0}, {"novice", 2, false, 0}, {"novice", 3, false, 0}, {"novice", 1, false, 1}, {"novice", 2, false, 1}, {"novice", 3, false, 1},
        {"novice", 1, true, 0},
        {"regular", 1, true, 0}, {"regular", 2, false, 0}, {"regular", 3, true, 0}, {"regular", 1, false, 0}, {"regular", 2, true, 0}, {"regular", 3, false, 0},
        {"regular", 1, true, 1},
        {"expert", 1, true, 0}, {"expert", 2, true, 0}, {"expert", 3, true, 0}, {"expert", 1, false, 0}, {"expert", 2, true, 1}, {"expert", 3, true, 1},
    };
    return slots;
}

World forgeWorld(std::uint64_t seed, int grid, std::string& error)
{
    error.clear();
    World world;
    world.seed = seed;
    world.grid = grid;
    if (grid < 1 || grid > kMaxGrid) {
        error = std::format("grid {} is out of range", grid);
        return world;
    }
    WorldDesc desc;
    desc.seed = seed;
    ForgeJob job(desc, grid);
    if (!job.start()) {
        error = "the forge did not start";
        return world;
    }
    for (int waited = 0; waited < kForgeWaitMs && job.state() == ForgeJobState::Running; waited += 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (job.state() != ForgeJobState::Done) {
        error = job.state() == ForgeJobState::Running ? "the forge timed out" : job.error();
        return world;
    }
    std::unique_ptr<ForgedGame> game = buildForgedGame(*job.result(), desc);
    if (!game || !game->ok()) {
        error = game ? game->error : "the forged game did not build";
        return world;
    }
    world.game = std::move(game);
    return world;
}

std::uint64_t worldHashOf(const World& world)
{
    return world.game && world.game->world ? world.game->world->worldHash() : 0;
}

Outcome replayFight(const World& world, std::uint64_t sceneSeed, std::span<const InputFrame> frames, const stage0::Tuning& tuning)
{
    if (!world.game) return {};
    Run run(*world.game, sceneSeed, tuning);
    if (!run.ready()) return {};
    int ticks = 0;
    for (const InputFrame& frame : frames) {
        run.scene.tick(kDt, frame);
        ticks += 1;
        if (run.decided()) break;
    }
    return run.outcome(ticks);
}

std::optional<Entry> recordEntry(const World& world, std::string_view skill, std::uint64_t sceneSeed)
{
    const std::optional<stage0::BotSkill> found = stage0::findBotSkill(skill);
    if (!found || !world.game) return std::nullopt;
    Run run(*world.game, sceneSeed, {});
    if (!run.ready()) return std::nullopt;
    run.bridge->enableBot(*found, sceneSeed);

    Entry entry;
    entry.skill = std::string(skill);
    entry.worldSeed = world.seed;
    entry.grid = world.grid;
    entry.worldHash = worldHashOf(world);
    entry.sceneSeed = sceneSeed;
    while (static_cast<int>(entry.frames.size()) < kMaxFightTicks && !run.decided()) {
        const InputFrame frame = run.bridge->botInput(run.scene);
        entry.frames.push_back(frame);
        run.scene.tick(kDt, frame);
    }
    entry.expected = run.outcome(static_cast<int>(entry.frames.size()));
    if (!entry.expected.finished) return std::nullopt;
    return entry;
}

Pick pickEntry(const World& world, std::string_view skill, bool playerWins, int pick)
{
    Pick picked;
    int matches = 0;
    for (std::uint64_t seed = 1; seed <= kSearchSeeds; ++seed) {
        std::optional<Entry> entry = recordEntry(world, skill, seed);
        if (!entry) continue;
        if (finishedAs(*entry, playerWins) && matches++ == pick) {
            entry->pick = pick;
            picked.entry = std::move(entry);
            picked.exact = true;
            return picked;
        }
        if (!picked.entry) {
            picked.entry = std::move(entry);
            picked.exact = false;
        }
    }
    return picked;
}

Resolved resolveEntry(const Entry& stored, const World& world)
{
    Resolved resolved;
    const std::uint64_t now = worldHashOf(world);
    const std::string label = entryName(stored);
    if (stored.worldHash == now) {
        resolved.entry = stored;
        return resolved;
    }
    resolved.repicked = true;
    const bool playerWins = !stored.expected.playerLost;
    Pick pick = pickEntry(world, stored.skill, playerWins, stored.pick);
    const std::string moved = std::format("{}: world {} changed (hash {:016x}, now {:016x}), so the recorded fight no longer applies;", label, stored.worldSeed,
        stored.worldHash, now);
    if (!pick.entry) {
        resolved.message = std::format("{} no {} fight finished on scene seeds 1 to {}, nothing to compare", moved, stored.skill, kSearchSeeds);
        return resolved;
    }
    resolved.message = std::format("{} re-picked scene seed {} -> {}{}; run bin/regen-combat-bridge-corpus to refresh the file", moved, stored.sceneSeed,
        pick.entry->sceneSeed, pick.exact ? "" : std::format(" (no seed gave a {}, took the first finished fight)", playerWins ? "player win" : "foe win"));
    resolved.entry = std::move(pick.entry);
    return resolved;
}

std::string entryName(const Entry& entry)
{
    return std::format("{}-world-{}-scene-{}", entry.skill, entry.worldSeed, entry.sceneSeed);
}

std::string encodeEntry(const Entry& entry)
{
    stage0::CorpusEntry body;
    body.name = entryName(entry);
    body.seed = entry.sceneSeed;
    body.frames = entry.frames;
    body.expected = entry.expected;
    return std::format("{} {}\nskill {}\nworld_seed {}\ngrid {}\nworld_hash {:016x}\npick {}\n", kHeader, kFileVersion, entry.skill, entry.worldSeed, entry.grid,
               entry.worldHash, entry.pick)
        + stage0::encodeCorpusEntry(body);
}

std::optional<Entry> decodeEntry(std::string_view text)
{
    const auto magic = fieldValue(text, kHeader);
    int version = 0;
    if (!magic || !parseNumber(*magic, version) || version != kFileVersion) return std::nullopt;
    const auto skill = fieldValue(text, "skill");
    const auto worldSeed = fieldValue(text, "world_seed");
    const auto grid = fieldValue(text, "grid");
    const auto hash = fieldValue(text, "world_hash");
    const auto pick = fieldValue(text, "pick");
    if (!skill || !worldSeed || !grid || !hash || !pick) return std::nullopt;

    Entry entry;
    if (!parseNumber(*worldSeed, entry.worldSeed) || !parseNumber(*grid, entry.grid) || !parseNumber(*hash, entry.worldHash, 16)
        || !parseNumber(*pick, entry.pick)) {
        return std::nullopt;
    }
    if (entry.pick < 0 || entry.pick >= static_cast<int>(kSearchSeeds) || entry.grid < 1 || entry.grid > kMaxGrid || skill->empty() || !stage0::findBotSkill(*skill)) return std::nullopt;
    entry.skill = std::string(*skill);

    const std::optional<stage0::CorpusEntry> body = stage0::decodeCorpusEntry(text);
    if (!body) return std::nullopt;
    entry.sceneSeed = body->seed;
    entry.frames = body->frames;
    entry.expected = body->expected;
    return entry;
}

}
