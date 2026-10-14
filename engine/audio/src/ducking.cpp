#include "ichor/audio/ducking.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kSnap = 1e-4f;

}

float duckTarget(const CategoryActivity& active)
{
    float target = 1.0f;
    for (std::size_t i = 1; i < kSoundCategoryCount; ++i) {
        if (active[i]) {
            target = std::min(target, 1.0f - categoryCeiling(static_cast<SoundCategory>(i)));
        }
    }
    return target;
}

float voiceGain(SoundCategory category, float gain)
{
    if (std::isnan(gain)) {
        return 0.0f;
    }
    if (category == SoundCategory::None) {
        return gain;
    }
    return std::clamp(gain, 0.0f, 1.0f) * categoryCeiling(category);
}

float nextDuckGain(float current, float target, float dt)
{
    target = std::isnan(target) ? 1.0f : std::clamp(target, 0.0f, 1.0f);
    current = std::isnan(current) ? 1.0f : std::clamp(current, 0.0f, 1.0f);
    if (!(dt > 0.0f)) {
        return current;
    }
    const float time = target < current ? kDuckAttackSeconds : kDuckReleaseSeconds;
    const float next = target + (current - target) * std::exp(-dt / time);
    if (std::abs(next - target) < kSnap) {
        return target;
    }
    return target < current ? std::max(next, target) : std::min(next, target);
}

}
