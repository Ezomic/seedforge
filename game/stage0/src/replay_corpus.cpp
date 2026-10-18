#include "ichor/stage0/replay_corpus.h"

#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/fight_bot.h"

#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <system_error>
#include <type_traits>

namespace ichor::stage0 {

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr int kSettleTicks = 30;
constexpr std::string_view kHeader = "ichor-stage0-corpus";
constexpr std::size_t kFieldLines = 10; // header plus nine fields; run lines follow

template <typename T>
bool parseNumber(std::string_view text, T& out, int base = 10)
{
    T value{};
    std::from_chars_result result;
    if constexpr (std::is_floating_point_v<T>) {
        result = std::from_chars(text.data(), text.data() + text.size(), value);
    } else {
        result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    }
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return false;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) return false;
    }
    out = value;
    return true;
}

std::vector<std::string_view> split(std::string_view line)
{
    std::vector<std::string_view> parts;
    while (true) {
        const std::size_t end = line.find(' ');
        parts.push_back(line.substr(0, end));
        if (end == std::string_view::npos) break;
        line.remove_prefix(end + 1);
    }
    return parts;
}

std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return lines;
}

std::optional<DeathCause> causeFromName(std::string_view text)
{
    for (std::size_t i = 0; i < kDeathCauseCount; ++i) {
        const DeathCause cause = static_cast<DeathCause>(i);
        if (text == name(cause)) return cause;
    }
    return std::nullopt;
}

std::optional<std::string_view> fieldValue(std::string_view line, std::string_view key)
{
    if (!line.starts_with(key) || line.size() <= key.size() + 1 || line[key.size()] != ' ') return std::nullopt;
    return line.substr(key.size() + 1);
}

}

std::span<const CorpusSpec> corpusPlan()
{
    static constexpr CorpusSpec plan[] = {
        {"novice", 1}, {"novice", 2}, {"novice", 3}, {"novice", 4}, {"novice", 5}, {"novice", 6}, {"novice", 7},
        {"regular", 1}, {"regular", 2}, {"regular", 3}, {"regular", 4}, {"regular", 5}, {"regular", 6}, {"regular", 7},
        {"expert", 1}, {"expert", 2}, {"expert", 3}, {"expert", 4}, {"expert", 5}, {"expert", 6},
    };
    return plan;
}

CorpusOutcome replayFight(std::uint64_t seed, std::span<const InputFrame> frames, const Tuning& tuning)
{
    Scene scene(seed);
    Stage0 stage(tuning);
    stage.setLegacyControls(true);
    stage.recorder().open({});
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(kDt);
    stage.resetFight(scene);
    for (int i = 0; i < kSettleTicks; ++i) scene.tick(kDt);

    CorpusOutcome outcome;
    for (const InputFrame& frame : frames) {
        scene.tick(kDt, frame);
        outcome.ticks += 1;
        if (stage.recorder().fightsWritten() > 0) {
            outcome.finished = true;
            break;
        }
    }
    outcome.stateHash = scene.stateHash();
    if (outcome.finished) {
        const std::vector<FightRow> rows = parseFightLog(stage.recorder().lastRow());
        if (rows.size() == 1) {
            outcome.playerLost = rows[0].playerLost;
            outcome.cause = rows[0].cause;
            outcome.playerHits = rows[0].playerHits;
            outcome.damageTakenTenths = static_cast<int>(std::lround(rows[0].playerDamageTaken * 10.0));
        } else {
            outcome.finished = false;
        }
    }
    return outcome;
}

std::optional<CorpusEntry> recordCorpusEntry(std::string_view skill, std::uint64_t seed)
{
    const std::optional<BotSkill> found = findBotSkill(skill);
    if (!found) return std::nullopt;
    BotConfig config;
    config.skill = *found;
    config.seed = seed;
    config.fights = 1;
    config.maxFightTicks = static_cast<int>(kMaxCorpusFrames);
    config.captureFirstFight = true;
    BotResult result = runBotFights(config);
    if (result.timeouts != 0 || result.firstFightInputs.empty()) return std::nullopt;

    CorpusEntry entry;
    entry.name = std::format("{}-seed-{}", skill, seed);
    entry.seed = seed;
    entry.frames = std::move(result.firstFightInputs);
    entry.expected = replayFight(seed, entry.frames);
    if (!entry.expected.finished) return std::nullopt;
    return entry;
}

