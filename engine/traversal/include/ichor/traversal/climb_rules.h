#pragma once

#include "ichor/traversal/verbs.h"

#include <cstdint>

namespace ichor {

// A climb or controlled descent is only ever made on a designated surface, never on free rock.
struct ClimbSurface {
    bool designated = false;
    float slopeDegrees = 0.0f;
    float heightMetres = 0.0f; // vertical extent of the surface
};

struct ClimbLimits {
    float minSlopeDegrees = kSteepSlopeDegrees; // gentler than this is walked, not climbed
    float maxSlopeDegrees = 90.0f;              // an overhang is never climbable
    float maxClimbMetres = 30.0f;
    float maxDescentMetres = 60.0f;
    float climbStaminaPerMetre = 4.0f;   // at minSlopeDegrees; doubles towards maxSlopeDegrees
    float descentStaminaPerMetre = 1.0f; // same slope scaling
    float safeFallMetres = 3.0f;         // up to here a fall costs nothing
    float lethalFallMetres = 20.0f;      // from here a fall kills

    bool valid() const;
};

enum class ClimbDirection : std::uint8_t { Up, Down };

enum class ClimbBlock : std::uint8_t {
    None,
    InvalidInput,  // NaN, infinite or negative surface, stamina or limits
    NoCapability,  // Climb for up, Descend for down
    NotDesignated,
    TooGentle,     // walkable, no climb needed
    TooSteep,      // beyond maxSlopeDegrees
    TooHigh,
    NoStamina,
};

struct ClimbAssessment {
    ClimbBlock block = ClimbBlock::None;
    float staminaCost = 0.0f; // for the whole surface; zero when blocked before it could be priced

    bool allowed() const { return block == ClimbBlock::None; }
};

// Pure. Checks are made in the order of the enum above; the first that fails is reported.
ClimbAssessment assessClimb(const ClimbLimits& limits, const CapabilitySet& capabilities, ClimbDirection direction,
    const ClimbSurface& surface, float staminaAvailable);

enum class FallSeverity : std::uint8_t { Safe, Injured, Lethal };

struct FallOutcome {
    FallSeverity severity = FallSeverity::Safe;
    float damageFraction = 0.0f; // 0 at safeFallMetres, rising linearly to 1 at lethalFallMetres
};

// A height that is not a number or is negative reads as zero. Invalid limits read as the defaults.
FallOutcome assessFall(const ClimbLimits& limits, float heightMetres);

}
