#pragma once

#include "ichor/core/reflect.h"

#include <glm/vec2.hpp>

#include <cstdint>

namespace ichor::stage0 {

// Anchor: slow, high poise, controls space. It plants and slams in an area; the
// answer is to break its poise or to punish the long recovery. All timing is whole ticks.
struct AnchorTuning {
    float maxHealth = 160.0f;
    float maxPoise = 300.0f; // the stagger economy: a standard fighter has 100
    float aggroRange = 14.0f;
    float slamRange = 3.5f;  // starts a wind-up when the target is this close
    float slamRadius = 2.5f; // area the slam covers
    float slamDamage = 45.0f;
    float slamPoiseDamage = 80.0f;
    float walkSpeed = 1.6f;
    int windupTicks = 60;
    int recoveryTicks = 90;
    int cooldownTicks = 60;  // held in place after recovery before the next wind-up
    float tickSeconds = 1.0f / 60.0f;
};

enum class AnchorState : std::uint8_t { Unaware, Advance, Windup, Recover, Hold, Staggered };
const char* name(AnchorState state);

struct AnchorBrain {
    std::uint8_t state = 0;
    std::int32_t stateTicks = 0;
    std::int32_t cooldownTicks = 0;
    std::int32_t slams = 0;

    AnchorState anchorState() const { return static_cast<AnchorState>(state); }
};

struct AnchorSenses {
    glm::vec2 self{0.0f};
    glm::vec2 target{0.0f};
    bool targetAlive = true;
    bool staggered = false; // the host reports the anchor's poise has broken
};

struct AnchorIntent {
    glm::vec2 move{0.0f}; // metres per second
    bool telegraph = false; // the wind-up began this tick: play the cue
    bool slam = false;      // the wind-up finished this tick
    bool reaches = false;   // with slam: the target is inside the slam radius
};

// One fixed tick of the decision layer. Non-finite positions change nothing.
AnchorIntent anchorStep(AnchorBrain& brain, const AnchorSenses& senses, const AnchorTuning& tuning);

}

namespace ichor {

template <>
struct Reflect<stage0::AnchorBrain> {
    static constexpr auto fields()
    {
        using B = stage0::AnchorBrain;
        return std::make_tuple(field("state", &B::state), field("stateTicks", &B::stateTicks), field("cooldownTicks", &B::cooldownTicks),
            field("slams", &B::slams));
    }
};

}
