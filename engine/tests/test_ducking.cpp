#include "ichor/audio/ducking.h"
#include "ichor/audio/null_audio.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr std::uint32_t kBlock = 64;

struct Rig {
    NullAudio audio;
    std::uint32_t bed = 0;
    std::uint32_t cue = 0;

    explicit Rig(std::int16_t cueLevel = 0, std::size_t cueFrames = 4800)
    {
        SceneBundle bundle;
        SoundClip bedClip;
        bedClip.name = "A_Bed";
        bedClip.sampleRate = 48000;
        bedClip.samples.assign(4800, 32767);
        bundle.sounds.push_back(bedClip);
        SoundClip cueClip;
        cueClip.name = "S_Cue";
        cueClip.sampleRate = 48000;
        cueClip.samples.assign(cueFrames, cueLevel);
        bundle.sounds.push_back(cueClip);
        audio.addBundle(bundle);
        bed = audio.findSound("A_Bed");
        cue = audio.findSound("S_Cue");
        audio.setAmbience(bed, 0.5f);
        render(48000 * 2);
    }

    std::vector<float> render(std::uint32_t frames) { return audio.render(frames); }
};

float meanLeft(const std::vector<float>& stereo)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < stereo.size(); i += 2) {
        sum += stereo[i];
    }
    return static_cast<float>(sum / static_cast<double>(stereo.size() / 2));
}

}

TEST_CASE("the duck target is the lowest ask among the active categories")
{
    CHECK(duckTarget({}) == 1.0f);
    CHECK(duckTarget({false, true, false, false}) == Approx(1.0f - categoryCeiling(SoundCategory::OneShot)));
    CHECK(duckTarget({false, false, true, false}) == Approx(1.0f - categoryCeiling(SoundCategory::Music)));
    CHECK(duckTarget({false, true, true, true}) == Approx(1.0f - categoryCeiling(SoundCategory::Voice)));
    CHECK(duckTarget({true, false, false, false}) == 1.0f);
}

TEST_CASE("the duck gain moves toward the target without overshoot, faster down than up")
{
    float gain = 1.0f;
    float previous = gain;
    const float dt = 64.0f / 48000.0f;
    int attackSamples = 0;
    while (gain > 0.4f + 0.01f) {
        gain = nextDuckGain(gain, 0.4f, dt);
        REQUIRE(gain <= previous);
        REQUIRE(gain >= 0.4f);
        previous = gain;
        ++attackSamples;
    }
    int releaseSamples = 0;
    while (gain < 1.0f - 0.01f) {
        const float next = nextDuckGain(gain, 1.0f, dt);
        REQUIRE(next >= gain);
        REQUIRE(next <= 1.0f);
        gain = next;
        ++releaseSamples;
    }
    CHECK(attackSamples * 3 < releaseSamples);
}

TEST_CASE("a large step lands on the target and a long run settles exactly")
{
    CHECK(nextDuckGain(1.0f, 0.3f, 100.0f) == 0.3f);
    float gain = 1.0f;
    for (int i = 0; i < 48000 * 30 / 64; ++i) {
        gain = nextDuckGain(gain, 0.3f, 64.0f / 48000.0f);
    }
    CHECK(gain == 0.3f);
    for (int i = 0; i < 48000 * 30 / 64; ++i) {
        gain = nextDuckGain(gain, 1.0f, 64.0f / 48000.0f);
    }
    CHECK(gain == 1.0f);
}

TEST_CASE("bad duck inputs never poison the gain")
{
    CHECK(nextDuckGain(0.7f, 0.2f, 0.0f) == 0.7f);
    CHECK(nextDuckGain(0.7f, 0.2f, -1.0f) == 0.7f);
    CHECK(nextDuckGain(0.7f, 0.2f, kNaN) == 0.7f);
    CHECK(nextDuckGain(kNaN, 1.0f, 0.01f) == 1.0f);
    CHECK(nextDuckGain(0.7f, kNaN, 0.01f) > 0.7f);
    CHECK(nextDuckGain(0.7f, -5.0f, 100.0f) == 0.0f);
    CHECK(nextDuckGain(0.7f, 5.0f, 100.0f) == 1.0f);
}

