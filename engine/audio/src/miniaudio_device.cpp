#include "ichor/audio/miniaudio_device.h"

#include "ichor/core/log.h"

#include <miniaudio.h>

namespace ichor {

struct MiniaudioDevice::Impl {
    ma_device device{};
    bool started = false;

    static void callback(ma_device* device, void* output, const void*, ma_uint32 frames)
    {
        auto* mixer = static_cast<Mixer*>(device->pUserData);
        mixer->mix(static_cast<float*>(output), frames);
    }
};

MiniaudioDevice::MiniaudioDevice()
    : impl_(std::make_unique<Impl>())
{
}

MiniaudioDevice::~MiniaudioDevice()
{
    if (impl_->started) {
        ma_device_uninit(&impl_->device);
    }
}

bool MiniaudioDevice::init()
{
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = Mixer::kChannels;
    config.sampleRate = Mixer::kRate;
    config.dataCallback = Impl::callback;
    config.pUserData = &mixer_;
    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        log(LogLevel::Error, "audio: no playback device");
        return false;
    }
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        log(LogLevel::Error, "audio: device would not start");
        return false;
    }
    impl_->started = true;
    logf(LogLevel::Info, "audio device: {}", impl_->device.playback.name);
    return true;
}

void MiniaudioDevice::play(std::uint32_t sound, glm::dvec3 position, bool spatial, float gain)
{
    mixer_.play(sound, position, gain, spatial);
}

void MiniaudioDevice::setListener(glm::dvec3 position, glm::quat rotation)
{
    mixer_.setListener(position, rotation * glm::vec3(0.0f, 0.0f, -1.0f), rotation * glm::vec3(1.0f, 0.0f, 0.0f));
}

bool MiniaudioDevice::running() const
{
    return impl_->started;
}

}
