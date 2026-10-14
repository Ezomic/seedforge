#pragma once

#include "ichor/audio/audio_host.h"
#include "ichor/audio/mixer.h"

#include <memory>

namespace ichor {

// The real output: a miniaudio playback device pulling from the mixer on its
// own thread. Everything the game does goes through the mixer's lock.
class MiniaudioDevice final : public AudioHost {
public:
    MiniaudioDevice();
    ~MiniaudioDevice() override;

    bool init() override;
    void addBundle(const SceneBundle& bundle) override { mixer_.addBundle(bundle); }
    std::uint32_t findSound(std::string_view name) const override { return mixer_.findClip(name); }
    void play(std::uint32_t sound, glm::dvec3 position, bool spatial, float gain = 1.0f,
        SoundCategory category = SoundCategory::None) override;
    void setAmbience(std::uint32_t sound, float gain) override { mixer_.setLoopGain(sound, gain); }
    void setListener(glm::dvec3 position, glm::quat rotation) override;
    void setReverb(const ReverbSettings& settings) override { mixer_.setReverb(settings); }

    bool running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Mixer mixer_;
};

}
