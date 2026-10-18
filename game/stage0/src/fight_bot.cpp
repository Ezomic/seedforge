#include "ichor/stage0/fight_bot.h"

#include "ichor/sim/components.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace ichor::stage0 {

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr float kDodgeLeadSeconds = 0.18f; // dodge this close to the foe's active frames
constexpr float kApproachDistance = 0.9f;  // of the weapon reach
constexpr float kBackOffDistance = 1.1f;
constexpr float kSwingChancePerTick = 0.15f;
constexpr float kStaminaReserve = 25.0f; // kept back for a dodge unless the bot is reckless
constexpr int kSettleTicks = 30;
constexpr int kMaxFights = 100000;
constexpr int kMaxFightTicks = 60 * 3600;

float unit(float value, float fallback)
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

glm::vec2 flat(glm::dvec3 from, glm::dvec3 to)
{
    return {static_cast<float>(to.x - from.x), static_cast<float>(to.z - from.z)};
}

InputFrame steer(InputFrame frame, glm::vec2 worldXz)
{
    frame.moveX = worldXz.x;
    frame.moveY = -worldXz.y;
    return frame;
}

bool threatening(std::uint8_t state)
{
    return state == static_cast<std::uint8_t>(CombatState::Windup) || state == static_cast<std::uint8_t>(CombatState::Active);
}

}

const std::array<BotSkill, kBotSkillCount>& botSkills()
{
    static const std::array<BotSkill, kBotSkillCount> skills{{
        {"novice", 36, 0.05f, 0.05f, 0.1f},
        {"regular", 14, 0.30f, 0.20f, 0.1f},
        {"expert", 8, 0.50f, 0.50f, 0.1f},
    }};
    return skills;
}

std::optional<BotSkill> findBotSkill(std::string_view name)
{
    for (const BotSkill& skill : botSkills()) {
        if (skill.name == name) return skill;
    }
    return std::nullopt;
}

BotSkill sanitise(BotSkill skill)
{
    skill.reactionTicks = std::clamp(skill.reactionTicks, 0, kBotHistoryTicks - 1);
    skill.blockChance = unit(skill.blockChance, 0.0f);
    skill.dodgeChance = unit(skill.dodgeChance, 0.0f);
    skill.aggression = unit(skill.aggression, 0.5f);
    return skill;
}

FightBot::FightBot(BotSkill skill, std::uint64_t seed)
    : skill_(sanitise(std::move(skill)))
    , rng_(Rng(seed).derive("stage0.bot"))
{
}

void FightBot::reset()
{
    history_.fill(0);
    tick_ = 0;
    lastSeen_ = 0;
    plan_ = Plan::None;
}

