#pragma once

#include "ichor/core/input.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/death_cause.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

// A regression corpus of recorded bot fights (SEED-466). Each entry is one fight on a fresh scene: the
// scene seed, the player's input frames, and what the fight came to. Replaying the frames on the current
// build must give the same outcome, so an accidental balance or determinism change turns the corpus red.
struct CorpusOutcome {
    bool finished = false; // the fight ended within the recorded frames
    bool playerLost = false;
    DeathCause cause = DeathCause::Unknown;
    int ticks = 0;
    int playerHits = 0;
    int damageTakenTenths = 0;
    std::uint64_t stateHash = 0;

    bool operator==(const CorpusOutcome&) const = default;
};

struct CorpusEntry {
    std::string name;
    std::uint64_t seed = 1;
    std::vector<InputFrame> frames;
    CorpusOutcome expected;
};

constexpr int kCorpusVersion = 1;
constexpr std::size_t kMaxCorpusFrames = 60 * 600;

struct CorpusSpec {
    std::string_view skill;
    std::uint64_t seed;
};

// The fights the corpus holds: bot skill and scene seed, spread over every skill, with wins and losses.
std::span<const CorpusSpec> corpusPlan();

// Plays the frames on a fresh Stage0 scene set up exactly as the bot does and reports how it ended.
CorpusOutcome replayFight(std::uint64_t seed, std::span<const InputFrame> frames, const Tuning& tuning = {});

// Has the named bot skill fight one fight on the seed and records it. Nullopt when the skill is unknown or
// the fight did not finish in time.
std::optional<CorpusEntry> recordCorpusEntry(std::string_view skill, std::uint64_t seed);

// Plain text, run-length encoded, so a diff of a fixture shows what changed.
std::string encodeCorpusEntry(const CorpusEntry& entry);

// Nullopt for anything that is not a well-formed version 1 entry: bad header, missing or repeated fields,
// non-finite floats, a run of zero ticks, or more than kMaxCorpusFrames ticks.
std::optional<CorpusEntry> decodeCorpusEntry(std::string_view text);

}
