#include "ichor/audio/wading_splash.h"

#include "ichor/core/wading.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace ichor {

std::optional<SplashCue> WadingSplash::step(double x, double z, float depth)
{
    if (!std::isfinite(x) || !std::isfinite(z)) {
        placed_ = false;
        sinceSplash_ = 0.0;
        return std::nullopt;
    }
    const double moved = placed_ ? std::hypot(x - x_, z - z_) : 0.0;
    placed_ = true;
    x_ = x;
    z_ = z;

    if (!(depth > kWadeStart) || !std::isfinite(depth) || moved > kMaxStepMetres) {
        sinceSplash_ = 0.0;
        return std::nullopt;
    }
    sinceSplash_ += moved;
    if (sinceSplash_ < kStrideMetres) {
        return std::nullopt;
    }
    sinceSplash_ = std::fmod(sinceSplash_, kStrideMetres);
    const float deepness = std::min((depth - kWadeStart) / (kWadeFull - kWadeStart), 1.0f);
    return SplashCue{kQuietGain + (1.0f - kQuietGain) * deepness};
}

SoundClip makeSplashClip()
{
    constexpr std::uint32_t kRate = 44100;
    constexpr double kSeconds = 0.4;
    const auto frames = static_cast<std::size_t>(kRate * kSeconds);

    SoundClip clip;
    clip.name = "A_Splash";
    clip.sampleRate = kRate;
    clip.samples.reserve(frames);

    std::uint32_t state = 0x5eed378u; // a fixed seed, so every build makes the same splash
    double low = 0.0;
    double high = 0.0;
    std::vector<double> raw(frames);
    double peak = 1e-9;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double p = t / kSeconds;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const double noise = static_cast<double>(state) / 2147483648.0 - 1.0;
        // Two one-pole filters make a band that darkens as the splash dies away.
        const double cutoff = 3200.0 - 2400.0 * p;
        const double a = 1.0 - std::exp(-2.0 * std::numbers::pi * cutoff / kRate);
        low += a * (noise - low);
        high += 0.02 * (low - high);
        const double band = low - high;
        const double bubble = std::sin(2.0 * std::numbers::pi * (520.0 - 260.0 * p) * t) * 0.35 * std::exp(-14.0 * p);
        const double attack = std::min(t / 0.003, 1.0);
        raw[i] = (band * std::exp(-7.0 * p) + bubble) * attack;
        peak = std::max(peak, std::abs(raw[i]));
    }
    for (const double sample : raw) {
        clip.samples.push_back(static_cast<std::int16_t>(std::lround(sample / peak * 0.7 * 32767.0)));
    }
    return clip;
}

}
