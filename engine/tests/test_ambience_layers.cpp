#include "ichor/assets/bundle.h"
#include "ichor/audio/ambience_layers.h"
#include "ichor/audio/null_audio.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr AmbienceLayer kAll[] = {AmbienceLayer::Bed, AmbienceLayer::TimeOfDay, AmbienceLayer::Weather,
    AmbienceLayer::Proximity, AmbienceLayer::OneShots, AmbienceLayer::Territory};

// Six constant full-scale beds, the loudest thing the layers can be asked to play.
void addBeds(NullAudio& audio, AmbienceLayers& layers, std::int16_t level = 32767)
{
    SceneBundle bundle;
    for (int i = 0; i < 6; ++i) {
        SoundClip clip;
        clip.name = "A_Bed" + std::to_string(i);
        clip.sampleRate = 48000;
        clip.samples.assign(4800, level);
        bundle.sounds.push_back(clip);
    }
    audio.addBundle(bundle);
    for (int i = 0; i < 6; ++i) {
        REQUIRE(layers.assign(kAll[i], audio.findSound("A_Bed" + std::to_string(i))));
    }
}

float lastLeft(const std::vector<float>& stereo)
{
    return stereo[stereo.size() - 2];
}

}

TEST_CASE("the ease moves toward the target a little at a time and reaches it")
{
    float gain = 0.0f;
    float previous = 0.0f;
    for (int i = 0; i < 48000 * 6; ++i) {
        gain = easeGain(gain, 1.0f);
        REQUIRE(gain >= previous);
        REQUIRE(gain - previous < 1e-4f);
        previous = gain;
    }
    CHECK(gain == Approx(1.0f).margin(1e-3));
    CHECK(easeGain(0.0f, 1.0f) < 1e-3f);
}

TEST_CASE("a fading gain lands on exactly zero, not a denormal")
{
    float gain = 1.0f;
    for (int i = 0; i < 48000 * 20; ++i) {
        gain = easeGain(gain, 0.0f);
    }
    CHECK(gain == 0.0f);
}

TEST_CASE("a very long run at a steady gain does not drift")
{
    float gain = 0.37f;
    for (int i = 0; i < 48000 * 600; ++i) {
        gain = easeGain(gain, 0.37f);
    }
    CHECK(gain == Approx(0.37f).margin(1e-6));
}

TEST_CASE("gains are clamped to 0..1 and NaN keeps the previous value")
{
    AmbienceLayers layers;
    layers.setGain(AmbienceLayer::Weather, 0.4f);
    layers.setGain(AmbienceLayer::Weather, std::numeric_limits<float>::quiet_NaN());
    CHECK(layers.gain(AmbienceLayer::Weather) == 0.4f);
    layers.setGain(AmbienceLayer::Weather, 9.0f);
    CHECK(layers.gain(AmbienceLayer::Weather) == 1.0f);
    layers.setGain(AmbienceLayer::Weather, std::numeric_limits<float>::infinity());
    CHECK(layers.gain(AmbienceLayer::Weather) == 1.0f);
    layers.setGain(AmbienceLayer::Weather, -std::numeric_limits<float>::infinity());
    CHECK(layers.gain(AmbienceLayer::Weather) == 0.0f);
}

TEST_CASE("an unknown layer is ignored")
{
    AmbienceLayers layers;
    const auto bogus = static_cast<AmbienceLayer>(42);
    layers.setGain(bogus, 1.0f);
    CHECK(layers.gain(bogus) == 0.0f);
    CHECK(layers.mixedGain(bogus) == 0.0f);
    CHECK_FALSE(layers.assign(bogus, 3));
    for (const AmbienceLayer layer : kAll) {
        CHECK(layers.gain(layer) == 0.0f);
    }
}

TEST_CASE("a bed can only belong to one layer and kNoSound is refused")
{
    AmbienceLayers layers;
    CHECK(layers.assign(AmbienceLayer::Bed, 2));
    CHECK_FALSE(layers.assign(AmbienceLayer::Weather, 2));
    CHECK_FALSE(layers.assign(AmbienceLayer::Weather, Mixer::kNoSound));
    CHECK(layers.assign(AmbienceLayer::Bed, 2));
}

