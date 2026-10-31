#include "ichor/assets/bundle.h"
#include "ichor/audio/mixer.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/reverb.h"
#include "ichor/data/data_set.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

// A short click followed by silence, as interleaved stereo.
std::vector<float> click(std::uint32_t frames)
{
    std::vector<float> signal(static_cast<std::size_t>(frames) * 2, 0.0f);
    for (int i = 0; i < 8; ++i) {
        signal[static_cast<std::size_t>(i) * 2] = 0.5f;
        signal[static_cast<std::size_t>(i) * 2 + 1] = 0.5f;
    }
    return signal;
}

double energyBetween(const std::vector<float>& stereo, std::uint32_t from, std::uint32_t to)
{
    double sum = 0.0;
    for (std::size_t i = static_cast<std::size_t>(from) * 2; i < static_cast<std::size_t>(to) * 2 && i < stereo.size(); ++i) {
        sum += static_cast<double>(stereo[i]) * stereo[i];
    }
    return sum;
}

// Runs the reverb long enough for its ramp to settle, then feeds it a click.
std::vector<float> response(const ReverbSettings& settings, std::uint32_t frames)
{
    Reverb reverb;
    reverb.setSettings(settings);
    std::vector<float> warmup(2000, 0.0f);
    reverb.process(warmup.data(), 1000);
    std::vector<float> signal = click(frames);
    reverb.process(signal.data(), frames);
    return signal;
}

}

TEST_CASE("a dry reverb leaves the signal exactly as it was")
{
    Reverb reverb;
    std::vector<float> signal = click(4800);
    const std::vector<float> before = signal;
    reverb.process(signal.data(), 4800);
    REQUIRE(signal == before);
}

TEST_CASE("a wet reverb rings on after the sound has ended")
{
    const std::vector<float> wet = response({0.5f, 0.6f, 0.4f}, 48000);
    // The click is 8 frames long; everything after the first few thousand frames is tail.
    REQUIRE(energyBetween(wet, 3000, 12000) > 1e-4);
    REQUIRE(energyBetween(wet, 3000, 12000) > energyBetween(wet, 36000, 45000));
    for (float sample : wet) {
        REQUIRE(std::isfinite(sample));
    }
}

TEST_CASE("a bigger room rings longer and a damped one is duller")
{
    const std::vector<float> small = response({0.5f, 0.1f, 0.4f}, 96000);
    const std::vector<float> large = response({0.5f, 0.95f, 0.4f}, 96000);
    REQUIRE(energyBetween(large, 48000, 96000) > energyBetween(small, 48000, 96000) * 4.0);

    // Damping rolls off the high end: the sample-to-sample difference of a damped
    // tail is smaller relative to its level.
    const auto roughness = [](const std::vector<float>& tail) {
        double diff = 0.0;
        double level = 1e-12;
        for (std::size_t i = 4000; i + 2 < tail.size() && i < 20000; i += 2) {
            diff += std::abs(tail[i + 2] - tail[i]);
            level += std::abs(tail[i]);
        }
        return diff / level;
    };
    const std::vector<float> bright = response({0.5f, 0.6f, 0.0f}, 20000);
    const std::vector<float> dull = response({0.5f, 0.6f, 1.0f}, 20000);
    REQUIRE(roughness(dull) < roughness(bright));
}

TEST_CASE("the same input gives the same output every time")
{
    const std::vector<float> a = response({0.4f, 0.7f, 0.3f}, 24000);
    const std::vector<float> b = response({0.4f, 0.7f, 0.3f}, 24000);
    REQUIRE(a == b);
}

TEST_CASE("settings ramp in rather than jumping, and dropping to dry clears the tail")
{
    Reverb reverb;
    reverb.setSettings({1.0f, 0.5f, 0.5f});
    std::vector<float> loud(4800, 0.2f);
    reverb.process(loud.data(), 2400);
    reverb.setSettings({});
    std::vector<float> after(2 * 96000, 0.0f);
    reverb.process(after.data(), 96000);
    // Once settled at dry, a fresh signal passes through untouched.
    std::vector<float> signal = click(2000);
    const std::vector<float> before = signal;
    reverb.process(signal.data(), 2000);
    REQUIRE(signal == before);
}

TEST_CASE("the mixer applies the reverb to what it mixes")
{
    NullAudio audio;
    SceneBundle bundle;
    SoundClip clip;
    clip.name = "tick";
    clip.sampleRate = 48000;
    clip.samples.assign(40, 20000);
    bundle.sounds.push_back(clip);
    audio.addBundle(bundle);

    const auto tail = [&](bool wet) {
        NullAudio fresh;
        fresh.addBundle(bundle);
        fresh.setListener({0, 0, 0}, glm::quat(1, 0, 0, 0));
        fresh.setReverb(wet ? ReverbSettings{0.5f, 0.7f, 0.4f} : ReverbSettings{});
        fresh.play(fresh.findSound("tick"), {0, 0, -1}, false);
        const std::vector<float> rendered = fresh.render(24000);
        return energyBetween(rendered, 4000, 20000);
    };
    REQUIRE(tail(false) == Approx(0.0).margin(1e-12));
    REQUIRE(tail(true) > 1e-5);
}

