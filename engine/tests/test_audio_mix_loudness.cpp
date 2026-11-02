#include "ichor/assets/bundle.h"
#include "ichor/audio/ambience_layers.h"
#include "ichor/audio/ducking.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/rain_ambience.h"
#include "ichor/audio/weather_ambience.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr std::uint32_t kRate = Mixer::kRate;
constexpr std::uint32_t kBlock = 480;
constexpr std::uint32_t kSettleFrames = kRate * 2;
constexpr std::uint32_t kCueFrames = kRate * 4;
constexpr std::uint32_t kMeasureFrom = kSettleFrames + kRate;
constexpr std::uint32_t kMeasureFrames = kRate;

SoundClip noiseClip(const std::string& name, std::uint32_t frames, std::uint32_t seed, std::int16_t level)
{
    SoundClip clip;
    clip.name = name;
    clip.sampleRate = kRate;
    clip.samples.resize(frames);
    std::uint32_t state = seed;
    for (auto& sample : clip.samples) {
        state = state * 1664525u + 1013904223u;
        sample = static_cast<std::int16_t>((static_cast<std::int32_t>(state >> 16) % (2 * level + 1)) - level);
    }
    return clip;
}

struct Bed {
    AmbienceLayer layer;
    float gain;
    SoundClip clip;
};

struct Cue {
    SoundCategory category;
    float gain;
};

struct Scenario {
    const char* name;
    std::vector<Bed> beds;
    std::vector<Cue> cues;
};

float meanAbs(const std::vector<float>& stereo, std::uint32_t fromFrame, std::uint32_t frames)
{
    double sum = 0.0;
    for (std::uint32_t i = fromFrame * 2; i < (fromFrame + frames) * 2; ++i) {
        sum += std::abs(stereo[i]);
    }
    return static_cast<float>(sum / (frames * 2.0));
}

float peak(const std::vector<float>& stereo, std::uint32_t fromFrame, std::uint32_t frames)
{
    float worst = 0.0f;
    for (std::uint32_t i = fromFrame * 2; i < (fromFrame + frames) * 2; ++i) {
        worst = std::max(worst, std::abs(stereo[i]));
    }
    return worst;
}

