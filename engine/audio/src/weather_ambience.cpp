#include "ichor/audio/weather_ambience.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace ichor {

namespace {

constexpr float kOvercastShare = 0.65f;

}

float windAmbienceGain(float overcast, float rain)
{
    if (std::isnan(overcast) || std::isnan(rain)) {
        return 0.0f;
    }
    const float sky = kOvercastShare * std::clamp(overcast, 0.0f, 1.0f) + (1.0f - kOvercastShare) * std::clamp(rain, 0.0f, 1.0f);
    const float t = std::clamp((sky - kWindDeadZone) / (1.0f - kWindDeadZone), 0.0f, 1.0f);
    return kWindMaxGain * t * t * (3.0f - 2.0f * t);
}

WeatherAudioTargets weatherAudioTargets(float overcast, float rain)
{
    return {windAmbienceGain(overcast, rain), rainAmbienceGain(rain)};
}

SoundClip makeWindClip()
{
    constexpr std::uint32_t kRate = 44100;
    constexpr std::size_t kFrames = kRate * 10;
    constexpr std::size_t kFade = kRate; // the tail is blended into the head over this many samples
    constexpr std::size_t kWarmUp = 8000;

    // Noise runs on past the end so the fade can blend the end of the loop into its start.
    std::uint32_t state = 0x5eed196u; // a fixed seed, so every build makes the same wind
    double first = 0.0;
    double second = 0.0;
    std::vector<double> raw(kWarmUp + kFrames + kFade);
    for (double& out : raw) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const double noise = static_cast<double>(state) / 2147483648.0 - 1.0;
        first += 0.03 * (noise - first);
        second += 0.05 * (first - second);
        out = second;
    }

    SoundClip clip;
    clip.name = "A_Wind";
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
        // Whole numbers of cycles over the clip keep the gusts periodic too.
        const double t = static_cast<double>(i) / static_cast<double>(kFrames);
        const double gust = 1.0 + 0.3 * std::sin(2.0 * std::numbers::pi * 2.0 * t) + 0.15 * std::sin(2.0 * std::numbers::pi * 5.0 * t + 0.7);
        body[i] = sample * gust;
        peak = std::max(peak, std::abs(body[i]));
    }
    for (const double sample : body) {
        clip.samples.push_back(static_cast<std::int16_t>(std::lround(sample / peak * 0.8 * 32767.0)));
    }
    return clip;
}

}
