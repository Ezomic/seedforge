#pragma once

#include "ichor/audio/audio_host.h"
#include "ichor/audio/mixer.h"

#include <cstdint>
#include <vector>

namespace ichor {

// No device. Tests pull frames out of it and count what was played.
class NullAudio final : public AudioHost {
public:
    bool init() override { return true; }
    void addBundle(const SceneBundle& bundle) override { mixer_.addBundle(bundle); }
    std::uint32_t findSound(std::string_view name) const override { return mixer_.findClip(name); }
    void play(std::uint32_t sound, glm::dvec3 position, bool spatial, float gain = 1.0f,
        SoundCategory category = SoundCategory::None) override;
    void setAmbience(std::uint32_t sound, float gain) override { mixer_.setLoopGain(sound, gain); }
    void fadeOutMusic(float seconds) override { mixer_.fadeOutCategory(SoundCategory::Music, seconds); }
    void setListener(glm::dvec3 position, glm::quat rotation) override;
    void setReverb(const ReverbSettings& settings) override { mixer_.setReverb(settings); }

    // Renders `frames` stereo frames through the mixer.
    std::vector<float> render(std::uint32_t frames);

    std::uint64_t playsRequested() const { return plays_; }
    Mixer& mixer() { return mixer_; }

private:
    Mixer mixer_;
    std::uint64_t plays_ = 0;
};

}
