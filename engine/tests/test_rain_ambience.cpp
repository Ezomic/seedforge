#include "ichor/assets/bundle.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/rain_ambience.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

std::uint32_t addRain(NullAudio& audio)
{
    SceneBundle bundle;
    bundle.sounds.push_back(makeRainClip());
    audio.addBundle(bundle);
    const std::uint32_t rain = audio.findSound("A_Rain");
    REQUIRE(rain != Mixer::kNoSound);
    return rain;
}

double energy(const std::vector<float>& stereo)
{
    double sum = 0.0;
    for (const float s : stereo) {
        sum += static_cast<double>(s) * s;
    }
    return sum;
}

float largestStep(const std::vector<float>& stereo)
{
    float worst = 0.0f;
    for (std::size_t i = 2; i < stereo.size(); ++i) {
        worst = std::max(worst, std::abs(stereo[i] - stereo[i - 2]));
    }
    return worst;
}

}

TEST_CASE("rain maps to a gain that is silent when dry and rises smoothly")
{
    CHECK(rainAmbienceGain(0.0f) == 0.0f);
    CHECK(rainAmbienceGain(kRainDeadZone) == 0.0f);
    CHECK(rainAmbienceGain(kRainDeadZone * 0.5f) == 0.0f);
    CHECK(rainAmbienceGain(1.0f) == Approx(kRainMaxGain));
    float previous = 0.0f;
    for (int i = 0; i <= 100; ++i) {
        const float gain = rainAmbienceGain(static_cast<float>(i) / 100.0f);
        CHECK(gain >= previous);
        CHECK(gain - previous < 0.05f);
        previous = gain;
    }
}

TEST_CASE("bad rain values are clamped, and NaN is silence")
{
    CHECK(rainAmbienceGain(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(rainAmbienceGain(-3.0f) == 0.0f);
    CHECK(rainAmbienceGain(-std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK(rainAmbienceGain(7.0f) == Approx(kRainMaxGain));
    CHECK(rainAmbienceGain(std::numeric_limits<float>::infinity()) == Approx(kRainMaxGain));
}

TEST_CASE("the rain clip is deterministic and loops without a jump at the seam")
{
    const SoundClip a = makeRainClip();
    const SoundClip b = makeRainClip();
    REQUIRE(a.samples.size() > 44100);
    CHECK(a.samples == b.samples);

    int worstInside = 0;
    for (std::size_t i = 1; i < a.samples.size(); ++i) {
        worstInside = std::max(worstInside, std::abs(a.samples[i] - a.samples[i - 1]));
    }
    double total = 0.0;
    for (std::size_t i = 1; i < a.samples.size(); ++i) {
        total += std::abs(a.samples[i] - a.samples[i - 1]);
    }
    const double typical = total / static_cast<double>(a.samples.size() - 1);
    const int seam = std::abs(a.samples.front() - a.samples.back());
    CHECK(seam <= 3.0 * typical);
    CHECK(worstInside > 0);
}

TEST_CASE("dry rain makes no sound and a downpour does")
{
    NullAudio audio;
    const std::uint32_t rain = addRain(audio);
    audio.setAmbience(rain, 0.0f);
    CHECK(energy(audio.render(4800)) == 0.0);
    audio.setAmbience(rain, rainAmbienceGain(1.0f));
    audio.render(48000);
    CHECK(energy(audio.render(4800)) > 1.0);
}

TEST_CASE("setting the ambience every frame keeps one looping voice")
{
    NullAudio audio;
    const std::uint32_t rain = addRain(audio);
    for (int frame = 0; frame < 5000; ++frame) {
        audio.setAmbience(rain, 0.5f);
        audio.render(64);
    }
    CHECK(audio.mixer().activeLoops() == 1);
    CHECK(audio.mixer().activeVoices() == 0);
}

TEST_CASE("an unknown sound is ignored")
{
    NullAudio audio;
    const std::uint32_t rain = addRain(audio);
    audio.setAmbience(Mixer::kNoSound, 1.0f);
    audio.setAmbience(rain + 40, 1.0f);
    CHECK(audio.mixer().activeLoops() == 0);
}

TEST_CASE("the loop keeps playing past the end of the clip")
{
    NullAudio audio;
    const std::uint32_t rain = addRain(audio);
    audio.setAmbience(rain, 1.0f);
    audio.render(48000);
    const std::uint32_t clipFrames = static_cast<std::uint32_t>(makeRainClip().samples.size() * 48000ull / 44100ull);
    const std::vector<float> across = audio.render(clipFrames * 2);
    CHECK(energy(across) > 1.0);
    CHECK(audio.mixer().activeLoops() == 1);
    const std::vector<float> late(across.end() - 9600, across.end());
    CHECK(energy(late) > 0.1);
}

TEST_CASE("the loop seam is not audible in the mixed output")
{
    NullAudio audio;
    const std::uint32_t rain = addRain(audio);
    audio.setAmbience(rain, 1.0f);
    audio.render(48000 * 10);
    const std::uint32_t clipFrames = static_cast<std::uint32_t>(makeRainClip().samples.size() * 48000ull / 44100ull);
    const std::vector<float> run = audio.render(clipFrames * 3);

    float nearSeam = 0.0f;
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t frame = 1; frame < run.size() / 2; ++frame) {
        const float step = std::abs(run[frame * 2] - run[(frame - 1) * 2]);
        const std::size_t offset = (frame + 480000 % clipFrames) % clipFrames;
        if (offset < 4 || offset + 4 > clipFrames) {
            nearSeam = std::max(nearSeam, step);
        } else {
            sum += step;
            ++count;
        }
    }
    const float elsewhere = static_cast<float>(sum / static_cast<double>(count));
    CHECK(nearSeam <= 4.0f * elsewhere);
}

TEST_CASE("a gain that flips around the threshold changes smoothly")
{
    NullAudio audio;
    const std::uint32_t rain = addRain(audio);
    audio.setAmbience(rain, 1.0f);
    audio.render(48000 * 5);
    const std::vector<float> steady = audio.render(4800);
    const float steadyStep = largestStep(steady);

    std::vector<float> flipping;
    for (int i = 0; i < 200; ++i) {
        audio.setAmbience(rain, i % 2 == 0 ? 0.0f : 1.0f);
        const std::vector<float> block = audio.render(480);
        flipping.insert(flipping.end(), block.begin(), block.end());
    }
    CHECK(largestStep(flipping) <= steadyStep * 1.05f);
}

TEST_CASE("bad gains cannot make the output blow up")
{
    NullAudio audio;
    const std::uint32_t rain = addRain(audio);
    for (const float gain : {std::numeric_limits<float>::quiet_NaN(), -4.0f, 1e9f, std::numeric_limits<float>::infinity()}) {
        audio.setAmbience(rain, gain);
        for (const float s : audio.render(4800)) {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::abs(s) <= 1.0f);
        }
    }
    audio.setAmbience(rain, 0.0f);
    audio.render(48000 * 5);
    CHECK(energy(audio.render(480)) == 0.0);
}
