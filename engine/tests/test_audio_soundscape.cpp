#include "ichor/audio/ambience_tod.h"
#include "ichor/audio/null_audio.h"
#include "ichor/audio/soundscape.h"
#include "ichor/data/data_set.h"

#include <worldforge/biome_map.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;
using Catch::Approx;

namespace {

Soundscape realSoundscape()
{
    DataSet data;
    REQUIRE(data.loadDirectory(std::filesystem::path(ICHOR_GAME_DATA_DIR)));
    Soundscape soundscape;
    std::string error;
    REQUIRE(Soundscape::load(data, soundscape, error));
    return soundscape;
}

float totalGain(const std::vector<BedGain>& beds)
{
    float total = 0.0f;
    for (const BedGain& bed : beds) {
        total += bed.gain;
    }
    return total;
}

float gainOf(const std::vector<BedGain>& beds, const std::string& clip)
{
    for (const BedGain& bed : beds) {
        if (bed.clip == clip) {
            return bed.gain;
        }
    }
    return 0.0f;
}

// The largest change of any one clip between two mixes.
float largestJump(const std::vector<BedGain>& a, const std::vector<BedGain>& b)
{
    float jump = 0.0f;
    for (const BedGain& bed : a) {
        jump = std::max(jump, std::abs(bed.gain - gainOf(b, bed.clip)));
    }
    for (const BedGain& bed : b) {
        jump = std::max(jump, std::abs(bed.gain - gainOf(a, bed.clip)));
    }
    return jump;
}

Soundscape small()
{
    Soundscape soundscape;
    soundscape.setBiome("forest", BiomeSounds{"bed_forest", 0.4f, "birds", "bugs", "crickets", "owls"});
    soundscape.setBiome("desert", BiomeSounds{"bed_desert", 0.3f, "", "", "", ""});
    soundscape.setStep(SoundSurface::Grass, StepSound{"step_grass", 0.5f});
    soundscape.setSettlement(SettlementSound::Bell, SettlementClip{"bell", 0.6f});
    return soundscape;
}

}

TEST_CASE("the real soundscape table names every biome, surface and settlement sound")
{
    const Soundscape soundscape = realSoundscape();
    for (const std::string_view name : worldforge::kBiomeNames) {
        INFO(name);
        REQUIRE(soundscape.biome(name) != nullptr);
    }
    for (int s = 0; s < static_cast<int>(kSoundSurfaceCount); ++s) {
        // Wading has its own splash, so water has no footstep.
        if (static_cast<SoundSurface>(s) != SoundSurface::Water) {
            REQUIRE(soundscape.step(static_cast<SoundSurface>(s)) != nullptr);
        }
    }
    for (const SettlementSound sound : {SettlementSound::Chatter, SettlementSound::Bell, SettlementSound::Mill, SettlementSound::Surf, SettlementSound::Machinery}) {
        REQUIRE(soundscape.settlement(sound) != nullptr);
    }
}

TEST_CASE("every biome is audible over a typical day, and dropping its bed turns that red")
{
    const Soundscape soundscape = realSoundscape();
    for (const std::string_view name : worldforge::kBiomeNames) {
        INFO(name);
        const auto meanTotal = [&](const Soundscape& s) {
            float sum = 0.0f;
            for (int h = 0; h < 24; ++h) {
                sum += totalGain(biomeBeds(s, name, static_cast<float>(h) + 0.5f));
            }
            return sum / 24.0f;
        };
        const float mean = meanTotal(soundscape);
        REQUIRE(mean >= 0.15f);

        Soundscape without = soundscape;
        BiomeSounds stripped = *soundscape.biome(name);
        stripped.bed.clear();
        without.setBiome(std::string(name), stripped);
        REQUIRE(meanTotal(without) < mean - 0.1f);
    }
}