// The beds play for kSettleFrames alone, then the cues start. Silent cues leave only the ducked
// bed in the mix; audible ones are full-scale noise, the worst case for the peak.
std::vector<float> render(const Scenario& scenario, bool withCues, bool audibleCues)
{
    NullAudio audio;
    SceneBundle bundle;
    for (const Bed& bed : scenario.beds) {
        bundle.sounds.push_back(bed.clip);
    }
    bundle.sounds.push_back(noiseClip("S_Cue", kRate * 5, 7, audibleCues ? 32767 : 0));
    audio.addBundle(bundle);

    AmbienceLayers layers;
    for (const Bed& bed : scenario.beds) {
        REQUIRE(layers.assign(bed.layer, audio.findSound(bed.clip.name)));
        layers.setGain(bed.layer, bed.gain);
    }
    layers.apply(audio);

    std::vector<float> out;
    for (std::uint32_t done = 0; done < kSettleFrames + kCueFrames; done += kBlock) {
        if (done == kSettleFrames && withCues) {
            const std::uint32_t cue = audio.findSound("S_Cue");
            for (const Cue& c : scenario.cues) {
                audio.play(cue, glm::dvec3(0.0), false, c.gain, c.category);
            }
        }
        const std::vector<float> part = audio.render(kBlock);
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

float ruleDepth(const Scenario& scenario)
{
    float ceiling = 0.0f;
    for (const Cue& cue : scenario.cues) {
        ceiling = std::max(ceiling, categoryCeiling(cue.category));
    }
    return 1.0f - ceiling;
}

std::vector<Scenario> scenarios()
{
    const WeatherAudioTargets storm = weatherAudioTargets(1.0f, 1.0f);
    const WeatherAudioTargets ridge = weatherAudioTargets(1.0f, 0.0f);
    const WeatherAudioTargets breeze = weatherAudioTargets(0.3f, 0.0f);
    return {
        {"combat in rain near a settlement",
            {{AmbienceLayer::Bed, storm.wind, makeWindClip()}, {AmbienceLayer::Weather, storm.rain, makeRainClip()},
                {AmbienceLayer::Proximity, 0.6f, noiseClip("A_Chatter", kRate, 11, 20000)}},
            {{SoundCategory::OneShot, 1.0f}, {SoundCategory::OneShot, 0.8f}}},
        {"a quiet field",
            {{AmbienceLayer::Bed, breeze.wind, makeWindClip()}, {AmbienceLayer::TimeOfDay, 0.15f, noiseClip("A_Crickets", kRate, 13, 6000)}},
            {}},
        {"a dungeon room",
            {{AmbienceLayer::Bed, 0.5f, noiseClip("A_Drone", kRate, 17, 32767)}, {AmbienceLayer::Proximity, 0.3f, noiseClip("A_Drip", kRate, 19, 12000)}},
            {{SoundCategory::OneShot, 1.0f}}},
        {"a market",
            {{AmbienceLayer::Bed, 0.4f, makeWindClip()}, {AmbienceLayer::Proximity, 0.9f, noiseClip("A_Crowd", kRate, 23, 32767)},
                {AmbienceLayer::TimeOfDay, 0.3f, noiseClip("A_Birds", kRate, 29, 16000)}},
            {{SoundCategory::Voice, 1.0f}, {SoundCategory::OneShot, 1.0f}}},
        {"a boss fight",
            {{AmbienceLayer::Bed, storm.wind, makeWindClip()}, {AmbienceLayer::Weather, storm.rain, makeRainClip()},
                {AmbienceLayer::Territory, 0.8f, noiseClip("A_Warcry", kRate, 31, 32767)}},
            {{SoundCategory::Music, 1.0f}, {SoundCategory::Voice, 1.0f}, {SoundCategory::OneShot, 1.0f}}},
        {"wind on a ridge", {{AmbienceLayer::Bed, ridge.wind, makeWindClip()}}, {}},
    };
}

Scenario withLoudestCue(Scenario scenario)
{
    const auto loudest = std::max_element(scenario.cues.begin(), scenario.cues.end(), [](const Cue& a, const Cue& b) {
        return categoryCeiling(a.category) < categoryCeiling(b.category);
    });
    if (loudest != scenario.cues.end()) {
        scenario.cues = {*loudest};
    }
    return scenario;
}

}

// The ceilings guarantee a settled bed plus one capped voice stays within full scale (ducking.h); further
// voices lean on the final clamp, so the peak is measured with the loudest cue alone.
TEST_CASE("six mixed scenes stay under full scale once the duck has settled", "[audio][mix]")
{
    for (const Scenario& full : scenarios()) {
        const Scenario scenario = withLoudestCue(full);
        const std::vector<float> mix = render(scenario, true, true);
        const float settled = peak(mix, kMeasureFrom, kMeasureFrames);
        const float whole = peak(mix, 0, static_cast<std::uint32_t>(mix.size() / 2));
        std::printf("[mix] %-36s settled peak %.3f  whole-run peak %.3f\n", scenario.name, settled, whole);
        INFO(scenario.name);
        CHECK(settled < 0.999f);
    }
}

TEST_CASE("the mix of each scene ducks to the depth the rule gives", "[audio][mix]")
{
    for (const Scenario& scenario : scenarios()) {
        if (scenario.cues.empty()) {
            continue;
        }
        const std::vector<float> ducked = render(scenario, true, false);
        const std::vector<float> open = render(scenario, false, false);
        const float depth = meanAbs(ducked, kMeasureFrom, kMeasureFrames) / meanAbs(open, kMeasureFrom, kMeasureFrames);
        const float rule = ruleDepth(scenario);
        std::printf("[mix] %-36s duck depth %.3f  rule %.3f\n", scenario.name, depth, rule);
        INFO(scenario.name);
        CHECK(depth == Approx(rule).margin(0.03));
        CHECK(depth < 0.75f);
    }
}

TEST_CASE("the mix of the quiet scenes stays quiet next to a busy one", "[audio][mix]")
{
    const std::vector<Scenario> all = scenarios();
    const float market = meanAbs(render(all[3], false, false), kMeasureFrom, kMeasureFrames);
    for (const std::size_t index : {std::size_t{1}, std::size_t{5}}) {
        const std::vector<float> mix = render(all[index], false, false);
        const float mean = meanAbs(mix, kMeasureFrom, kMeasureFrames);
        std::printf("[mix] %-36s mean %.4f  (market %.4f)  peak %.3f\n", all[index].name, mean, market, peak(mix, kMeasureFrom, kMeasureFrames));
        INFO(all[index].name);
        CHECK(mean < market * 0.5f);
        CHECK(peak(mix, kMeasureFrom, kMeasureFrames) < 0.6f);
    }
}
