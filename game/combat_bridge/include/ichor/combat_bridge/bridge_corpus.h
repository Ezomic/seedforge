#pragma once

#include "ichor/core/input.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/replay_corpus.h"
#include "ichor/world/forged_game.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::bridge_corpus {

// A regression corpus of bot fights in forged worlds (SEED-659), the counterpart of the Stage 0 corpus (SEED-466) for
// the combat bridge. An entry is one fight on a fresh scene in one forged world: the input frames and what the fight
// came to. Replaying the frames on the current build must give the same outcome.
//
// The forged world can change under the corpus (a worldgen change moves the terrain, the settlement or the combat
// site). The file records the world hash it was made on. When the world built now has another hash, `resolveEntry`
// does not compare against stale numbers: it searches the scene seeds again, in order, for a fight with the same
// skill and winner, and says so. The re-pick is a pure function of the world, so it is the same on every machine.
constexpr int kFileVersion = 1;
constexpr int kGridSize = 64;
constexpr int kMaxFightTicks = 60 * 120;
constexpr std::uint64_t kSearchSeeds = 64; // scene seeds 1 to this are tried, in order, by a pick

using Outcome = stage0::CorpusOutcome;

struct Slot {
    std::string_view skill;
    std::uint64_t worldSeed;
    bool playerWins; // the winner the search looks for
    int pick;        // which of the scene seeds with that winner, counting from 0: slots that share a skill, world and winner differ here
};

// The 20 fights the corpus holds: bot skill, world and winner, in file order.
std::span<const Slot> plan();

struct Entry {
    std::string skill;
    std::uint64_t worldSeed = 1;
    int grid = kGridSize;
    std::uint64_t worldHash = 0;
    std::uint64_t sceneSeed = 1; // seeds the scene and the bot; chosen by search
    int pick = 0;                // the slot's pick, kept so a re-pick in a changed world finds the same slot
    std::vector<InputFrame> frames;
    Outcome expected;
};

struct World {
    std::unique_ptr<ForgedGame> game;
    std::uint64_t seed = 1;
    int grid = kGridSize;
};

// The forged world for a seed and grid; its game is null with `error` set when it did not forge.
World forgeWorld(std::uint64_t seed, int grid, std::string& error);

std::uint64_t worldHashOf(const World& world);

// Plays the frames on a fresh scene in the world, set up exactly as the bot is, and reports how the fight ended.
Outcome replayFight(const World& world, std::uint64_t sceneSeed, std::span<const InputFrame> frames, const stage0::Tuning& tuning = {});

// Has the named skill fight one fight in the world on the scene seed and records it. Nullopt when the skill is unknown,
// the world has no combat site or the fight did not finish in time.
std::optional<Entry> recordEntry(const World& world, std::string_view skill, std::uint64_t sceneSeed);

struct Pick {
    std::optional<Entry> entry;
    bool exact = true; // the entry has the winner asked for; false when no seed gave it and the first finished fight stands in
};

// The scene seed whose fight is the `pick`th (from 0) to end the wanted way, else the first that finishes at all.
Pick pickEntry(const World& world, std::string_view skill, bool playerWins, int pick);

struct Resolved {
    std::optional<Entry> entry; // what to replay and compare; empty when no fight can be found in this world
    bool repicked = false;
    std::string message; // empty when the stored entry still stands
};

// The stored entry itself when the world hash matches; otherwise a re-pick for the same skill, world seed and winner,
// with a message naming the old and new hash and scene seed.
Resolved resolveEntry(const Entry& stored, const World& world);

// "skill-world-W-scene-S": the entry's name in the file and the corpus folder.
std::string entryName(const Entry& entry);

// Plain text, run-length encoded after a six line header, so a diff of a fixture shows what changed.
std::string encodeEntry(const Entry& entry);

// Nullopt for anything that is not a well-formed version 1 entry (see stage0::decodeCorpusEntry for the body).
std::optional<Entry> decodeEntry(std::string_view text);

// Every *.replay file of the folder in file name order. Empty with `error` set when the folder cannot be read or a file is malformed.
std::vector<Entry> loadCorpus(const std::filesystem::path& dir, std::string& error);

struct ReplayLine {
    std::string file; // the file's stem, "01-novice-world-1-scene-1"
    std::uint64_t worldHash = 0;
    std::uint64_t sceneSeed = 0;
    bool repicked = false;
    Outcome expected;
    Outcome actual;
};

// What replaying the whole corpus on this build came to, fight by fight (SEED-743). CI runs it on every platform and compares the
// texts, so a fight that replays differently on one OS is caught even when the world moved and every file was re-picked.
struct ReplayReport {
    std::vector<ReplayLine> lines;
    std::vector<std::string> warnings; // the re-pick messages of resolveEntry
    std::string error;                 // set when the folder did not load, held no fight or a world did not forge; the lines are then incomplete

    bool matches() const; // every replayed outcome equals the recorded one
    std::string text() const; // one line per fight in file order, LF only, the same on every platform when the fights agree
};

// Forges each world of the corpus once and replays every fight against its file (through resolveEntry).
ReplayReport replayCorpus(const std::filesystem::path& dir);

}