TEST_CASE("the selection is a pure table of biome, surface and hour")
{
    const Soundscape soundscape = small();
    const std::vector<BedGain> noon = biomeBeds(soundscape, "forest", 12.0f);
    REQUIRE(noon == biomeBeds(soundscape, "forest", 12.0f));
    REQUIRE(gainOf(noon, "bed_forest") == Approx(0.4f));
    REQUIRE(gainOf(noon, "bugs") == Approx(ambienceTimeOfDay(12.0f).dayInsects));
    REQUIRE(gainOf(noon, "owls") == 0.0f);

    const std::vector<BedGain> midnight = biomeBeds(soundscape, "forest", 0.0f);
    REQUIRE(gainOf(midnight, "owls") == Approx(ambienceTimeOfDay(0.0f).night));
    REQUIRE(gainOf(midnight, "bugs") == 0.0f);
    REQUIRE(biomeBeds(soundscape, "forest", 24.0f) == midnight);
    REQUIRE(biomeBeds(soundscape, "forest", std::numeric_limits<float>::quiet_NaN()) == biomeBeds(soundscape, "forest", kAmbienceFallbackHour));

    REQUIRE(std::is_sorted(noon.begin(), noon.end(), [](const BedGain& a, const BedGain& b) { return a.clip < b.clip; }));
    REQUIRE(biomeBeds(soundscape, "no_such_biome", 12.0f).empty());
    REQUIRE(biomeBeds(Soundscape{}, "forest", 12.0f).empty());

    REQUIRE(footstepFor(soundscape, SoundSurface::Grass)->clip == "step_grass");
    REQUIRE_FALSE(footstepFor(soundscape, SoundSurface::Rock).has_value());
}

TEST_CASE("a clip named by two slots adds up and stays in range")
{
    Soundscape soundscape;
    soundscape.setBiome("b", BiomeSounds{"same", 0.9f, "same", "same", "same", "same"});
    const std::vector<BedGain> beds = biomeBeds(soundscape, "b", 12.0f);
    REQUIRE(beds.size() == 1);
    REQUIRE(beds[0].gain <= 1.0f);
    REQUIRE(beds[0].gain >= 0.9f);
}

TEST_CASE("walking across a biome border crossfades without a step")
{
    const Soundscape soundscape = realSoundscape();
    constexpr int kSteps = 1000;
    std::vector<BedGain> previous = blendedBeds(soundscape, "forest", "desert", 0.0f, 9.0f);
    REQUIRE(gainOf(previous, "A_Bed_Forest") > 0.2f);
    float worst = 0.0f;
    for (int i = 1; i <= kSteps; ++i) {
        const std::vector<BedGain> now = blendedBeds(soundscape, "forest", "desert", static_cast<float>(i) / kSteps, 9.0f);
        worst = std::max(worst, largestJump(previous, now));
        previous = now;
    }
    REQUIRE(worst < 2.0f / kSteps);
    REQUIRE(gainOf(previous, "A_Bed_Forest") == 0.0f);
    REQUIRE(gainOf(previous, "A_Wind") > 0.15f);

    const std::vector<BedGain> middle = blendedBeds(soundscape, "forest", "desert", 0.5f, 9.0f);
    REQUIRE(gainOf(middle, "A_Bed_Forest") > 0.05f);
    REQUIRE(gainOf(middle, "A_Wind") > 0.05f);

    const std::vector<BedGain> nan = blendedBeds(soundscape, "forest", "desert", std::numeric_limits<float>::quiet_NaN(), 9.0f);
    REQUIRE(nan == blendedBeds(soundscape, "forest", "desert", 0.0f, 9.0f));
}