InputFrame FightBot::next(Scene& scene, const Stage0& stage)
{
    InputFrame frame;
    auto& registry = scene.registry();
    const entt::entity meEntity = scene.find(stage.playerId());
    const entt::entity foeEntity = scene.find(stage.foeId());
    if (meEntity == entt::null || foeEntity == entt::null) return frame;
    const Combatant& me = registry.get<Combatant>(meEntity);
    const Combatant& foe = registry.get<Combatant>(foeEntity);
    if (!me.alive() || !foe.alive()) return frame;

    const Tuning& tuning = stage.tuning();
    const glm::dvec3 here = registry.get<Transform>(meEntity).position;
    const glm::dvec3 there = registry.get<Transform>(foeEntity).position;
    const glm::vec2 toFoe = flat(here, there);
    const float distance = glm::length(toFoe);
    const glm::vec2 heading = distance > 1e-4f ? toFoe / distance : glm::vec2(0.0f, -1.0f);

    history_[tick_ % kBotHistoryTicks] = foe.state;
    const std::uint32_t delay = static_cast<std::uint32_t>(skill_.reactionTicks);
    const std::uint8_t seen = tick_ >= delay ? history_[(tick_ - delay) % kBotHistoryTicks] : static_cast<std::uint8_t>(CombatState::Idle);
    tick_ += 1;

    const std::uint8_t windup = static_cast<std::uint8_t>(CombatState::Windup);
    const float reach = tuning.attackRange + tuning.attackRadius;
    if (seen == windup && lastSeen_ != windup && plan_ == Plan::None) {
        const float blockRoll = rng_.nextFloat();
        const float dodgeRoll = rng_.nextFloat();
        if (distance <= reach + 1.0f) {
            if (blockRoll < skill_.blockChance) plan_ = Plan::Block;
            else if (dodgeRoll < skill_.dodgeChance) plan_ = Plan::Dodge;
        }
    }
    lastSeen_ = seen;

    const bool idle = me.combatState() == CombatState::Idle;
    if (plan_ == Plan::Block) {
        if (!threatening(seen)) plan_ = Plan::None;
        else if (idle) {
            frame.buttons |= InputFrame::Block;
            return frame;
        }
    } else if (plan_ == Plan::Dodge) {
        if (!threatening(foe.state)) plan_ = Plan::None;
        else if (idle && me.stamina >= tuning.dodgeStaminaCost) {
            const bool heavy = foe.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
            const float total = heavy ? tuning.heavyWindup : tuning.lightWindup;
            const bool due = foe.combatState() == CombatState::Active || total - foe.stateTime <= kDodgeLeadSeconds;
            if (due) {
                const glm::vec2 side(-heading.y, heading.x);
                const float toward = glm::dot(glm::vec2(static_cast<float>(here.x), static_cast<float>(here.z)), side);
                frame.buttons |= InputFrame::Dodge;
                frame = steer(frame, toward > 0.0f ? -side : side);
                plan_ = Plan::None;
                return frame;
            }
        }
    }

    if (!idle) return frame;

    const bool foeBusy = threatening(seen);
    const bool foeOpen = seen == static_cast<std::uint8_t>(CombatState::Recovery) || seen == static_cast<std::uint8_t>(CombatState::Staggered);
    if (foeBusy) {
        if (distance < kBackOffDistance + 0.5f) frame = steer(frame, -heading);
        return frame;
    }
    if (distance <= reach * kApproachDistance) {
        const bool heavy = foeOpen && me.stamina >= tuning.heavyStaminaCost + kStaminaReserve * (1.0f - skill_.aggression);
        const float cost = heavy ? tuning.heavyStaminaCost : tuning.lightStaminaCost;
        const bool afford = me.stamina >= cost + kStaminaReserve * (1.0f - skill_.aggression);
        const float chance = skill_.aggression * kSwingChancePerTick * (foeOpen ? 3.0f : 1.0f);
        if (afford && rng_.nextFloat() < chance) {
            frame.buttons |= heavy ? InputFrame::Heavy : InputFrame::Attack;
        } else if (distance < kBackOffDistance) {
            frame = steer(frame, -heading);
        }
        return frame;
    }
    frame = steer(frame, heading);
    return frame;
}

BotResult runBotFights(const BotConfig& config)
{
    BotResult result;
    const int fights = std::clamp(config.fights, 0, kMaxFights);
    const int maxTicks = std::clamp(config.maxFightTicks, 1, kMaxFightTicks);
    const BotSkill skill = sanitise(config.skill);

    Scene scene(config.seed);
    Stage0 stage;
    stage.setLegacyControls(true); // the bot steers in world axes and needs the foe in front of it
    stage.recorder().open(config.csv);
    stage.recorder().setSession({cleanField("bot-" + skill.name), cleanField(std::format("seed-{}", config.seed))});
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(kDt);
    stage.resetFight(scene);
    for (int i = 0; i < kSettleTicks; ++i) scene.tick(kDt);

    FightBot bot(skill, config.seed);
    InputFrame confirm;
    confirm.buttons = InputFrame::Confirm;

    for (int attempt = 0; attempt < fights; ++attempt) {
        const int before = stage.recorder().fightsWritten();
        bool finished = false;
        for (int tick = 0; tick < maxTicks; ++tick) {
            scene.tick(kDt, bot.next(scene, stage));
            if (stage.recorder().fightsWritten() > before) {
                finished = true;
                break;
            }
        }
        if (finished) {
            const std::vector<FightRow> parsed = parseFightLog(stage.recorder().lastRow());
            result.rows.insert(result.rows.end(), parsed.begin(), parsed.end());
        } else {
            result.timeouts += 1;
        }
        scene.tick(kDt, confirm);
        scene.tick(kDt);
        bot.reset();
    }
    result.summary = summarise(result.rows);
    return result;
}

std::string formatBotResult(const BotConfig& config, const BotResult& result)
{
    const BotSkill skill = sanitise(config.skill);
    std::string text = std::format("bot {} (reaction {} ticks, block {:.2f}, dodge {:.2f}, aggression {:.2f}), seed {}, {} fights played, {} timed out\n",
        skill.name, skill.reactionTicks, skill.blockChance, skill.dodgeChance, skill.aggression,
        config.seed, std::clamp(config.fights, 0, kMaxFights), result.timeouts);
    text += formatReport(result.summary);
    text += "note: bot results are a rough early signal, not a substitute for the 200-fight protocol.\n";
    return text;
}

}
