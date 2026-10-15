#include "ichor/traversal/climb_rules.h"

#include <limits>

namespace ichor {

namespace {

constexpr float kMaxFloat = std::numeric_limits<float>::max();

bool finiteNonNegative(float value)
{
    return value >= 0.0f && value <= kMaxFloat;
}

float clampedProduct(float a, float b)
{
    if (a == 0.0f || b == 0.0f) {
        return 0.0f;
    }
    return a > kMaxFloat / b ? kMaxFloat : a * b;
}

}

bool ClimbLimits::valid() const
{
    return finiteNonNegative(minSlopeDegrees) && finiteNonNegative(maxSlopeDegrees)
        && minSlopeDegrees < maxSlopeDegrees && maxSlopeDegrees <= 90.0f && finiteNonNegative(maxClimbMetres)
        && finiteNonNegative(maxDescentMetres) && finiteNonNegative(climbStaminaPerMetre)
        && finiteNonNegative(descentStaminaPerMetre) && finiteNonNegative(safeFallMetres)
        && finiteNonNegative(lethalFallMetres) && safeFallMetres < lethalFallMetres;
}

ClimbAssessment assessClimb(const ClimbLimits& limits, const CapabilitySet& capabilities, ClimbDirection direction,
    const ClimbSurface& surface, float staminaAvailable)
{
    if (!limits.valid() || !finiteNonNegative(surface.slopeDegrees) || !finiteNonNegative(surface.heightMetres)
        || !finiteNonNegative(staminaAvailable)) {
        return {ClimbBlock::InvalidInput, 0.0f};
    }
    const bool up = direction == ClimbDirection::Up;
    if (!capabilities.has(up ? Capability::Climb : Capability::Descend)) {
        return {ClimbBlock::NoCapability, 0.0f};
    }
    if (!surface.designated) {
        return {ClimbBlock::NotDesignated, 0.0f};
    }
    if (surface.slopeDegrees < limits.minSlopeDegrees) {
        return {ClimbBlock::TooGentle, 0.0f};
    }
    if (surface.slopeDegrees > limits.maxSlopeDegrees) {
        return {ClimbBlock::TooSteep, 0.0f};
    }
    if (surface.heightMetres > (up ? limits.maxClimbMetres : limits.maxDescentMetres)) {
        return {ClimbBlock::TooHigh, 0.0f};
    }

    const float slopeShare = (surface.slopeDegrees - limits.minSlopeDegrees)
        / (limits.maxSlopeDegrees - limits.minSlopeDegrees);
    const float perMetre = up ? limits.climbStaminaPerMetre : limits.descentStaminaPerMetre;
    const float cost = clampedProduct(clampedProduct(surface.heightMetres, perMetre), 1.0f + slopeShare);
    if (cost > staminaAvailable) {
        return {ClimbBlock::NoStamina, cost};
    }
    return {ClimbBlock::None, cost};
}

FallOutcome assessFall(const ClimbLimits& limits, float heightMetres)
{
    const ClimbLimits use = limits.valid() ? limits : ClimbLimits{};
    const float height = heightMetres > 0.0f ? heightMetres : 0.0f;
    if (height <= use.safeFallMetres) {
        return {FallSeverity::Safe, 0.0f};
    }
    if (height >= use.lethalFallMetres) {
        return {FallSeverity::Lethal, 1.0f};
    }
    return {FallSeverity::Injured, (height - use.safeFallMetres) / (use.lethalFallMetres - use.safeFallMetres)};
}

}
