#pragma once

#include "ichor/audio/audio_host.h"
#include "ichor/audio/mixer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ichor {

// The six layers of docs/12-audio-atmosphere.md, each one looping bed with its own gain.
enum class AmbienceLayer : std::uint8_t {
    Bed,
    TimeOfDay,
    Weather,
    Proximity,
    OneShots,
    Territory,
};

constexpr std::size_t kAmbienceLayerCount = 6;

// The most the layer gains may add up to. Every bed is at most full scale, so a sum of 1 cannot
// push the mix past +-1.
constexpr float kAmbienceBudget = 1.0f;

// One sample of the ease the mixer applies to a loop's gain (a time constant of about a quarter
// second at 48 kHz). It moves toward the target by a fixed fraction of the gap, so a jump from 0 to
// 1 is spread over many samples, and it snaps a fading gain to exactly 0 instead of letting it
// decay into denormals.
float easeGain(float gain, float target);

// What each layer's requested gain becomes. When the requests add up to more than kAmbienceBudget
// every one is scaled by the same factor so they add up to exactly the budget; below it they pass
// through untouched. Continuous in every gain, so crossing the budget never steps.
std::array<float, kAmbienceLayerCount> budgetedGains(const std::array<float, kAmbienceLayerCount>& requested);

// The layer table. Any thread may call any member; apply() pushes the budgeted gains into the
// host, which eases them in. Gains are clamped to 0..1 and NaN keeps the previous value. It holds
// no sim state, so it never feeds the world hash.
class AmbienceLayers {
public:
    // Gives a layer its bed. A clip another layer already uses, an unknown layer or kNoSound is
    // refused and the call returns false.
    bool assign(AmbienceLayer layer, std::uint32_t sound);
    void setGain(AmbienceLayer layer, float gain);
    float gain(AmbienceLayer layer) const;
    // The gain the host is asked for after the budget. Layers with no bed do not count against it.
    float mixedGain(AmbienceLayer layer) const;

    void apply(AudioHost& host) const;

private:
    std::array<float, kAmbienceLayerCount> budgetedLocked() const;

    mutable std::mutex mutex_;
    std::array<float, kAmbienceLayerCount> gains_{};
    std::array<std::uint32_t, kAmbienceLayerCount> sounds_{Mixer::kNoSound, Mixer::kNoSound, Mixer::kNoSound,
        Mixer::kNoSound, Mixer::kNoSound, Mixer::kNoSound};
};

}
