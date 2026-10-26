#include "ichor/assets/audio_cook.h"
#include "ichor/assets/bundle.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/rain_ambience.h"
#include "ichor/audio/sound_legend.h"
#include "ichor/audio/weather_ambience.h"
#include "ichor/audio/wading_splash.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace ichor;

namespace {

const std::filesystem::path kAudio = std::filesystem::path(ICHOR_ART_DIR) / "audio";

// The wind, rain and splash are made in code, not cut from a recording.
bool madeInCode(std::string_view clip)
{
    return clip == "A_Splash" || clip == "A_Rain" || clip == "A_Wind";
}

std::vector<SoundClip> shippedClips()
{
    std::vector<SoundClip> clips;
    for (const SoundCue& cue : soundCues()) {
        if (cue.clip.empty() || madeInCode(cue.clip)) continue;
        SoundClip clip;
        REQUIRE(cookSound(kAudio / (std::string(cue.clip) + ".wav"), clip));
        clip.name = std::string(cue.clip);
        clips.push_back(std::move(clip));
    }
    return clips;
}

double rmsDb(const SoundClip& clip)
{
    double sum = 0.0;
    for (const std::int16_t s : clip.samples) {
        const double v = static_cast<double>(s) / 32768.0;
        sum += v * v;
    }
    return 10.0 * std::log10(sum / static_cast<double>(clip.samples.size()) + 1e-12);
}

double peakDb(const SoundClip& clip)
{
    int peak = 0;
    for (const std::int16_t s : clip.samples) peak = std::max(peak, std::abs(static_cast<int>(s)));
    return 20.0 * std::log10(static_cast<double>(peak) / 32768.0 + 1e-12);
}

double meanStep(const SoundClip& clip)
{
    double sum = 0.0;
    for (std::size_t i = 1; i < clip.samples.size(); ++i) sum += std::abs(clip.samples[i] - clip.samples[i - 1]);
    return sum / static_cast<double>(clip.samples.size() - 1);
}

bool loops(const SoundCue& cue)
{
    return cue.group == CueGroup::Biome || (cue.group == CueGroup::TimeOfDay && cue.clip != "A_Night_Wolves");
}

}

TEST_CASE("every sound id in the legend resolves to a real clip")
{
    NullAudio host;
    SceneBundle bundle;
    for (SoundClip& clip : shippedClips()) bundle.sounds.push_back(std::move(clip));
    for (const char* name : {"A_WindupLight", "A_WindupHeavy", "A_Tell"}) {
        SoundClip clip;
        REQUIRE(cookSound(kAudio / (std::string(name) + ".wav"), clip));
        bundle.sounds.push_back(std::move(clip));
    }
    bundle.sounds.push_back(makeSplashClip());
    bundle.sounds.push_back(makeRainClip());
    bundle.sounds.push_back(makeWindClip());
    host.addBundle(bundle);

    for (const CueClipStatus& status : auditCueClips(host, {})) {
        INFO("no clip " << status.clip << " for the cue " << status.cue->id);
        REQUIRE(status.present);
    }
    for (const SoundCue& cue : soundCues()) {
        if (cue.clip.empty()) continue;
        INFO(cue.id);
        REQUIRE(resolveCueSound(host, cue.id) == host.findSound(cue.clip));
    }
}

TEST_CASE("each shipped clip is loud enough to hear, quiet enough to sit under the rest, and never clips")
{
    const std::vector<SoundClip> clips = shippedClips();
    REQUIRE(clips.size() >= 24);
    for (const SoundClip& clip : clips) {
        INFO(clip.name);
        REQUIRE(clip.sampleRate >= 22050);
        REQUIRE(clip.samples.size() > clip.sampleRate / 10);
        const double rms = rmsDb(clip);
        REQUIRE(rms > -32.0);
        REQUIRE(rms < -14.0);
        REQUIRE(peakDb(clip) < -0.9);
    }
}

TEST_CASE("a looping bed or hour layer meets its own start without a jump")
{
    for (const SoundClip& clip : shippedClips()) {
        const SoundCue* cue = findCueByClip(clip.name);
        REQUIRE(cue != nullptr);
        if (!loops(*cue)) continue;
        INFO(clip.name);
        const double seam = std::abs(clip.samples.front() - clip.samples.back());
        REQUIRE(seam < 8.0 * meanStep(clip) + 16.0);
        REQUIRE(clip.samples.size() >= static_cast<std::size_t>(clip.sampleRate) * 3);
    }
}
