#include "ichor/audio/music_stinger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace ichor {

namespace {

constexpr std::uint32_t kRate = 44100;

struct Partial {
    double ratio;
    double weight;
};

double tone(double hz, double t)
{
    return std::sin(2.0 * std::numbers::pi * hz * t);
}

double sample(const MusicCue& cue, double t, double progress)
{
    const double root = cue.rootHz;
    switch (cue.event) {
    case MusicEvent::Vista:
        return tone(root, t) + 0.6 * tone(root * 1.5, t) + 0.4 * tone(root * 2.0, t) + 0.15 * tone(root * 3.0, t);
    case MusicEvent::Discovery: {
        constexpr std::array<double, 4> kSteps = {1.0, 1.25, 1.5, 2.0};
        const auto step = std::min<std::size_t>(static_cast<std::size_t>(progress * 4.0), 3);
        return tone(root * kSteps[step], t) + 0.3 * tone(root * kSteps[step] * 2.0, t) + 0.3 * tone(root * 0.5, t);
    }
    case MusicEvent::Combat: {
        const double pulse = 0.65 + 0.35 * std::sin(2.0 * std::numbers::pi * 3.0 * t);
        return pulse * (tone(root, t) + 0.7 * tone(root * 1.2, t) + 0.4 * tone(root * 0.5, t));
    }
    case MusicEvent::Death: {
        const double sink = root * (1.0 - 0.25 * progress);
        return tone(sink, t) + 0.6 * tone(sink * 1.2, t) + 0.8 * tone(root * 0.25, t);
    }
    }
    return 0.0;
}

}

std::string musicClipName(const MusicCue& cue)
{
    return "M_" + cue.id;
}

SoundClip makeMusicClip(const MusicCue& cue)
{
    SoundClip clip;
    clip.name = musicClipName(cue);
    clip.sampleRate = kRate;
    if (!validMusicCue(cue)) {
        return clip;
    }

    const auto frames = static_cast<std::size_t>(static_cast<double>(cue.duration) * kRate);
    std::vector<double> body(frames);
    double peak = 1e-9;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        body[i] = sample(cue, t, static_cast<double>(i) / static_cast<double>(frames)) * cueEnvelope(cue, t);
        peak = std::max(peak, std::abs(body[i]));
    }
    clip.samples.reserve(frames);
    for (const double value : body) {
        clip.samples.push_back(static_cast<std::int16_t>(std::lround(value / peak * 0.8 * 32767.0)));
    }
    return clip;
}

}
