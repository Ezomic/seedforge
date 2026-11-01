#pragma once

#include "ichor/data/data_set.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ichor {

// The playthrough bot (SEED-748): the SEED-492 walk bot (forged_walk_bot.h) carried on past the settlement. In one headless
// run over a forged world it walks to the quest giver, talks, accepts the errand, walks to the dungeon and fights its first
// room with enemies clear, picks the errand's item up, saves, quits and loads, in that order, and says of every step whether
// it was done, failed, stalled or has no headless driver yet. It stands for a player in the nightly and opens no window: the
// overworld legs are the walk bot's over the walking rules, the dungeon is walked by the player's physics body, and the scene
// is placed only where a leg ended, as test_parley_playthrough does. The fight is the combat bridge's scripted player.
//
// A step with no driver is reported as Unsupported, never skipped in silence. Today that is Quit: the app quits through the
// pause menu in main.cpp (Platform::requestQuit) and nothing below the app can be told to quit, so the bot tears the headless
// game down (the scene and every system) and the Load step opens a fresh one, which is what a quit and a relaunch leave
// behind without being one.
enum class PlaythroughStep : std::uint8_t { Walk, Talk, Accept, Fight, Loot, Save, Quit, Load };
constexpr std::size_t kPlaythroughSteps = 8;
const char* playthroughStepName(PlaythroughStep step);

enum class PlaythroughStatus : std::uint8_t {
    NotRun,      // an earlier step ended the run
    Done,
    Failed,      // the step could not be completed; detail says why
    Stalled,     // nothing changed for stallSeconds, the walk timed out while still moving, or the step's time ran out
    Unsupported, // no headless driver for it yet; detail names what is missing and what stood in
};
const char* playthroughStatusName(PlaythroughStatus status);

struct PlaythroughStepResult {
    PlaythroughStep step = PlaythroughStep::Walk;
    PlaythroughStatus status = PlaythroughStatus::NotRun;
    std::string detail;
    double wallSeconds = 0.0;
    double simSeconds = 0.0; // ticks the step took, as scene time
};

struct PlaythroughParams {
    int grid = 128;
    std::string skill = "expert";   // the fight bot's skill (stage0::findBotSkill)
    std::uint64_t botSeed = 1;      // its random stream
    int maxDeaths = 3;              // deaths allowed in the dungeon over the fight and loot steps; one more fails the step
    double stallSeconds = 15.0;     // scene seconds with nothing moving before a dungeon step is stalled
    double maxStepSeconds = 600.0;  // scene seconds a dungeon step may take in all
    double maxForgeSeconds = 600.0; // wall
    std::filesystem::path saveDir;  // where the slot is written; empty picks a folder under the temp directory, removed afterwards
};

struct PlaythroughResult {
    std::uint64_t seed = 0;
    std::string worldError; // the forge or the game refused the seed, so no step ran
    std::array<PlaythroughStepResult, kPlaythroughSteps> steps{};
    double wallSeconds = 0.0;

    const PlaythroughStepResult& of(PlaythroughStep step) const { return steps[static_cast<std::size_t>(step)]; }
    // Every step Done or Unsupported.
    bool completed() const;
    // "walk ok, talk ok, accept ok, fight ok, loot ok, save ok, quit UNSUPPORTED, load ok".
    std::string summary() const;
};

// Plays the whole run in the forged world of `seed`. `data` is the game's data set, `dataDir` the folder it was loaded from.
PlaythroughResult playThrough(std::uint64_t seed, const DataSet& data, const std::filesystem::path& dataDir, const PlaythroughParams& params = {});

}
