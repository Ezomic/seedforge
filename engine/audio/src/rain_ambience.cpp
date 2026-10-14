#include "ichor/audio/rain_ambience.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace ichor {

float rainAmbienceGain(float rain)
{
    if (std::isnan(rain)) {
        return 0.0f;
    }
    const float t = std::clamp((std::clamp(rain, 0.0f, 1.0f) - kRainDeadZone) / (1.0f - kRainDeadZone), 0.0f, 1.0f);
    return kRainMaxGain * t * t * (3.0f - 2.0f * t);
}

SoundClip makeRainClip()
{
    constexpr std::uint32_t kRate = 44100;
    constexpr std::size_t kFrames = kRate * 8;
    constexpr std::size_t kFade = kRate; // the tail is blended into the head over this many samples
    constexpr std::size_t kWarmUp = 4000;

    // Noise runs on past the end so the fade can blend the end of the loop into its start.
    std::uint32_t state = 0x5eed386u; // a fixed seed, so every build makes the same rain
    double low = 0.0;
    double high = 0.0;
    std::vector<double> raw(kWarmUp + kFrames + kFade);
    for (double& out : raw) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const double noise = static_cast<double>(state) / 2147483648.0 - 1.0;
        low += 0.45 * (noise - low);
        high += 0.03 * (low - high);
        out = low - high;
    }

    SoundClip clip;
    clip.name = "A_Rain";
    clip.sampleRate = kRate;
    clip.samples.reserve(kFrames);

    std::vector<double> body(kFrames);
    double peak = 1e-9;
    for (std::size_t i = 0; i < kFrames; ++i) {
        double sample = raw[kWarmUp + i];
        if (i < kFade) {
            const double p = static_cast<double>(i) / static_cast<double>(kFade);
            // Equal power, since the two stretches of noise are unrelated. At i = 0 the head is the
            // noise that follows the last sample, which is what makes the seam continuous.
            sample = sample * std::sin(0.5 * std::numbers::pi * p) + raw[kWarmUp + kFrames + i] * std::cos(0.5 * std::numbers::pi * p);
        }
        // Whole numbers of cycles over the clip keep the swell periodic too.
        const double t = static_cast<double>(i) / static_cast<double>(kFrames);
        const double swell = 1.0 + 0.12 * std::sin(2.0 * std::numbers::pi * 3.0 * t) + 0.08 * std::sin(2.0 * std::numbers::pi * 7.0 * t + 1.3);
        body[i] = sample * swell;
        peak = std::max(peak, std::abs(body[i]));
    }
    for (const double sample : body) {
        clip.samples.push_back(static_cast<std::int16_t>(std::lround(sample / peak * 0.8 * 32767.0)));
    }
    return clip;
}

}