TEST_CASE("the day turns through every hour including midnight without a step")
{
    const Soundscape soundscape = realSoundscape();
    constexpr int kSteps = 24 * 200;
    std::vector<BedGain> previous = biomeBeds(soundscape, "forest", 0.0f);
    float worst = 0.0f;
    float meanBirds = 0.0f;
    for (int i = 1; i <= kSteps; ++i) {
        const float hour = 24.0f * static_cast<float>(i) / kSteps;
        const std::vector<BedGain> now = biomeBeds(soundscape, "forest", hour);
        worst = std::max(worst, largestJump(previous, now));
        meanBirds += gainOf(now, "A_Dawn_Birds");
        previous = now;
    }
    REQUIRE(worst < 0.05f);
    REQUIRE(meanBirds / kSteps > 0.02f);
    REQUIRE(gainOf(biomeBeds(soundscape, "forest", 2.0f), "A_Night_Insects") > gainOf(biomeBeds(soundscape, "forest", 12.0f), "A_Night_Insects"));
}

TEST_CASE("beds add up and the budget scales them together")
{
    std::vector<BedGain> beds{{"a", 0.6f}, {"c", 0.6f}};
    addBeds(beds, {{"b", 0.4f}, {"c", 0.2f}, {"", 0.5f}, {"z", 0.0f}});
    REQUIRE(beds.size() == 3);
    REQUIRE(gainOf(beds, "c") == Approx(0.8f));
    budgetBeds(beds);
    REQUIRE(totalGain(beds) == Approx(1.0f));
    REQUIRE(gainOf(beds, "a") / gainOf(beds, "c") == Approx(0.6f / 0.8f));

    std::vector<BedGain> quiet{{"a", 0.3f}};
    budgetBeds(quiet);
    REQUIRE(gainOf(quiet, "a") == Approx(0.3f));
}

TEST_CASE("the bed mixer eases toward its targets at a bounded rate")
{
    BedMixer mixer;
    const double dt = 1.0 / 60.0;
    const std::vector<BedGain> forest{{"forest", 0.8f}};
    float previous = 0.0f;
    int ticks = 0;
    while (mixer.level("forest") < 0.8f) {
        mixer.step(forest, dt);
        REQUIRE(mixer.level("forest") - previous <= BedMixer::kFadePerSecond * static_cast<float>(dt) + 1e-6f);
        previous = mixer.level("forest");
        REQUIRE(++ticks < 60 * 10);
    }
    REQUIRE(ticks > 60 * 2);

    const std::vector<BedGain> desert{{"desert", 0.8f}};
    float worstTotal = 0.0f;
    for (int i = 0; i < 60 * 10; ++i) {
        mixer.step(desert, dt);
        worstTotal = std::max(worstTotal, mixer.level("forest") + mixer.level("desert"));
    }
    REQUIRE(mixer.level("forest") == 0.0f);
    REQUIRE(mixer.level("desert") == Approx(0.8f));
    REQUIRE(worstTotal < 1.0f);
}

TEST_CASE("the bed mixer reports only what changed and ignores bad time")
{
    BedMixer mixer;
    const std::vector<BedGain> forest{{"forest", 0.5f}};
    REQUIRE(mixer.step(forest, std::numeric_limits<double>::quiet_NaN()).empty());
    REQUIRE(mixer.step(forest, -1.0).empty());
    REQUIRE(mixer.step(forest, 0.0).empty());
    REQUIRE(mixer.step(forest, std::numeric_limits<double>::infinity()).empty());
    REQUIRE(mixer.level("forest") == 0.0f);
    REQUIRE(mixer.step(forest, 1.0).size() == 1);
    for (int i = 0; i < 20; ++i) {
        mixer.step(forest, 1.0);
    }
    REQUIRE(mixer.step(forest, 1.0).empty());

    const std::vector<BedGain> hostile{{"forest", std::numeric_limits<float>::quiet_NaN()}, {"loud", 40.0f}};
    BedMixer other;
    for (int i = 0; i < 20; ++i) {
        other.step(hostile, 1.0);
    }
    REQUIRE(other.level("forest") == 0.0f);
    REQUIRE(other.level("loud") == 1.0f);
}