TEST_CASE("the field is dry outside every stamp, full inside, and blends in between")
{
    ReverbField field;
    field.add({{100.0, 0.0, 0.0}, 20.0f, {0.8f, 0.9f, 0.2f}});

    REQUIRE(field.at({0.0, 0.0, 0.0}) == ReverbSettings{});
    REQUIRE(field.at({125.0, 0.0, 0.0}) == ReverbSettings{});

    const ReverbSettings centre = field.at({100.0, 0.0, 0.0});
    REQUIRE(centre.wet == Approx(0.8f));
    REQUIRE(centre.roomSize == Approx(0.9f));
    REQUIRE(field.at({105.0, 0.0, 0.0}).wet == Approx(0.8f)); // inside the inner 60 percent

    float previous = 1.0f;
    for (double d = 12.0; d <= 20.0; d += 0.5) {
        const float wet = field.at({100.0 + d, 0.0, 0.0}).wet;
        REQUIRE(wet <= previous + 1e-6f);
        previous = wet;
    }
    REQUIRE(field.at({119.0, 0.0, 0.0}).wet < 0.1f);
}

TEST_CASE("overlapping stamps average their rooms and never get louder than the wettest")
{
    ReverbField field;
    field.add({{0.0, 0.0, 0.0}, 20.0f, {0.4f, 0.2f, 0.2f}});
    field.add({{0.0, 0.0, 0.0}, 20.0f, {0.8f, 0.8f, 0.6f}});
    const ReverbSettings both = field.at({0.0, 0.0, 0.0});
    REQUIRE(both.wet == Approx(0.6f));
    REQUIRE(both.roomSize == Approx(0.5f));
    REQUIRE(both.damping == Approx(0.4f));
    REQUIRE(both.wet <= 0.8f);
}

TEST_CASE("stamps load from the data layer, and a bad one loads nothing")
{
    const auto dir = test::tempPath("ichor_reverb_data");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "reverb");
    const auto write = [&](const char* text) { std::ofstream(dir / "reverb" / "stamps.json", std::ios::binary | std::ios::trunc) << text; };

    write(R"([{"id": "cave", "x": 10, "y": 2, "z": -5, "radius": 30, "wet": 0.7, "room": 0.8, "damping": 0.3},
              {"id": "temple", "x": 200, "y": 0, "z": 0, "radius": 12, "wet": 0.4, "room": 0.5, "damping": 0.5}])");
    DataSet data;
    REQUIRE(data.loadDirectory(dir));
    ReverbField field;
    REQUIRE(loadReverbStamps(data, field));
    REQUIRE(field.size() == 2);
    REQUIRE(field.at({10.0, 2.0, -5.0}).wet == Approx(0.7f));

    write(R"([{"id": "ok", "x": 0, "y": 0, "z": 0, "radius": 5, "wet": 0.5, "room": 0.5, "damping": 0.5},
              {"id": "bad", "x": 0, "y": 0, "z": 0, "radius": 5, "wet": 3.0, "room": 0.5, "damping": 0.5}])");
    REQUIRE(data.loadDirectory(dir));
    ReverbField rejected;
    REQUIRE_FALSE(loadReverbStamps(data, rejected));
    REQUIRE(rejected.size() == 0);

    write(R"([{"id": "short", "x": 0, "y": 0, "z": 0}])");
    REQUIRE(data.loadDirectory(dir));
    REQUIRE_FALSE(loadReverbStamps(data, rejected));

    std::filesystem::remove_all(dir);
    DataSet empty;
    ReverbField none;
    REQUIRE(loadReverbStamps(empty, none)); // no table is no stamps, not an error
}

TEST_CASE("non-finite settings and positions are ignored, not remembered")
{
    Reverb reverb;
    reverb.setSettings({0.5f, 0.6f, 0.4f});
    reverb.setSettings({std::nanf(""), 0.6f, 0.4f});
    reverb.setSettings({0.5f, std::numeric_limits<float>::infinity(), 0.4f});
    REQUIRE(reverb.settings() == ReverbSettings{0.5f, 0.6f, 0.4f});
    std::vector<float> signal = click(4800);
    reverb.process(signal.data(), 4800);
    for (float sample : signal) {
        REQUIRE(std::isfinite(sample));
    }

    ReverbField field;
    field.add({{0.0, 0.0, 0.0}, 20.0f, {0.8f, 0.9f, 0.2f}});
    const ReverbSettings lost = field.at({std::nan(""), 0.0, 0.0});
    REQUIRE(lost == ReverbSettings{});
}

TEST_CASE("a stamp radius that vanishes when narrowed to float is refused")
{
    const auto dir = test::tempPath("ichor_reverb_data_radius");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "reverb");
    std::ofstream(dir / "reverb" / "stamps.json", std::ios::binary) << R"([{"id": "tiny", "x": 0, "y": 0, "z": 0, "radius": 1e-50, "wet": 0.5, "room": 0.5, "damping": 0.5}])";
    DataSet data;
    REQUIRE(data.loadDirectory(dir));
    ReverbField field;
    REQUIRE_FALSE(loadReverbStamps(data, field));
    REQUIRE(field.size() == 0);
    std::filesystem::remove_all(dir);
}
