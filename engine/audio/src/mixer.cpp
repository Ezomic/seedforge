#include "ichor/audio/mixer.h"

#include "ichor/audio/ambience_layers.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kReferenceDistance = 2.0f; // metres at which a spatial sound has fallen to half gain
constexpr float kCentreGain = 1.41421356f;   // undoes the equal-power pan at centre so spatial and flat cues match
constexpr float kMasterStep = 10.0f / static_cast<float>(Mixer::kRate); // the master gain crosses its whole range in a tenth of a second

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

void Mixer::play(std::uint32_t clip, glm::dvec3 position, float gain, bool spatial, SoundCategory category)
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
    voices_.push_back({clip, 0.0, position, voiceGain(category, gain),
        spatial, category});
}

void Mixer::fadeOutCategory(SoundCategory category, float seconds)
{
    const std::lock_guard lock(mutex_);
    const float frames = std::isnan(seconds) ? 0.0f : std::max(seconds, 0.0f) * static_cast<float>(kRate);
    for (Voice& voice : voices_) {
        if (voice.category == category) {
            voice.fadeStep = frames < 1.0f ? voice.fade : voice.fade / frames;
        }
    }
}

void Mixer::setMasterGain(float gain)
{
    const std::lock_guard lock(mutex_);
    masterTarget_ = std::isnan(gain) ? 0.0f : std::clamp(gain, 0.0f, 1.0f);
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
    if (frames == 0) {
        return;
    }

    CategoryActivity active{};
    for (const Voice& voice : voices_) {
        active[static_cast<std::size_t>(voice.category)] = true;
    }
    const float duckFrom = duck_;
    duck_ = nextDuckGain(duckFrom, duckTarget(active), static_cast<float>(frames) / static_cast<float>(kRate));
    const float duckStep = (duck_ - duckFrom) / static_cast<float>(frames);

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
            const float sample = (a + (b - a) * t);
            interleaved[f * 2] += sample * left * voice.fade;
            interleaved[f * 2 + 1] += sample * right * voice.fade;
            voice.cursor += step;
            if (voice.fadeStep > 0.0f) {
                voice.fade = std::max(0.0f, voice.fade - voice.fadeStep);
                if (voice.fade <= 0.0f) {
                    voice.cursor = static_cast<double>(clip.samples.size());
                    break;
                }
            }
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
                const float sample = (a + (b - a) * t) * loop.gain * (duckFrom + duckStep * static_cast<float>(f + 1));
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
    // Skipped at full gain so the mix stays bit for bit what it was before the master gain existed.
    if (master_ != 1.0f || masterTarget_ != 1.0f) {
        for (std::uint32_t f = 0; f < frames; ++f) {
            master_ = master_ < masterTarget_ ? std::min(masterTarget_, master_ + kMasterStep) : std::max(masterTarget_, master_ - kMasterStep);
            interleaved[f * 2] *= master_;
            interleaved[f * 2 + 1] *= master_;
        }
    }
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