TEST_CASE("beds reach the null host as loops and a missing clip stays silent")
{
    NullAudio audio;
    SceneBundle bundle;
    SoundClip clip;
    clip.name = "A_Bed_Test";
    clip.sampleRate = 48000;
    clip.samples.assign(4800, 8000);
    bundle.sounds.push_back(clip);
    audio.addBundle(bundle);

    const std::uint32_t present = audio.findSound("A_Bed_Test");
    const std::uint32_t missing = audio.findSound("A_Bed_Absent");
    REQUIRE(present != Mixer::kNoSound);
    REQUIRE(missing == Mixer::kNoSound);

    audio.setAmbience(present, 1.0f);
    audio.render(48000);
    const std::vector<float> loud = audio.render(2048);
    REQUIRE(audio.mixer().activeLoops() == 1);
    audio.setAmbience(present, 0.0f);
    audio.render(48000 * 4);
    const std::vector<float> quiet = audio.render(2048);
    const auto peak = [](const std::vector<float>& v) {
        float p = 0.0f;
        for (const float s : v) {
            p = std::max(p, std::abs(s));
        }
        return p;
    };
    REQUIRE(peak(loud) > 0.05f);
    REQUIRE(peak(quiet) < 0.001f);
}

TEST_CASE("the surface under the feet follows water, road, then the ground")
{
    REQUIRE(soundSurface(1, 0, 0, 0, 0) == SoundSurface::Grass);
    REQUIRE(soundSurface(0, 1, 0, 0, 0) == SoundSurface::Rock);
    REQUIRE(soundSurface(0, 0, 1, 0, 0) == SoundSurface::Snow);
    REQUIRE(soundSurface(0.3f, 0.3f, 0.3f, 0, 0) == SoundSurface::Grass);
    REQUIRE(soundSurface(0.2f, 0.4f, 0.4f, 0, 0) == SoundSurface::Rock);
    REQUIRE(soundSurface(1, 0, 0, 0.5f, 0) == SoundSurface::Road);
    REQUIRE(soundSurface(1, 0, 0, 0.49f, 0) == SoundSurface::Grass);
    REQUIRE(soundSurface(1, 0, 0, 1, 0.3f) == SoundSurface::Water);
    REQUIRE(soundSurface(0, 1, 0, 0, 0.05f) == SoundSurface::Rock);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(soundSurface(nan, nan, nan, nan, nan) == SoundSurface::Grass);
    REQUIRE(soundSurface(0, 1, 0, 0, std::numeric_limits<float>::infinity()) == SoundSurface::Water);
}

TEST_CASE("footsteps follow the ground covered and nothing else")
{
    FootstepPacer pacer;
    int steps = 0;
    for (int i = 0; i <= 1000; ++i) {
        steps += pacer.step(0.1 * i, 0.0) ? 1 : 0;
    }
    REQUIRE(steps == Approx(100.0 / FootstepPacer::kStrideMetres).margin(1));

    FootstepPacer still;
    for (int i = 0; i < 1000; ++i) {
        REQUIRE_FALSE(still.step(5.0, 5.0));
    }

    FootstepPacer teleport;
    teleport.step(0.0, 0.0);
    REQUIRE_FALSE(teleport.step(5000.0, 0.0));

    FootstepPacer bad;
    REQUIRE_FALSE(bad.step(std::numeric_limits<double>::quiet_NaN(), 0.0));
    REQUIRE_FALSE(bad.step(std::numeric_limits<double>::infinity(), 0.0));
    REQUIRE_FALSE(bad.step(1.0, 1.0));
}

