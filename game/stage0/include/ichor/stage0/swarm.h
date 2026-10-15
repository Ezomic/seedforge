#pragma once

#include "ichor/core/reflect.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <vector>

namespace ichor::stage0 {

// Swarm: individually trivial, dangerous in numbers. All timing is whole ticks
// and all ordering is by id, so a group steps identically on every platform.
constexpr std::int32_t kSwarmNeverGranted = 1 << 20;

struct SwarmTuning {
    int maxAttackers = 2;         // attack tokens: at most this many units are committed at once
    int tokenGapTicks = 20;       // minimum ticks between two token grants, so strikes never start together
    int attackCooldownTicks = 90; // counted from the end of recovery before the unit is eligible for a token again
    int windupTicks = 30;
    int recoveryTicks = 40;
    float aggroRange = 14.0f;
    float tokenRange = 4.0f;      // only units this close to the target can be handed a token
    float surroundRadius = 2.0f;
    float strikeRange = 1.2f;
    float walkSpeed = 3.0f;
    float tickSeconds = 1.0f / 60.0f;
};

enum class SwarmState : std::uint8_t { Unaware, Approach, Surround, Strike, Recover };
const char* name(SwarmState state);

struct SwarmBrain {
    std::uint8_t state = 0;
    std::int32_t stateTicks = 0;
    std::int32_t cooldownTicks = 0;
    bool token = false;
    std::int32_t strikes = 0;

    SwarmState swarmState() const { return static_cast<SwarmState>(state); }
};

struct SwarmUnit {
    std::uint64_t id = 0;
    glm::vec2 pos{0.0f};
    bool alive = true;
    bool holdsToken = false;
    bool engaged = true; // only units already in position are handed a token
    std::int32_t cooldownTicks = 0;
    std::int32_t strikes = 0; // strikes so far: the unit with fewest goes first, so nobody starves
};

struct TokenResult {
    std::vector<std::uint64_t> holders; // ascending by id
    bool granted = false;
    std::uint64_t newHolder = 0;
};

// Pure: group state in, token holders out. Independent of the order of `units`.
// Live holders keep their token (the lowest ids if there are more than the cap);
// one new token is granted when a slot is free and the gap since the last grant
// has passed, to the eligible unit with the fewest strikes (then nearest, then
// lower id). Dead and non-finite units never hold one; a repeated id counts once.
TokenResult assignAttackTokens(const std::vector<SwarmUnit>& units, glm::vec2 target, const SwarmTuning& tuning, std::int32_t ticksSinceGrant);

// Position on the ring around the target for the unit ranked `rank` of `count`
// (rank is the unit's index among live members sorted by id).
glm::vec2 surroundSlot(int rank, int count, glm::vec2 target, float radius);

struct SwarmSenses {
    glm::vec2 self{0.0f};
    glm::vec2 target{0.0f};
    glm::vec2 slot{0.0f};
    bool targetAlive = true;
    bool mayStrike = true; // false when another unit began its wind-up this tick
};

struct SwarmIntent {
    glm::vec2 move{0.0f}; // metres per second
    bool strike = false;  // the wind-up finished this tick
};

// One unit's decision layer. Reads brain.token, writes everything else.
SwarmIntent swarmStep(SwarmBrain& brain, const SwarmSenses& senses, const SwarmTuning& tuning);

struct SwarmMember {
    std::uint64_t id = 0;
    glm::vec2 pos{0.0f};
    bool alive = true;
    SwarmBrain brain;
};

struct SwarmTarget {
    glm::vec2 pos{0.0f};
    bool alive = true;
};

struct SwarmGroup {
    std::vector<SwarmMember> members;
    std::int32_t ticksSinceGrant = kSwarmNeverGranted;
};

struct SwarmEvents {
    std::vector<std::uint64_t> granted;
    std::vector<std::uint64_t> strikes; // ascending by id
};

// One fixed tick for the whole group: tokens, slots, every brain, movement.
SwarmEvents tickSwarm(SwarmGroup& group, SwarmTarget target, const SwarmTuning& tuning);

}

namespace ichor {

template <>
struct Reflect<stage0::SwarmBrain> {
    static constexpr auto fields()
    {
        using B = stage0::SwarmBrain;
        return std::make_tuple(field("state", &B::state), field("stateTicks", &B::stateTicks), field("cooldownTicks", &B::cooldownTicks),
            field("token", &B::token), field("strikes", &B::strikes));
    }
};

}