TEST_CASE("the bed sits well below its resting level while a one-shot plays, then recovers")
{
    Rig rig;
    const float resting = meanLeft(rig.render(4800));
    REQUIRE(resting == Approx(0.5f).margin(1e-3));

    rig.audio.play(rig.cue, glm::dvec3(0.0), false, 1.0f, SoundCategory::OneShot);
    rig.render(2400);
    const float during = meanLeft(rig.render(2400));
    CHECK(during < resting * 0.7f);

    rig.render(48000 * 5);
    CHECK(meanLeft(rig.render(4800)) == Approx(resting).margin(1e-3));
}

TEST_CASE("louder categories duck deeper and a voice without a category does not duck")
{
    float level[4]{};
    const SoundCategory categories[] = {SoundCategory::None, SoundCategory::Music, SoundCategory::OneShot, SoundCategory::Voice};
    for (int i = 0; i < 4; ++i) {
        Rig rig(0, 48000 * 3);
        rig.audio.play(rig.cue, glm::dvec3(0.0), false, 1.0f, categories[i]);
        rig.render(48000);
        level[i] = meanLeft(rig.render(4800));
    }
    CHECK(level[0] == Approx(0.5f).margin(1e-3));
    CHECK(level[1] == Approx(0.5f * (1.0f - categoryCeiling(SoundCategory::Music))).margin(2e-3));
    CHECK(level[3] < level[2]);
    CHECK(level[2] < level[1]);
}

TEST_CASE("ducked bed plus a full-gain categorised voice stays under full scale")
{
    Rig rig(32767, 48000 * 3);
    rig.audio.setAmbience(rig.bed, 0.9f);
    rig.render(48000);
    rig.audio.play(rig.cue, glm::dvec3(0.0), false, 1.0f, SoundCategory::Voice);
    const std::vector<float> out = rig.render(48000);
    const float peak = *std::max_element(out.begin(), out.end());
    CHECK(peak <= 1.0f);
    CHECK(peak < 0.999f);
    CHECK(meanLeft(std::vector<float>(out.end() - 4800, out.end())) == Approx(0.97f).margin(0.02));
}

TEST_CASE("switching the source on and off every few blocks never steps the bed")
{
    Rig rig(0, 96);
    std::vector<float> all;
    for (int block = 0; block < 600; ++block) {
        if (block % 5 == 0) {
            rig.audio.play(rig.cue, glm::dvec3(0.0), false, 1.0f, SoundCategory::OneShot);
        }
        const std::vector<float> out = rig.render(kBlock);
        all.insert(all.end(), out.begin(), out.end());
    }
    float worstStep = 0.0f;
    for (std::size_t i = 2; i < all.size(); i += 2) {
        worstStep = std::max(worstStep, std::abs(all[i] - all[i - 2]));
    }
    CHECK(worstStep < 4e-4f);
    CHECK(meanLeft(all) < 0.5f * 0.8f);
}

TEST_CASE("overlapping sources and zero-length blocks are harmless")
{
    Rig rig(0, 48000);
    rig.audio.play(rig.cue, glm::dvec3(0.0), false, 1.0f, SoundCategory::Music);
    rig.audio.play(rig.cue, glm::dvec3(0.0), false, 1.0f, SoundCategory::Voice);
    rig.audio.play(rig.cue, glm::dvec3(0.0), false, kNaN, SoundCategory::OneShot);
    CHECK(rig.render(0).empty());
    rig.render(4800);
    const std::vector<float> out = rig.render(4800);
    for (const float sample : out) {
        REQUIRE(std::isfinite(sample));
    }
    CHECK(meanLeft(out) < 0.5f * 0.5f);
}

TEST_CASE("the ducked mix is the same on every run")
{
    std::vector<float> runs[2];
    for (auto& run : runs) {
        Rig rig(1000, 9600);
        for (int block = 0; block < 100; ++block) {
            if (block % 17 == 0) {
                rig.audio.play(rig.cue, glm::dvec3(0.0), false, 0.8f, SoundCategory::Voice);
            }
            const std::vector<float> out = rig.render(480);
            run.insert(run.end(), out.begin(), out.end());
        }
    }
    CHECK(runs[0] == runs[1]);
}