TEST_CASE("a settlement is heard near and not far, and silent without a clip")
{
    const Soundscape soundscape = small();
    SettlementSoundType sacred = SettlementSoundType::Sacred;
    const auto near = settlementBed(soundscape, "settlement_0001", 0.5f, sacred, 10.0f, 0.0f, 0.0f);
    REQUIRE(near.has_value());
    REQUIRE(near->clip == "bell");
    const auto far = settlementBed(soundscape, "settlement_0001", 0.5f, sacred, 400.0f, 0.0f, 0.0f);
    REQUIRE((!far.has_value() || far->gain < near->gain));
    REQUIRE_FALSE(settlementBed(soundscape, "settlement_0001", 0.5f, sacred, 5000.0f, 0.0f, 0.0f).has_value());
    REQUIRE_FALSE(settlementBed(soundscape, "settlement_0001", 0.5f, sacred, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f).has_value());
    REQUIRE_FALSE(settlementBed(Soundscape{}, "settlement_0001", 0.5f, sacred, 10.0f, 0.0f, 0.0f).has_value());
    REQUIRE(settlementBed(soundscape, "settlement_0001", 0.5f, sacred, 10.0f, 0.0f, 0.0f) == near);

    REQUIRE(settlementTypeForBiome("tidal_ruin") == SettlementSoundType::Coastal);
    REQUIRE(settlementTypeForBiome("crag") == SettlementSoundType::Mining);
    REQUIRE(settlementTypeForBiome("nonsense") == SettlementSoundType::Inland);
}

TEST_CASE("the soundscape table loads, and a bad record loads nothing")
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("ichor_soundscape_" + std::to_string(ICHOR_PID));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "soundscape");
    const auto attempt = [&](const std::string& text, Soundscape& out, std::string& error) {
        std::ofstream(root / "soundscape" / "s.json", std::ios::trunc) << text;
        DataSet set;
        REQUIRE(set.loadDirectory(root));
        return Soundscape::load(set, out, error);
    };

    Soundscape loaded;
    std::string error;
    REQUIRE(attempt(R"([{"id":"biome_forest","kind":"biome","bed":"b","bed_gain":0.5,"night":"n"},
        {"id":"step_rock","kind":"step","clip":"r","gain":0.4},
        {"id":"settlement_mill","kind":"settlement","clip":"m","gain":0.4}])", loaded, error));
    REQUIRE(loaded.biome("forest")->night == "n");
    REQUIRE(loaded.step(SoundSurface::Rock)->clip == "r");
    REQUIRE(loaded.settlement(SettlementSound::Mill)->clip == "m");
    REQUIRE(loaded.clipNames() == std::vector<std::string>{"b", "m", "n", "r"});

    const std::vector<std::string> bad{
        R"([{"id":"biome_forest","kind":"biome","bed":"b","bed_gain":2}])",
        R"([{"id":"biome_forest","kind":"biome","bed":"b","bed_gain":-1}])",
        R"([{"id":"biome_forest","kind":"biome","bed":"b"}])",
        R"([{"id":"biome_forest","kind":"biome","bed_gain":0.5}])",
        R"([{"id":"biome_forest","kind":"biome","bed":3,"bed_gain":0.5}])",
        R"([{"id":"forest","kind":"biome","bed":"b","bed_gain":0.5}])",
        R"([{"id":"biome_","kind":"biome","bed":"b","bed_gain":0.5}])",
        R"([{"id":"step_lava","kind":"step","clip":"r","gain":0.4}])",
        R"([{"id":"step_rock","kind":"step","gain":0.4}])",
        R"([{"id":"settlement_horn","kind":"settlement","clip":"m","gain":0.4}])",
        R"([{"id":"biome_forest","kind":"weather","bed":"b","bed_gain":0.5}])",
        R"([{"id":"biome_forest","bed":"b","bed_gain":0.5}])",
    };
    for (const std::string& text : bad) {
        INFO(text);
        Soundscape untouched;
        untouched.setStep(SoundSurface::Grass, StepSound{"keep", 0.1f});
        std::string why;
        REQUIRE_FALSE(attempt(text, untouched, why));
        REQUIRE_FALSE(why.empty());
        REQUIRE(untouched.step(SoundSurface::Grass)->clip == "keep");
    }

    Soundscape none;
    REQUIRE(attempt("[]", none, error));
    REQUIRE(none.empty());
    REQUIRE(biomeBeds(none, "forest", 12.0f).empty());
    std::filesystem::remove_all(root);
}
