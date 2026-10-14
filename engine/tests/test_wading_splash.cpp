#include "ichor/audio/null_audio.h"
#include "ichor/audio/wading_splash.h"
#include "ichor/core/wading.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

// Walks `ticks` ticks of `perTick` metres along x at a fixed depth and counts the cues.
int splashesWalking(WadingSplash& splash, double perTick, int ticks, float depth, double startX = 0.0)
{
    int cues = 0;
    for (int i = 0; i <= ticks; ++i) {
        cues += splash.step(startX + perTick * i, 0.0, depth).has_value() ? 1 : 0;
    }
    return cues;
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

TEST_CASE("walking through water splashes once per stride")
{
    WadingSplash splash;
    // 0.05 m a tick for 225 ticks is 11.25 m, ten strides of 1.1 m.
    REQUIRE(splashesWalking(splash, 0.05, 225, 0.5f) == 10);
}

TEST_CASE("moving faster splashes more often")
{
    WadingSplash slow;
    WadingSplash fast;
    const int slowCues = splashesWalking(slow, 0.02, 300, 0.5f);
    const int fastCues = splashesWalking(fast, 0.06, 300, 0.5f);
    REQUIRE(fastCues > slowCues * 2);
}

TEST_CASE("standing still in water is silent")
{
    WadingSplash splash;
    REQUIRE(splashesWalking(splash, 0.0, 1000, 1.0f, 5.0) == 0);
}

TEST_CASE("repeated identical positions never accumulate distance")
{
    WadingSplash splash;
    for (int i = 0; i < 500; ++i) {
        REQUIRE_FALSE(splash.step(3.0, -4.0, 1.0f).has_value());
    }
}

TEST_CASE("the first tick has no previous position, so it is silent")
{
    WadingSplash splash;
    REQUIRE_FALSE(splash.step(1000.0, 1000.0, 1.0f).has_value());
}

TEST_CASE("dry ground and shallow water are silent, the wading threshold is not wading")
{
    for (const float depth : {0.0f, -1.0f, -1e30f, kWadeStart, kWadeStart * 0.5f}) {
        WadingSplash splash;
        REQUIRE(splashesWalking(splash, 0.1, 400, depth) == 0);
    }
    WadingSplash splash;
    REQUIRE(splashesWalking(splash, 0.1, 400, std::nextafter(kWadeStart, 1.0f)) > 0);
}

TEST_CASE("depths that are not numbers are silent")
{
    for (const float depth : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        WadingSplash splash;
        REQUIRE(splashesWalking(splash, 0.1, 400, depth) == 0);
    }
}

TEST_CASE("positions that are not numbers are silent and do not poison later steps")
{
    WadingSplash splash;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    REQUIRE_FALSE(splash.step(nan, 0.0, 1.0f).has_value());
    REQUIRE_FALSE(splash.step(0.0, inf, 1.0f).has_value());
    REQUIRE(splashesWalking(splash, 0.1, 400, 1.0f) > 0);
}

TEST_CASE("deeper water is louder, and the gain stays in range")
{
    const auto firstGain = [](float depth) {
        WadingSplash splash;
        for (int i = 0; i < 1000; ++i) {
            if (const auto cue = splash.step(0.1 * i, 0.0, depth)) {
                return cue->gain;
            }
        }
        return -1.0f;
    };
    const float shallow = firstGain(kWadeStart + 0.01f);
    const float mid = firstGain((kWadeStart + kWadeFull) * 0.5f);
    const float deep = firstGain(kWadeFull);
    REQUIRE(shallow > 0.0f);
    REQUIRE(shallow < mid);
    REQUIRE(mid < deep);
    REQUIRE(deep == Approx(1.0f));
    REQUIRE(firstGain(1e30f) == Approx(1.0f));
    REQUIRE(firstGain(std::numeric_limits<float>::max()) == Approx(1.0f));
}

TEST_CASE("a teleport does not splash and the walk resumes from the new place")
{
    WadingSplash splash;
    REQUIRE(splashesWalking(splash, 0.05, 10, 1.0f) == 0);
    REQUIRE_FALSE(splash.step(1.0e6, -1.0e6, 1.0f).has_value());
    REQUIRE(splashesWalking(splash, 0.05, 225, 1.0f, 1.0e6) == 10);
}

TEST_CASE("a huge but finite jump yields at most one cue")
{
    WadingSplash splash;
    splash.step(0.0, 0.0, 1.0f);
    REQUIRE_FALSE(splash.step(1.0e300, 0.0, 1.0f).has_value());
}

TEST_CASE("leaving the water forgets the stride, so a short wade does not splash on the shore")
{
    WadingSplash splash;
    double x = 0.0;
    int cues = 0;
    for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 15; ++i) { // 0.75 m wet, under one stride
            x += 0.05;
            cues += splash.step(x, 0.0, 0.5f).has_value() ? 1 : 0;
        }
        for (int i = 0; i < 15; ++i) {
            x += 0.05;
            cues += splash.step(x, 0.0, 0.0f).has_value() ? 1 : 0;
        }
    }
    REQUIRE(cues == 0);
}

TEST_CASE("the same walk gives the same cues")
{
    WadingSplash a;
    WadingSplash b;
    for (int i = 0; i < 2000; ++i) {
        const double x = std::sin(i * 0.01) * 30.0;
        const double z = std::cos(i * 0.013) * 30.0;
        const float depth = 0.05f + static_cast<float>(i % 40) * 0.05f;
        const auto cueA = a.step(x, z, depth);
        const auto cueB = b.step(x, z, depth);
        REQUIRE(cueA.has_value() == cueB.has_value());
        if (cueA) {
            REQUIRE(cueA->gain == cueB->gain);
        }
    }
}

TEST_CASE("the splash clip is a short, audible, deterministic sound")
{
    const SoundClip clip = makeSplashClip();
    REQUIRE(clip.name == "A_Splash");
    REQUIRE(clip.sampleRate > 0);
    const double seconds = static_cast<double>(clip.samples.size()) / clip.sampleRate;
    REQUIRE(seconds > 0.1);
    REQUIRE(seconds < 1.0);
    int peak = 0;
    for (const std::int16_t s : clip.samples) {
        peak = std::max(peak, std::abs(static_cast<int>(s)));
    }
    REQUIRE(peak > 3000);
    REQUIRE(peak < 32767);
    REQUIRE(makeSplashClip().samples == clip.samples);
}

TEST_CASE("the splash clip plays through the host and can be found by name")
{
    SceneBundle bundle;
    bundle.sounds.push_back(makeSplashClip());
    NullAudio audio;
    audio.addBundle(bundle);
    const std::uint32_t cue = audio.findSound("A_Splash");
    REQUIRE(cue != Mixer::kNoSound);
    audio.play(cue, {0.0, 0.0, 0.0}, false, 1.0f);
    REQUIRE(energy(audio.render(4800)) > 0.0);
}