TEST_CASE("gains under the budget pass through and gains over it share it equally")
{
    CHECK(budgetedGains({0.2f, 0.3f, 0.1f, 0, 0, 0})[1] == Approx(0.3f));
    const auto six = budgetedGains({1, 1, 1, 1, 1, 1});
    float sum = 0.0f;
    for (const float gain : six) {
        CHECK(gain == Approx(1.0f / 6.0f));
        sum += gain;
    }
    CHECK(sum == Approx(kAmbienceBudget));
    const auto two = budgetedGains({0.9f, 0.3f, 0, 0, 0, 0});
    CHECK(two[0] / two[1] == Approx(3.0f));
    CHECK(two[0] + two[1] == Approx(kAmbienceBudget));
}

TEST_CASE("bad requests cannot inflate or dodge the budget")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto out = budgetedGains({nan, -5.0f, 1.0f, 1.0f, 0, 0});
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[2] + out[3] == Approx(kAmbienceBudget));
}

TEST_CASE("the budget is continuous where it starts to bite")
{
    const float below = budgetedGains({0.5f, 0.4999f, 0, 0, 0, 0})[0];
    const float above = budgetedGains({0.5f, 0.5001f, 0, 0, 0, 0})[0];
    CHECK(std::abs(above - below) < 1e-3f);
}

TEST_CASE("a layer with no bed does not eat the budget")
{
    AmbienceLayers layers;
    layers.assign(AmbienceLayer::Weather, 0);
    layers.setGain(AmbienceLayer::Weather, 0.6f);
    layers.setGain(AmbienceLayer::Territory, 1.0f);
    CHECK(layers.mixedGain(AmbienceLayer::Weather) == Approx(0.6f));
    CHECK(layers.mixedGain(AmbienceLayer::Territory) == 0.0f);
}

TEST_CASE("all six layers at full gain share the budget instead of clipping")
{
    NullAudio audio;
    AmbienceLayers layers;
    addBeds(audio, layers, 16384); // half scale: unbudgeted, six of them would sum to 3 and hit the mixer's clamp
    for (const AmbienceLayer layer : kAll) {
        layers.setGain(layer, 1.0f);
    }
    layers.apply(audio);
    audio.render(48000 * 4);
    float peak = 0.0f;
    for (int block = 0; block < 40; ++block) {
        for (const float s : audio.render(480)) {
            peak = std::max(peak, std::abs(s));
        }
    }
    CHECK(peak == Approx(0.5f).margin(0.01));
    CHECK(audio.mixer().activeLoops() == 6);
}

TEST_CASE("a gain that jumps from 0 to 1 in one block rises in bounded steps")
{
    NullAudio audio;
    AmbienceLayers layers;
    addBeds(audio, layers);
    layers.setGain(AmbienceLayer::Bed, 1.0f);
    layers.apply(audio);
    const std::vector<float> block = audio.render(480);
    float worst = 0.0f;
    for (std::size_t i = 1; i < block.size() / 2; ++i) {
        worst = std::max(worst, std::abs(block[i * 2] - block[(i - 1) * 2]));
    }
    CHECK(worst < 1e-3f);
    CHECK(lastLeft(block) < 0.05f);
}

TEST_CASE("layers are driven independently")
{
    NullAudio audio;
    AmbienceLayers layers;
    addBeds(audio, layers);
    layers.setGain(AmbienceLayer::Weather, 0.5f);
    layers.apply(audio);
    audio.render(48000 * 4);
    const float weatherOnly = lastLeft(audio.render(480));
    CHECK(weatherOnly == Approx(0.5f).margin(0.01));
    layers.setGain(AmbienceLayer::Bed, 0.25f);
    layers.apply(audio);
    audio.render(48000 * 4);
    CHECK(lastLeft(audio.render(480)) == Approx(0.75f).margin(0.01));
    layers.setGain(AmbienceLayer::Weather, 0.0f);
    layers.apply(audio);
    audio.render(48000 * 4);
    CHECK(lastLeft(audio.render(480)) == Approx(0.25f).margin(0.01));
}

TEST_CASE("a zero-length render is harmless")
{
    NullAudio audio;
    AmbienceLayers layers;
    addBeds(audio, layers);
    layers.setGain(AmbienceLayer::Bed, 1.0f);
    layers.apply(audio);
    CHECK(audio.render(0).empty());
}
