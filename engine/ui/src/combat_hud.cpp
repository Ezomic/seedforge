#include "ichor/ui/combat_hud.h"

#include "ichor/core/dmath.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

float fractionOf(float value, float max)
{
    if (!std::isfinite(value) || !std::isfinite(max) || max <= 0.0f || value <= 0.0f) {
        return 0.0f;
    }
    return std::min(value / max, 1.0f);
}

CombatHudBar barOf(float value, float max, float lowThreshold)
{
    const float fraction = fractionOf(value, max);
    return {fraction, fraction <= lowThreshold};
}

float relativeAngle(float bearing, float facing)
{
    const float delta = bearing - facing;
    const float turns = std::floor(delta * dmath::kInvTwoPi + 0.5f);
    float angle = delta - turns * dmath::kTwoPi;
    if (angle <= -dmath::kPi) {
        angle += dmath::kTwoPi;
    }
    return angle;
}

}

CombatHudView buildCombatHudView(const CombatHudInput& input, const CombatHudTuning& tuning)
{
    CombatHudView view;
    view.health = barOf(input.health, input.maxHealth, tuning.lowThreshold);
    view.stamina = barOf(input.stamina, input.maxStamina, tuning.lowThreshold);

    if (input.lawResource) {
        const float value = std::isfinite(input.lawResource->value) ? std::max(input.lawResource->value, 0.0f) : 0.0f;
        view.lawResource = CombatHudLawView{input.lawResource->id, fractionOf(value, input.lawResource->max), value};
    }

    if (input.target) {
        view.targetStagger = fractionOf(input.target->stagger, input.target->staggerMax);
    }

    for (const CombatHudStatus& status : input.statuses) {
        if (status.id.empty()) {
            continue;
        }
        view.statuses.push_back({status.id, std::max(status.stacks, 1), fractionOf(status.remaining, 1.0f)});
    }

    if (std::isfinite(input.facingYaw) && std::isfinite(tuning.indicatorSeconds) && tuning.indicatorSeconds > 0.0f) {
        std::vector<CombatHudHit> hits;
        for (const CombatHudHit& hit : input.recentHits) {
            if (std::isfinite(hit.bearing) && std::isfinite(hit.ageSeconds) && hit.ageSeconds >= 0.0f
                && hit.ageSeconds < tuning.indicatorSeconds) {
                hits.push_back(hit);
            }
        }
        std::stable_sort(hits.begin(), hits.end(),
            [](const CombatHudHit& a, const CombatHudHit& b) { return a.ageSeconds < b.ageSeconds; });
        if (hits.size() > tuning.maxIndicators) {
            hits.resize(tuning.maxIndicators);
        }
        for (const CombatHudHit& hit : hits) {
            view.indicators.push_back(
                {relativeAngle(hit.bearing, input.facingYaw), 1.0f - hit.ageSeconds / tuning.indicatorSeconds});
        }
    }
    return view;
}

}
