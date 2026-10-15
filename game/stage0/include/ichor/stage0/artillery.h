#pragma once

#include "ichor/core/reflect.h"

#include <glm/vec2.hpp>

#include <cstdint>

namespace ichor::stage0 {

// Artillery: ranged pressure that forces movement and dies fast when reached. It holds a
// distance band, locks a ground point when it starts aiming, and lands a blast there after a
// fixed telegraph; the answer is to leave the marked point, break line of sight, or close in.
// All timing is whole ticks.
struct ArtilleryTuning {
    float maxHealth = 40.0f;
    float maxPoise = 50.0f;
    float aggroRange = 26.0f;
    float minRange = 7.0f;    // closer than this it backs away and does not fire
    float maxRange = 18.0f;   // farther than this it moves up
    float blastRadius = 2.0f;
    float blastDamage = 25.0f;
    float blastPoiseDamage = 20.0f;
    float walkSpeed = 3.0f;
    float retreatSpeed = 4.5f;
    int windupTicks = 50;
    int recoveryTicks = 40;
    int cooldownTicks = 90;
    float tickSeconds = 1.0f / 60.0f;
};

enum class ArtilleryState : std::uint8_t { Unaware, Reposition, Aim, Recover, Staggered };
const char* name(ArtilleryState state);

struct ArtilleryBrain {
    std::uint8_t state = 0;
    std::int32_t stateTicks = 0;
    std::int32_t cooldownTicks = 0;
    std::int32_t shots = 0;
    float aimX = 0.0f; // the ground point locked when the aim began
    float aimY = 0.0f;

    ArtilleryState artilleryState() const { return static_cast<ArtilleryState>(state); }
};

struct ArtillerySenses {
    glm::vec2 self{0.0f};
    glm::vec2 target{0.0f};
    bool targetAlive = true;
    bool lineOfSight = true; // the host reports an unobstructed shot to the target
    bool staggered = false;  // the host reports the artillery's poise has broken
};

struct ArtilleryIntent {
    glm::vec2 move{0.0f};   // metres per second
    bool telegraph = false; // the aim began this tick: show the marker at aimAt
    bool fire = false;      // the aim finished this tick
    bool hits = false;      // with fire: the target is inside the blast radius of aimAt
    glm::vec2 aimAt{0.0f};  // the locked ground point, set on telegraph and fire
};

// One fixed tick of the decision layer. Non-finite positions change nothing.
ArtilleryIntent artilleryStep(ArtilleryBrain& brain, const ArtillerySenses& senses, const ArtilleryTuning& tuning);

}

namespace ichor {

template <>
struct Reflect<stage0::ArtilleryBrain> {
    static constexpr auto fields()
    {
        using B = stage0::ArtilleryBrain;
        return std::make_tuple(field("state", &B::state), field("stateTicks", &B::stateTicks), field("cooldownTicks", &B::cooldownTicks),
            field("shots", &B::shots), field("aimX", &B::aimX), field("aimY", &B::aimY));
    }
};

}
