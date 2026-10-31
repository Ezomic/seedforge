#include "ichor/assets/bundle.h"
#include "ichor/audio/null_audio.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr double kDt = NullPlatform::kTick;

struct Rig {
    NullPlatform platform;
    NullRenderer renderer;
    Engine engine{platform, renderer, EngineConfig{}};
    std::vector<float> seenMoveX;

    explicit Rig(bool pauseOnFocusLoss = true)
    {
        engine.setPauseOnFocusLoss(pauseOnFocusLoss);
        engine.scene().addSystem("watch", [this](Scene&, double, const InputFrame& input) { seenMoveX.push_back(input.moveX); });
    }

    void frames(int count, double dt = kDt)
    {
        for (int i = 0; i < count; ++i) {
            REQUIRE(engine.frame(dt));
        }
    }
};

struct Bed {
    NullAudio audio;
    std::uint32_t clip = 0;

    Bed()
    {
        SceneBundle bundle;
        SoundClip bed;
        bed.name = "A_Bed";
        bed.sampleRate = 48000;
        bed.samples.assign(4800, 32767);
        bundle.sounds.push_back(bed);
        audio.addBundle(bundle);
        clip = audio.findSound("A_Bed");
        audio.setAmbience(clip, 0.5f);
        audio.render(48000 * 2);
    }

    float meanLeft(std::uint32_t frames)
    {
        const std::vector<float> stereo = audio.render(frames);
        double sum = 0.0;
        for (std::size_t i = 0; i < stereo.size(); i += 2) {
            sum += stereo[i];
        }
        return static_cast<float>(sum / static_cast<double>(stereo.size() / 2));
    }
};

InputStream scriptedStream(int ticks)
{
    InputStream stream;
    for (int i = 0; i < ticks; ++i) {
        InputFrame frame;
        frame.moveX = static_cast<float>(i % 7) / 7.0f;
        stream.frames.push_back(frame);
    }
    return stream;
}

}

TEST_CASE("losing focus holds the simulation and regaining it resumes with no tick skipped or owed")
{
    Rig rig;
    rig.frames(5);
    REQUIRE(rig.engine.scene().tickCount() == 5);

    rig.platform.setFocused(false);
    rig.frames(30);
    CHECK(rig.engine.focusPaused());
    CHECK(rig.engine.scene().tickCount() == 5);
    CHECK(rig.engine.frameCount() == 35);

    rig.platform.setFocused(true);
    rig.frames(1);
    CHECK_FALSE(rig.engine.focusPaused());
    CHECK(rig.engine.scene().tickCount() == 6);
    rig.frames(10);
    CHECK(rig.engine.scene().tickCount() == 16);
}

TEST_CASE("a long unfocused frame owes no time once focus is back")
{
    Rig rig;
    rig.platform.setFocused(false);
    rig.frames(1, 5.0);
    rig.platform.setFocused(true);
    rig.frames(1, kDt);
    CHECK(rig.engine.scene().tickCount() == 1);
}

TEST_CASE("without the opt-in a headless or offscreen run never pauses on focus")
{
    Rig rig(false);
    rig.platform.setFocused(false);
    rig.frames(10);
    CHECK_FALSE(rig.engine.focusPaused());
    CHECK(rig.engine.scene().tickCount() == 10);
}

TEST_CASE("a platform without a window always has focus")
{
    NullPlatform platform;
    REQUIRE(platform.focused());
    const Platform& base = platform;
    REQUIRE(base.focused());
}

TEST_CASE("focus lost and regained between two frames does not pause")
{
    Rig rig;
    for (int i = 0; i < 5; ++i) {
        rig.platform.setFocused(false);
        rig.platform.setFocused(true);
        rig.frames(1);
    }
    CHECK(rig.engine.scene().tickCount() == 5);
    CHECK_FALSE(rig.engine.focusPaused());
}

TEST_CASE("regaining focus leaves a menu pause in place")
{
    // The app's menu pause is a frame with no time in it. Time that passed while unfocused must not be paid out
    // when focus returns, or a menu opened meanwhile would let the world run behind it.
    Rig rig;
    rig.frames(3);
    rig.platform.setFocused(false);
    rig.frames(20);
    rig.frames(4, 0.0);
    rig.platform.setFocused(true);
    rig.frames(4, 0.0);
    CHECK(rig.engine.scene().tickCount() == 3);
}

TEST_CASE("a replay ignores focus and ends on the same frame at the same tick")
{
    const InputStream stream = scriptedStream(90);

    Rig steady;
    steady.engine.replayInputs(stream);
    std::vector<std::uint64_t> steadyTicks;
    while (!steady.engine.replayFinished()) {
        steady.frames(1);
        steadyTicks.push_back(steady.engine.scene().tickCount());
    }

    Rig flickering;
    flickering.engine.replayInputs(stream);
    std::vector<std::uint64_t> flickeringTicks;
    while (!flickering.engine.replayFinished()) {
        flickering.platform.setFocused(flickeringTicks.size() % 20 >= 13);
        flickering.frames(1);
        CHECK_FALSE(flickering.engine.focusPaused());
        flickeringTicks.push_back(flickering.engine.scene().tickCount());
    }

    REQUIRE(steady.seenMoveX.size() == stream.frames.size());
    CHECK(flickering.seenMoveX == steady.seenMoveX);
    CHECK(flickeringTicks == steadyTicks);
}

