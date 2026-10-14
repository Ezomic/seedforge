#include "ichor/audio/ambience_layers.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kEase = 1.0f / 12000.0f;
constexpr float kFloor = 1e-4f;

std::size_t indexOf(AmbienceLayer layer)
{
    return static_cast<std::size_t>(layer);
}

}

float easeGain(float gain, float target)
{
    gain += (target - gain) * kEase;
    if (gain < kFloor && target == 0.0f) {
        return 0.0f;
    }
    return gain;
}

std::array<float, kAmbienceLayerCount> budgetedGains(const std::array<float, kAmbienceLayerCount>& requested)
{
    std::array<float, kAmbienceLayerCount> out{};
    float sum = 0.0f;
    for (std::size_t i = 0; i < kAmbienceLayerCount; ++i) {
        out[i] = std::isnan(requested[i]) ? 0.0f : std::clamp(requested[i], 0.0f, 1.0f);
        sum += out[i];
    }
    const float scale = sum > kAmbienceBudget ? kAmbienceBudget / sum : 1.0f;
    for (float& gain : out) {
        gain *= scale;
    }
    return out;
}

bool AmbienceLayers::assign(AmbienceLayer layer, std::uint32_t sound)
{
    const std::size_t index = indexOf(layer);
    if (index >= kAmbienceLayerCount || sound == Mixer::kNoSound) {
        return false;
    }
    const std::lock_guard lock(mutex_);
    for (std::size_t other = 0; other < kAmbienceLayerCount; ++other) {
        if (other != index && sounds_[other] == sound) {
            return false;
        }
    }
    sounds_[index] = sound;
    return true;
}

void AmbienceLayers::setGain(AmbienceLayer layer, float gain)
{
    const std::size_t index = indexOf(layer);
    if (index >= kAmbienceLayerCount || std::isnan(gain)) {
        return;
    }
    const std::lock_guard lock(mutex_);
    gains_[index] = std::clamp(gain, 0.0f, 1.0f);
}

float AmbienceLayers::gain(AmbienceLayer layer) const
{
    const std::size_t index = indexOf(layer);
    if (index >= kAmbienceLayerCount) {
        return 0.0f;
    }
    const std::lock_guard lock(mutex_);
    return gains_[index];
}

std::array<float, kAmbienceLayerCount> AmbienceLayers::budgetedLocked() const
{
    std::array<float, kAmbienceLayerCount> requested{};
    for (std::size_t i = 0; i < kAmbienceLayerCount; ++i) {
        requested[i] = sounds_[i] == Mixer::kNoSound ? 0.0f : gains_[i];
    }
    return budgetedGains(requested);
}

float AmbienceLayers::mixedGain(AmbienceLayer layer) const
{
    const std::size_t index = indexOf(layer);
    if (index >= kAmbienceLayerCount) {
        return 0.0f;
    }
    const std::lock_guard lock(mutex_);
    return budgetedLocked()[index];
}

void AmbienceLayers::apply(AudioHost& host) const
{
    std::array<float, kAmbienceLayerCount> mixed;
    std::array<std::uint32_t, kAmbienceLayerCount> sounds;
    {
        const std::lock_guard lock(mutex_);
        mixed = budgetedLocked();
        sounds = sounds_;
    }
    for (std::size_t i = 0; i < kAmbienceLayerCount; ++i) {
        if (sounds[i] != Mixer::kNoSound) {
            host.setAmbience(sounds[i], mixed[i]);
        }
    }
}

}
