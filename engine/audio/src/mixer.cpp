#include "ichor/audio/mixer.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kReferenceDistance = 2.0f; // metres at which a spatial sound is at full gain

}

void Mixer::addBundle(const SceneBundle& bundle)
{
    const std::lock_guard lock(mutex_);
    for (const SoundClip& sound : bundle.sounds) {
        clips_.push_back({sound.name, sound.sampleRate, sound.samples});
    }
}

std::uint32_t Mixer::clipCount() const
{
    const std::lock_guard lock(mutex_);
    return static_cast<std::uint32_t>(clips_.size());
}

std::uint32_t Mixer::findClip(std::string_view name) const
{
    const std::lock_guard lock(mutex_);
    for (std::uint32_t i = 0; i < clips_.size(); ++i) {
        if (clips_[i].name == name) {
            return i;
        }
    }
    return kNoSound;
}

void Mixer::play(std::uint32_t clip, glm::vec3 position, float gain, bool spatial)
{
    const std::lock_guard lock(mutex_);
    if (clip >= clips_.size() || clips_[clip].samples.empty()) {
        return;
    }
    voices_.push_back({clip, 0.0, position, gain, spatial});
}

void Mixer::setListener(glm::vec3 position, glm::vec3 forward, glm::vec3 right)
{
    const std::lock_guard lock(mutex_);
    listenerPosition_ = position;
    listenerForward_ = forward;
    listenerRight_ = right;
}

void Mixer::mix(float* interleaved, std::uint32_t frames)
{
    const std::lock_guard lock(mutex_);
    std::fill(interleaved, interleaved + static_cast<std::size_t>(frames) * kChannels, 0.0f);

    for (Voice& voice : voices_) {
        const Clip& clip = clips_[voice.clip];
        float left = voice.gain;
        float right = voice.gain;
        if (voice.spatial) {
            const glm::vec3 relative = voice.position - listenerPosition_;
            const float distance = std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
            const float ratio = distance / kReferenceDistance;
            const float attenuation = 1.0f / (1.0f + ratio * ratio);
            const float pan = distance > 1e-4f
                ? std::clamp((relative.x * listenerRight_.x + relative.y * listenerRight_.y + relative.z * listenerRight_.z) / distance, -1.0f, 1.0f)
                : 0.0f;
            left = voice.gain * attenuation * std::sqrt(0.5f * (1.0f - pan));
            right = voice.gain * attenuation * std::sqrt(0.5f * (1.0f + pan));
        }

        const double step = static_cast<double>(clip.sampleRate) / static_cast<double>(kRate);
        const std::size_t last = clip.samples.size() - 1;
        for (std::uint32_t f = 0; f < frames; ++f) {
            const auto index = static_cast<std::size_t>(voice.cursor);
            if (index >= last) {
                voice.cursor = static_cast<double>(clip.samples.size());
                break;
            }
            const float t = static_cast<float>(voice.cursor - static_cast<double>(index));
            const float a = static_cast<float>(clip.samples[index]) / 32768.0f;
            const float b = static_cast<float>(clip.samples[index + 1]) / 32768.0f;
            const float sample = a + (b - a) * t;
            interleaved[f * 2] += sample * left;
            interleaved[f * 2 + 1] += sample * right;
            voice.cursor += step;
        }
    }

    std::erase_if(voices_, [&](const Voice& voice) { return voice.cursor >= static_cast<double>(clips_[voice.clip].samples.size()); });
    for (std::uint32_t i = 0; i < frames * kChannels; ++i) {
        interleaved[i] = std::clamp(interleaved[i], -1.0f, 1.0f);
    }
}

std::size_t Mixer::activeVoices() const
{
    const std::lock_guard lock(mutex_);
    return voices_.size();
}

}
