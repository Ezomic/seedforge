#pragma once

#include "ichor/traversal/verbs.h"

#include <cstdint>
#include <optional>

namespace ichor {

// What the diver carries. Each tier reaches deeper than the one before it; Bare is plain swimming.
enum class DiveGear : std::uint8_t {
    Bare,
    Breath, // a held breath trained for diving
    Helmet,
    PressureSuit,
    Orichalcum, // Atlantean pressure plate, the Flood + Orichalcum synergy
};

struct GearProfile {
    float maxDepth; // metres, inclusive
    int airMs;      // air at the surface rate, in milliseconds
};

constexpr float kMaxDepthMetres = 1000.0f; // any deeper depth, or infinity, reads as this
constexpr float kMaxDwellSeconds = 1.0e6f;

enum class DepthCheck : std::uint8_t {
    Reachable,
    TooDeep,   // beyond what the gear reaches
    NeedsDive, // the gear would reach it, but the Dive capability is missing
};

// An out of range gear value reads as Bare.
GearProfile gearProfile(DiveGear gear);

// Depth that is not a number, or is negative, reads as zero.
DepthCheck checkDepth(float depth, DiveGear gear, const CapabilitySet& capabilities);

// The deepest water this gear and these capabilities reach.
float deepestReachable(DiveGear gear, const CapabilitySet& capabilities);

// The lightest gear that reaches this depth, or nothing if no gear does.
std::optional<DiveGear> minimumGear(float depth);

// How many thousandths of the surface rate air is spent at this depth. Zero at the surface, over 1000 below it.
int airDrainPermille(float depth);

// Air left after dwelling this many seconds at a constant depth. Never negative.
int airRemainingMs(DiveGear gear, float depth, float dwellSeconds);

// Seconds a full supply lasts at a constant depth, rounded down. Zero at the surface reads as the full supply.
int secondsOfAir(DiveGear gear, float depth);

}