TEST_CASE("a scripted bot run ignores focus changes")
{
    const auto botRun = [](bool flicker) {
        Rig rig;
        int calls = 0;
        rig.engine.setInputSource([&calls](Scene&) {
            InputFrame frame;
            frame.moveX = static_cast<float>(calls++ % 5) / 5.0f;
            return frame;
        });
        for (int i = 0; i < 60; ++i) {
            rig.platform.setFocused(!flicker || i % 9 < 4);
            rig.frames(1);
        }
        return rig.seenMoveX;
    };
    const std::vector<float> steady = botRun(false);
    REQUIRE(steady.size() == 60);
    CHECK(botRun(true) == steady);
}

TEST_CASE("only an open-ended run someone plays pauses on focus loss")
{
    CHECK(pausesOnFocusLoss({}));
    CHECK_FALSE(pausesOnFocusLoss({.headless = true}));
    CHECK_FALSE(pausesOnFocusLoss({.screenshot = true}));
    CHECK_FALSE(pausesOnFocusLoss({.frameLimit = 1}));
    CHECK_FALSE(pausesOnFocusLoss({.frameLimit = 1500}));
    CHECK_FALSE(pausesOnFocusLoss({.headless = true, .screenshot = true, .frameLimit = 60}));
}

TEST_CASE("audio fades to silence while unfocused and back when focus returns")
{
    Rig rig;
    Bed bed;
    rig.engine.setAudio(&bed.audio);
    rig.frames(1);
    const float resting = bed.meanLeft(4800);
    REQUIRE(resting == Approx(0.5f).margin(1e-3));

    rig.platform.setFocused(false);
    rig.frames(1);
    const std::vector<float> onset = bed.audio.render(64);
    CHECK(onset[0] == Approx(resting).margin(0.05));
    bed.audio.render(48000);
    CHECK(std::abs(bed.meanLeft(4800)) < 1e-3f);

    rig.frames(10);
    CHECK(std::abs(bed.meanLeft(4800)) < 1e-3f);

    rig.platform.setFocused(true);
    rig.frames(1);
    bed.audio.render(48000);
    CHECK(bed.meanLeft(4800) == Approx(resting).margin(1e-3));
}

TEST_CASE("an audio host attached while unfocused is silenced too")
{
    Rig rig;
    rig.platform.setFocused(false);
    rig.frames(3);
    Bed bed;
    rig.engine.setAudio(&bed.audio);
    rig.frames(1);
    bed.audio.render(48000);
    CHECK(std::abs(bed.meanLeft(4800)) < 1e-3f);
}

TEST_CASE("a menu pause or a run without the opt-in keeps the sound")
{
    Rig menu;
    Bed menuBed;
    menu.engine.setAudio(&menuBed.audio);
    menu.frames(5, 0.0);
    menuBed.audio.render(48000);
    CHECK(menuBed.meanLeft(4800) == Approx(0.5f).margin(1e-3));

    Rig offscreen(false);
    Bed offscreenBed;
    offscreen.engine.setAudio(&offscreenBed.audio);
    offscreen.platform.setFocused(false);
    offscreen.frames(5);
    offscreenBed.audio.render(48000);
    CHECK(offscreenBed.meanLeft(4800) == Approx(0.5f).margin(1e-3));
}

TEST_CASE("the master gain eases without a step and bad values never poison the mix")
{
    Bed bed;
    bed.audio.setMasterGain(0.0f);
    const std::vector<float> block = bed.audio.render(4800);
    float biggestStep = 0.0f;
    for (std::size_t i = 2; i < block.size(); i += 2) {
        biggestStep = std::max(biggestStep, std::abs(block[i] - block[i - 2]));
    }
    CHECK(biggestStep < 0.01f);
    CHECK(std::abs(bed.meanLeft(4800)) < 1e-6f);

    bed.audio.setMasterGain(0.5f);
    bed.audio.render(4800);
    CHECK(bed.meanLeft(4800) == Approx(0.25f).margin(1e-3));

    const std::pair<float, float> cases[] = {{std::nanf(""), 0.0f}, {-1.0f, 0.0f}, {7.0f, 0.5f}, {INFINITY, 0.5f}};
    for (const auto& [bad, expected] : cases) {
        bed.audio.setMasterGain(bad);
        bed.audio.render(4800);
        CHECK(bed.meanLeft(4800) == Approx(expected).margin(1e-3));
    }
}
