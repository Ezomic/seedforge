#include "ichor/audio/mixer.h"

#include "ichor/audio/ambience_layers.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kReferenceDistance = 2.0f; // metres at which a spatial sound has fallen to half gain
constexpr float kCentreGain = 1.41421356f;   // undoes the equal-power pan at centre so spatial and flat cues match

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

void Mixer::play(std::uint32_t clip, glm::dvec3 position, float gain, bool spatial)
{
    const std::lock_guard lock(mutex_);
    if (clip >= clips_.size() || clips_[clip].samples.empty()) {
        return;
    }
    if (voices_.size() >= kMaxVoices) {
        voices_.erase(voices_.begin()); // the oldest voice makes room
    }
    if (voices_.capacity() < kMaxVoices) {
        voices_.reserve(kMaxVoices);
    }
    voices_.push_back({clip, 0.0, position, gain, spatial});
}

void Mixer::setLoopGain(std::uint32_t clip, float gain)
{
    const std::lock_guard lock(mutex_);
    if (clip >= clips_.size() || clips_[clip].samples.empty()) {
        return;
    }
    const float target = std::isnan(gain) ? 0.0f : std::clamp(gain, 0.0f, 1.0f);
    for (Loop& loop : loops_) {
        if (loop.clip == clip) {
            loop.target = target;
            return;
        }
    }
    loops_.push_back({clip, 0.0, 0.0f, target});
}

void Mixer::setListener(glm::dvec3 position, glm::vec3 forward, glm::vec3 right)
{
    const std::lock_guard lock(mutex_);
    listenerPosition_ = position;
    listenerForward_ = forward;
    listenerRight_ = right;
}

void Mixer::setReverb(const ReverbSettings& settings)
{
    const std::lock_guard lock(mutex_);
    reverb_.setSettings(settings);
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
            const glm::vec3 relative(voice.position - listenerPosition_);
            const float distance = std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
            const float ratio = distance / kReferenceDistance;
            const float attenuation = 1.0f / (1.0f + ratio * ratio);
            const float pan = distance > 1e-4f
                ? std::clamp((relative.x * listenerRight_.x + relative.y * listenerRight_.y + relative.z * listenerRight_.z) / distance, -1.0f, 1.0f)
                : 0.0f;
            left = voice.gain * attenuation * kCentreGain * std::sqrt(0.5f * (1.0f - pan));
            right = voice.gain * attenuation * kCentreGain * std::sqrt(0.5f * (1.0f + pan));
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

    for (Loop& loop : loops_) {
        const Clip& clip = clips_[loop.clip];
        const double step = static_cast<double>(clip.sampleRate) / static_cast<double>(kRate);
        const double length = static_cast<double>(clip.samples.size());
        for (std::uint32_t f = 0; f < frames; ++f) {
            loop.gain = easeGain(loop.gain, loop.target);
            if (loop.gain > 0.0f) {
                const auto index = static_cast<std::size_t>(loop.cursor);
                const std::size_t next = index + 1 < clip.samples.size() ? index + 1 : 0;
                const float t = static_cast<float>(loop.cursor - static_cast<double>(index));
                const float a = static_cast<float>(clip.samples[index]) / 32768.0f;
                const float b = static_cast<float>(clip.samples[next]) / 32768.0f;
                const float sample = (a + (b - a) * t) * loop.gain;
                interleaved[f * 2] += sample;
                interleaved[f * 2 + 1] += sample;
            }
            loop.cursor += step;
            if (loop.cursor >= length) {
                loop.cursor -= length;
            }
        }
    }

    std::erase_if(voices_, [&](const Voice& voice) { return voice.cursor >= static_cast<double>(clips_[voice.clip].samples.size()); });
    reverb_.process(interleaved, frames);
    for (std::uint32_t i = 0; i < frames * kChannels; ++i) {
        interleaved[i] = std::clamp(interleaved[i], -1.0f, 1.0f);
    }
}

std::size_t Mixer::activeVoices() const
{
    const std::lock_guard lock(mutex_);
    return voices_.size();
}

std::size_t Mixer::activeLoops() const
{
    const std::lock_guard lock(mutex_);
    return loops_.size();
}

}
