#include "ichor/assets/bundle.h"
#include "ichor/audio/ambience_layers.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/weather_ambience.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

double meanStep(const std::vector<float>& stereo)
{
    double total = 0.0;
    for (std::size_t i = 2; i < stereo.size(); i += 2) {
        total += std::abs(stereo[i] - stereo[i - 2]);
    }
    return total / static_cast<double>(stereo.size() / 2 - 1);
}

double energy(const std::vector<float>& stereo)
{
    double sum = 0.0;
    for (const float s : stereo) {
        sum += static_cast<double>(s) * s;
    }
    return sum;
}

}

TEST_CASE("clear skies are windless and a storm is loud")
{
    CHECK(windAmbienceGain(0.0f, 0.0f) == 0.0f);
    CHECK(windAmbienceGain(kWindDeadZone * 0.5f, 0.0f) == 0.0f);
    CHECK(windAmbienceGain(1.0f, 1.0f) == Approx(kWindMaxGain));
    CHECK(windAmbienceGain(1.0f, 0.0f) > windAmbienceGain(0.3f, 0.0f));
    CHECK(windAmbienceGain(0.5f, 1.0f) > windAmbienceGain(0.5f, 0.0f));
}

TEST_CASE("the wind gain never jumps as the weather sweeps")
{
    float previous = 0.0f;
    for (int i = 0; i <= 200; ++i) {
        const float t = static_cast<float>(i) / 200.0f;
        const float gain = windAmbienceGain(t, t);
        CHECK(gain >= previous);
        CHECK(gain - previous < 0.02f);
        previous = gain;
    }
}

TEST_CASE("bad weather values are clamped and NaN is silence")
{
    CHECK(windAmbienceGain(kNaN, 1.0f) == 0.0f);
    CHECK(windAmbienceGain(1.0f, kNaN) == 0.0f);
    CHECK(windAmbienceGain(-5.0f, -5.0f) == 0.0f);
    CHECK(windAmbienceGain(-kInf, -kInf) == 0.0f);
    CHECK(windAmbienceGain(9.0f, 9.0f) == Approx(kWindMaxGain));
    CHECK(windAmbienceGain(kInf, kInf) == Approx(kWindMaxGain));
    CHECK(windAmbienceGain(9.0f, 0.0f) == Approx(windAmbienceGain(1.0f, 0.0f)));
}

TEST_CASE("the weather targets carry both layers and fit the budget")
{
    const WeatherAudioTargets storm = weatherAudioTargets(1.0f, 1.0f);
    CHECK(storm.rain == Approx(rainAmbienceGain(1.0f)));
    CHECK(storm.wind == Approx(windAmbienceGain(1.0f, 1.0f)));
    CHECK(storm.rain + storm.wind <= kAmbienceBudget + 1e-6f);

    for (int o = -2; o <= 12; ++o) {
        for (int r = -2; r <= 12; ++r) {
            const WeatherAudioTargets t = weatherAudioTargets(static_cast<float>(o) / 10.0f, static_cast<float>(r) / 10.0f);
            CHECK(t.rain + t.wind <= kAmbienceBudget + 1e-6f);
        }
    }
    CHECK(weatherAudioTargets(kNaN, 1.0f).wind == 0.0f);
    CHECK(weatherAudioTargets(kNaN, 1.0f).rain == Approx(kRainMaxGain));
    CHECK(windAmbienceGain(0.0f, 1.0f) > 0.0f);

    const WeatherAudioTargets bad = weatherAudioTargets(kNaN, kNaN);
    CHECK(bad.rain == 0.0f);
    CHECK(bad.wind == 0.0f);
}

TEST_CASE("the wind clip is deterministic and loops without a jump at the seam")
{
    const SoundClip a = makeWindClip();
    const SoundClip b = makeWindClip();
    REQUIRE(a.samples.size() > 44100);
    CHECK(a.name == "A_Wind");
    CHECK(a.samples == b.samples);

    double total = 0.0;
    for (std::size_t i = 1; i < a.samples.size(); ++i) {
        total += std::abs(a.samples[i] - a.samples[i - 1]);
    }
    const double typical = total / static_cast<double>(a.samples.size() - 1);
    CHECK(typical > 0.0);
    CHECK(std::abs(a.samples.front() - a.samples.back()) <= 3.0 * typical);
}

TEST_CASE("the wind clip is audibly there and not clipped")
{
    const SoundClip clip = makeWindClip();
    int peak = 0;
    double sum = 0.0;
    for (const std::int16_t s : clip.samples) {
        peak = std::max(peak, std::abs(static_cast<int>(s)));
        sum += static_cast<double>(s) * s;
    }
    const double rms = std::sqrt(sum / static_cast<double>(clip.samples.size()));
    CHECK(peak <= 32767);
    CHECK(rms > 1000.0);
}

TEST_CASE("the wind layer is silent in clear weather and sounds in a storm")
{
    NullAudio audio;
    SceneBundle bundle;
    bundle.sounds.push_back(makeWindClip());
    audio.addBundle(bundle);
    const std::uint32_t wind = audio.findSound("A_Wind");
    REQUIRE(wind != Mixer::kNoSound);

    AmbienceLayers layers;
    REQUIRE(layers.assign(AmbienceLayer::Bed, wind));
    layers.setGain(AmbienceLayer::Bed, weatherAudioTargets(0.0f, 0.0f).wind);
    layers.apply(audio);
    CHECK(energy(audio.render(4800)) == 0.0);

    layers.setGain(AmbienceLayer::Bed, weatherAudioTargets(1.0f, 1.0f).wind);
    layers.apply(audio);
    audio.render(48000);
    CHECK(energy(audio.render(4800)) > 1.0);
}

TEST_CASE("weather flipping around a threshold changes the mix smoothly")
{
    NullAudio audio;
    SceneBundle bundle;
    bundle.sounds.push_back(makeWindClip());
    audio.addBundle(bundle);
    AmbienceLayers layers;
    layers.assign(AmbienceLayer::Bed, audio.findSound("A_Wind"));

    layers.setGain(AmbienceLayer::Bed, weatherAudioTargets(1.0f, 1.0f).wind);
    layers.apply(audio);
    audio.render(48000 * 5);
    const double steady = meanStep(audio.render(4800));

    std::vector<float> flipping;
    for (int i = 0; i < 200; ++i) {
        const float weather = i % 2 == 0 ? 0.0f : 1.0f;
        layers.setGain(AmbienceLayer::Bed, weatherAudioTargets(weather, weather).wind);
        layers.apply(audio);
        const std::vector<float> block = audio.render(480);
        flipping.insert(flipping.end(), block.begin(), block.end());
    }
    for (const float s : flipping) {
        REQUIRE(std::isfinite(s));
    }
    float worst = 0.0f;
    for (std::size_t i = 2; i < flipping.size(); ++i) {
        worst = std::max(worst, std::abs(flipping[i] - flipping[i - 2]));
    }
    CHECK(worst <= 6.0 * steady);
}

TEST_CASE("wind keeps playing as one loop through a long run")
{
    NullAudio audio;
    SceneBundle bundle;
    bundle.sounds.push_back(makeWindClip());
    audio.addBundle(bundle);
    AmbienceLayers layers;
    layers.assign(AmbienceLayer::Bed, audio.findSound("A_Wind"));
    layers.setGain(AmbienceLayer::Bed, 0.4f);
    for (int frame = 0; frame < 20000; ++frame) {
        layers.apply(audio);
        audio.render(480);
    }
    CHECK(audio.mixer().activeLoops() == 1);
    const std::vector<float> late = audio.render(9600);
    CHECK(energy(late) > 0.1);
}
