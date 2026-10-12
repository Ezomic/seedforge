#include "ichor/assets/audio_cook.h"

#include "ichor/core/log.h"

#include <miniaudio.h>

namespace ichor {

bool cookSound(const std::filesystem::path& source, SoundClip& out)
{
    const std::string file = source.string();
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 1, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(file.c_str(), &config, &decoder) != MA_SUCCESS) {
        logf(LogLevel::Error, "cannot decode {}", file);
        return false;
    }
    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS || frames == 0) {
        logf(LogLevel::Error, "{} has no frames", file);
        ma_decoder_uninit(&decoder);
        return false;
    }
    out.name = source.stem().string();
    out.sampleRate = decoder.outputSampleRate;
    out.samples.resize(static_cast<std::size_t>(frames));
    ma_uint64 read = 0;
    const ma_result result = ma_decoder_read_pcm_frames(&decoder, out.samples.data(), frames, &read);
    ma_decoder_uninit(&decoder);
    if ((result != MA_SUCCESS && result != MA_AT_END) || read == 0) {
        logf(LogLevel::Error, "{} could not be read", file);
        return false;
    }
    out.samples.resize(static_cast<std::size_t>(read));
    return true;
}

}
