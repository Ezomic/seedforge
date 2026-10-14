#include "ichor/anim/animation.h"
#include "ichor/assets/audio_cook.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "ichor/audio/null_audio.h"
#include "ichor/sim/scene.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numeric>

using namespace ichor;

namespace {

const std::filesystem::path kAudio = std::filesystem::path(ICHOR_ART_DIR) / "audio";

double energy(const std::vector<float>& stereo, int channel)
{
    double sum = 0.0;
    for (std::size_t i = static_cast<std::size_t>(channel); i < stereo.size(); i += 2) {
        sum += static_cast<double>(stereo[i]) * stereo[i];
    }
    return sum;
}

SceneBundle cuesBundle()
{
    SceneBundle bundle;
    bundle.materials.push_back(Material{.name = "default"});
    for (const char* name : {"A_WindupLight.wav", "A_WindupHeavy.wav", "A_Tell.wav"}) {
        SoundClip clip;
        REQUIRE(cookSound(kAudio / name, clip));
        bundle.sounds.push_back(std::move(clip));
    }
    return bundle;
}

}

TEST_CASE("the three telegraph cues cook and are distinct")
{
    const SceneBundle bundle = cuesBundle();
    REQUIRE(bundle.sounds.size() == 3);
    for (const SoundClip& clip : bundle.sounds) {
        REQUIRE(clip.sampleRate >= 8000);
        REQUIRE(clip.samples.size() > clip.sampleRate / 20); // longer than 50 ms
    }
    REQUIRE(bundle.sounds[0].samples.size() != bundle.sounds[1].samples.size());
    REQUIRE(bundle.sounds[1].samples.size() != bundle.sounds[2].samples.size());

    const auto path = std::filesystem::temp_directory_path() / "ichor_cues.iscene";
    REQUIRE(writeBundle(path, bundle));
    SceneBundle loaded;
    REQUIRE(readBundle(path, loaded));
    std::filesystem::remove(path);
    REQUIRE(loaded.sounds.size() == 3);
    REQUIRE(loaded.sounds[2].samples == bundle.sounds[2].samples);
}

TEST_CASE("a played cue makes sound and then stops")
{
    NullAudio audio;
    audio.addBundle(cuesBundle());
    REQUIRE(audio.findSound("A_Tell") == 2);
    REQUIRE(audio.findSound("nothing") == Mixer::kNoSound);

    REQUIRE(energy(audio.render(480), 0) == 0.0);
    audio.play(audio.findSound("A_Tell"), {0.0, 0.0, 0.0}, false);
    REQUIRE(audio.mixer().activeVoices() == 1);
    const auto first = audio.render(4800);
    REQUIRE(energy(first, 0) > 0.0);
    REQUIRE(energy(first, 0) == energy(first, 1));

    // Drain the whole clip; the voice retires and silence follows.
    for (int i = 0; i < 100; ++i) {
        audio.render(4800);
    }
    REQUIRE(audio.mixer().activeVoices() == 0);
    REQUIRE(energy(audio.render(480), 0) == 0.0);
}

TEST_CASE("a cue's gain reaches the mixer, and playing without one is full volume")
{
    const auto played = [](float gain) {
        NullAudio audio;
        audio.addBundle(cuesBundle());
        AudioHost& host = audio;
        host.play(0, {0.0, 0.0, 0.0}, false, gain);
        return energy(audio.render(2400), 0);
    };
    const double full = played(1.0f);
    REQUIRE(played(0.5f) == Catch::Approx(full * 0.25).epsilon(1e-6));
    REQUIRE(played(0.0f) == 0.0);

    NullAudio audio;
    audio.addBundle(cuesBundle());
    AudioHost& host = audio;
    host.play(0, {0.0, 0.0, 0.0}, false);
    REQUIRE(energy(audio.render(2400), 0) == Catch::Approx(full).epsilon(1e-6));
}

TEST_CASE("spatial cues pan towards their side and fade with distance")
{
    NullAudio audio;
    audio.addBundle(cuesBundle());
    audio.setListener({0.0, 0.0, 0.0}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    audio.play(0, {2.0, 0.0, 0.0}, true); // to the listener's right
    const auto right = audio.render(2400);
    REQUIRE(energy(right, 1) > energy(right, 0) * 4.0);
    for (int i = 0; i < 100; ++i) audio.render(4800);

    audio.play(0, {0.0, 0.0, -1.0}, true);
    const auto near = audio.render(2400);
    for (int i = 0; i < 100; ++i) audio.render(4800);
    audio.play(0, {0.0, 0.0, 0.0}, false);
    const auto flat = audio.render(2400);
    for (int i = 0; i < 100; ++i) audio.render(4800);
    audio.play(0, {0.0, 0.0, 0.0}, true);
    const auto centred = audio.render(2400);
    REQUIRE(std::abs(energy(flat, 0) - energy(centred, 0)) < energy(flat, 0) * 0.001);
    for (int i = 0; i < 100; ++i) audio.render(4800);
    audio.play(0, {0.0, 0.0, -12.0}, true);
    const auto far = audio.render(2400);
    REQUIRE(energy(near, 0) > energy(far, 0) * 10.0);
    REQUIRE(std::abs(energy(near, 0) - energy(near, 1)) < energy(near, 0) * 0.01);
}

TEST_CASE("a montage raises its wind-up cue on the tick it starts")
{
    SceneBundle arm;
    CookStats stats;
    REQUIRE(cookGltf(std::filesystem::path(ICHOR_TEST_DATA) / "arm.glb", arm, stats));
    AnimationSet animations;
    REQUIRE(animations.load(arm));

    Scene scene(5);
    scene.setAnimations(&animations);
    SpawnCommand spawn;
    spawn.id = StableId::of("arm");
    spawn.name.value = "arm";
    spawn.transform.position = {1.0, 0.0, 2.0};
    spawn.animator = Animator{0, 0.0f, 1.0f, true, false};
    scene.enqueue(spawn);
    scene.tick(1.0 / 60.0);
    REQUIRE(scene.soundEvents().empty());

    PlayMontageCommand light{StableId::of("arm"), 0, 0.35f, 0.5f, 0.9f, 7, 2};
    scene.enqueue(light);
    scene.tick(1.0 / 60.0);
    REQUIRE(scene.soundEvents().size() == 1);
    REQUIRE(scene.soundEvents()[0].sound == 7);
    REQUIRE(scene.soundEvents()[0].emitter == StableId::of("arm"));
    REQUIRE(scene.soundEvents()[0].position == glm::dvec3(1.0, 0.0, 2.0));

    // The active cue fires on tick 21 and nothing in between.
    int activeCueTick = -1;
    for (int tick = 2; tick <= 60; ++tick) {
        scene.tick(1.0 / 60.0);
        for (const SoundEvent& event : scene.soundEvents()) {
            REQUIRE(event.sound == 2);
            REQUIRE(activeCueTick < 0);
            activeCueTick = tick;
        }
    }
    REQUIRE(activeCueTick == 21);
}