std::string encodeCorpusEntry(const CorpusEntry& entry)
{
    const CorpusOutcome& out = entry.expected;
    std::string text = std::format("{} {}\nname {}\nseed {}\nwinner {}\ncause {}\nticks {}\nplayer_hits {}\ndamage_taken_tenths {}\nhash {:016x}\n",
        kHeader, kCorpusVersion, entry.name, entry.seed, out.playerLost ? "foe" : "player", name(out.cause), out.ticks, out.playerHits,
        out.damageTakenTenths, out.stateHash);
    std::string runs;
    std::size_t runCount = 0;
    for (std::size_t i = 0; i < entry.frames.size();) {
        std::size_t end = i + 1;
        while (end < entry.frames.size() && entry.frames[end] == entry.frames[i]) ++end;
        const InputFrame& f = entry.frames[i];
        runs += std::format("{} {} {} {} {}\n", end - i, f.buttons, f.moveX, f.moveY, f.lookX);
        runCount += 1;
        i = end;
    }
    return text + std::format("runs {}\n", runCount) + runs;
}

std::optional<CorpusEntry> decodeCorpusEntry(std::string_view text)
{
    std::vector<std::string_view> lines = splitLines(text);
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.size() < kFieldLines) return std::nullopt;

    const auto header = fieldValue(lines[0], kHeader);
    int version = 0;
    if (!header || !parseNumber(*header, version) || version != kCorpusVersion) return std::nullopt;

    const auto vName = fieldValue(lines[1], "name");
    const auto vSeed = fieldValue(lines[2], "seed");
    const auto vWinner = fieldValue(lines[3], "winner");
    const auto vCause = fieldValue(lines[4], "cause");
    const auto vTicks = fieldValue(lines[5], "ticks");
    const auto vHits = fieldValue(lines[6], "player_hits");
    const auto vDamage = fieldValue(lines[7], "damage_taken_tenths");
    const auto vHash = fieldValue(lines[8], "hash");
    const auto vRuns = fieldValue(lines[9], "runs");
    if (!vName || !vSeed || !vWinner || !vCause || !vTicks || !vHits || !vDamage || !vHash || !vRuns) return std::nullopt;

    CorpusEntry entry;
    entry.name = std::string(*vName);
    std::size_t runCount = 0;
    if (!parseNumber(*vSeed, entry.seed) || !parseNumber(*vTicks, entry.expected.ticks) || !parseNumber(*vHits, entry.expected.playerHits)
        || !parseNumber(*vDamage, entry.expected.damageTakenTenths) || !parseNumber(*vHash, entry.expected.stateHash, 16)
        || !parseNumber(*vRuns, runCount)) {
        return std::nullopt;
    }
    if (*vWinner != "player" && *vWinner != "foe") return std::nullopt;
    const auto cause = causeFromName(*vCause);
    if (!cause || runCount != lines.size() - kFieldLines || entry.expected.ticks < 0) return std::nullopt;
    entry.expected.playerLost = *vWinner == "foe";
    entry.expected.cause = *cause;
    entry.expected.finished = true;

    for (std::size_t i = kFieldLines; i < lines.size(); ++i) {
        const std::vector<std::string_view> parts = split(lines[i]);
        if (parts.size() != 5) return std::nullopt;
        std::size_t count = 0;
        InputFrame frame;
        if (!parseNumber(parts[0], count) || count == 0 || count > kMaxCorpusFrames - entry.frames.size() || !parseNumber(parts[1], frame.buttons)
            || !parseNumber(parts[2], frame.moveX) || !parseNumber(parts[3], frame.moveY) || !parseNumber(parts[4], frame.lookX)) {
            return std::nullopt;
        }
        entry.frames.insert(entry.frames.end(), count, frame);
    }
    if (entry.frames.size() != static_cast<std::size_t>(entry.expected.ticks)) return std::nullopt;
    return entry;
}

}
