#include "ichor/audio/null_audio.h"

namespace ichor {

void NullAudio::play(std::uint32_t sound, glm::dvec3 position, bool spatial, float gain)
{
    ++plays_;
    mixer_.play(sound, position, gain, spatial);
}

void NullAudio::setListener(glm::dvec3 position, glm::quat rotation)
{
    mixer_.setListener(position, rotation * glm::vec3(0.0f, 0.0f, -1.0f), rotation * glm::vec3(1.0f, 0.0f, 0.0f));
}

std::vector<float> NullAudio::render(std::uint32_t frames)
{
    std::vector<float> out(static_cast<std::size_t>(frames) * Mixer::kChannels);
    mixer_.mix(out.data(), frames);
    return out;
}

}
