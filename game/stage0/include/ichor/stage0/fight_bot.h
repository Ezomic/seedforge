#pragma once

#include "ichor/core/input.h"
#include "ichor/core/rng.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/fight_report.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

// A scripted player for early tuning data. It reads the same fighter state a human sees (the foe's
// wind-up, its distance) through a reaction delay, and answers through the ordinary InputFrame.
// Its results are a rough early signal about balance, not a substitute for the 200-fight protocol.
struct BotSkill {
    std::string name;
    int reactionTicks = 18;   // whole fixed steps before a foe wind-up is noticed
    float blockChance = 0.0f; // share of noticed wind-ups answered with the guard, in [0, 1]
    float dodgeChance = 0.0f; // share of the remaining wind-ups answered with a dodge, in [0, 1]
    float aggression = 0.5f;  // how readily it closes in and swings, in [0, 1]
};

constexpr std::size_t kBotSkillCount = 3;
// novice, regular, expert.
const std::array<BotSkill, kBotSkillCount>& botSkills();
std::optional<BotSkill> findBotSkill(std::string_view name);

// Non-finite or out-of-range values are clamped, so a hostile config cannot reach the sim.
BotSkill sanitise(BotSkill skill);

constexpr int kBotHistoryTicks = 64;

class FightBot {
public:
    FightBot(BotSkill skill, std::uint64_t seed);

    InputFrame next(Scene& scene, const Stage0& stage);

    // Forgets the previous fight's reactions; the random stream carries on.
    void reset();

private:
    enum class Plan : std::uint8_t { None, Block, Dodge };

    BotSkill skill_;
    Rng rng_;
    std::array<std::uint8_t, kBotHistoryTicks> history_{};
    std::uint32_t tick_ = 0;
    std::uint8_t lastSeen_ = 0;
    Plan plan_ = Plan::None;
};

struct BotConfig {
    std::uint64_t seed = 1;
    BotSkill skill = botSkills()[1];
    int fights = 20;
    int maxFightTicks = 60 * 90; // a fight that runs longer is abandoned and counted as a timeout
    std::filesystem::path csv;   // empty keeps the log in memory only
};

struct BotResult {
    int timeouts = 0;
    std::vector<FightRow> rows;
    FightSummary summary;
};

// Plays config.fights fights headless on the real Stage0 loop. Fights that finish are logged by the
// recorder, so the csv (when given) is readable by stage0_report.
BotResult runBotFights(const BotConfig& config);

std::string formatBotResult(const BotConfig& config, const BotResult& result);

}
