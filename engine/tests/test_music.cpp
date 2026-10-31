#include "ichor/assets/bundle.h"
#include "ichor/audio/music_director.h"
#include "ichor/audio/music_player.h"
#include "ichor/audio/null_audio.h"
#include "ichor/data/data_set.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
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

namespace {

MusicCue cue(std::string id, MusicEvent event, float duration = 10.0f, float cooldown = 0.0f)
{
    return MusicCue{.id = std::move(id), .event = event, .duration = duration, .fadeIn = 1.0f, .fadeOut = 2.0f, .cooldown = cooldown, .gain = 1.0f, .rootHz = 220.0f};
}

MusicConfig config()
{
    return MusicConfig{.minGap = 30.0, .preemptGap = 0.5};
}

std::vector<MusicCue> oneOfEach()
{
    return {cue("vista", MusicEvent::Vista), cue("find", MusicEvent::Discovery), cue("fight", MusicEvent::Combat), cue("dies", MusicEvent::Death)};
}

// Plays one event at `now` and returns the cue that started, or npos.
std::size_t startNow(MusicDirector& director, MusicEvent event, double now)
{
    const MusicDecision decision = director.onEvent(event, now);
    if (decision.verdict != MusicVerdict::Accepted) {
        return MusicDirector::npos;
    }
    const MusicUpdate update = director.update(now);
    return update.start ? update.start->cue : MusicDirector::npos;
}

}

TEST_CASE("a cue starts on its event and the director goes quiet when it ends")
{
    MusicDirector director(oneOfEach(), config(), 1);
    REQUIRE(director.level(0.0) == 0.0f);
    REQUIRE(startNow(director, MusicEvent::Discovery, 5.0) == 1);
    REQUIRE(director.playing(5.0));
    REQUIRE(director.playing(14.9));
    REQUIRE_FALSE(director.playing(15.1));
    REQUIRE(director.level(15.1) == 0.0f);
}

TEST_CASE("the cue fades in and out instead of switching")
{
    MusicDirector director(oneOfEach(), config(), 1);
    REQUIRE(startNow(director, MusicEvent::Discovery, 0.0) == 1);
    REQUIRE(director.level(0.0) == 0.0f);
    const float quarter = director.level(0.25);
    REQUIRE(quarter > 0.0f);
    REQUIRE(quarter < 0.5f);
    REQUIRE(director.level(5.0) == 1.0f);
    const float tail = director.level(9.0);
    REQUIRE(tail > 0.0f);
    REQUIRE(tail < 1.0f);
    REQUIRE(director.level(9.99) < tail);
}

TEST_CASE("a second event while a cue plays is refused when it does not outrank it")
{
    MusicDirector director(oneOfEach(), config(), 1);
    REQUIRE(startNow(director, MusicEvent::Combat, 0.0) == 2);
    REQUIRE(director.onEvent(MusicEvent::Discovery, 1.0).verdict == MusicVerdict::Busy);
    REQUIRE(director.onEvent(MusicEvent::Combat, 1.0).verdict == MusicVerdict::Busy);
    REQUIRE(director.onEvent(MusicEvent::Vista, 1.0).verdict == MusicVerdict::Busy);
}

TEST_CASE("events inside the minimum gap are dropped, then accepted once it has passed")
{
    MusicDirector director(oneOfEach(), config(), 1);
    REQUIRE(startNow(director, MusicEvent::Discovery, 0.0) == 1);
    director.update(10.0);
    REQUIRE(director.onEvent(MusicEvent::Discovery, 20.0).verdict == MusicVerdict::Gap);
    REQUIRE(director.onEvent(MusicEvent::Vista, 39.9).verdict == MusicVerdict::Gap);
    REQUIRE(director.onEvent(MusicEvent::Vista, 40.0).verdict == MusicVerdict::Accepted);
}

TEST_CASE("a per-cue cooldown keeps the same cue from repeating, counted from its start")
{
    std::vector<MusicCue> cues = {cue("find", MusicEvent::Discovery, 5.0f, 100.0f)};
    MusicDirector director(cues, config(), 1);
    REQUIRE(startNow(director, MusicEvent::Discovery, 0.0) == 0);
    REQUIRE(director.onEvent(MusicEvent::Discovery, 60.0).verdict == MusicVerdict::Cooldown);
    REQUIRE(director.onEvent(MusicEvent::Discovery, 99.0).verdict == MusicVerdict::Cooldown);
    REQUIRE(director.onEvent(MusicEvent::Discovery, 100.0).verdict == MusicVerdict::Accepted);
}

TEST_CASE("a cue on cooldown is skipped for another cue of the same event")
{
    std::vector<MusicCue> cues = {cue("a", MusicEvent::Discovery, 5.0f, 1000.0f), cue("b", MusicEvent::Discovery, 5.0f, 1000.0f)};
    MusicDirector director(cues, config(), 7);
    const std::size_t first = startNow(director, MusicEvent::Discovery, 0.0);
    const std::size_t second = startNow(director, MusicEvent::Discovery, 100.0);
    REQUIRE(first != MusicDirector::npos);
    REQUIRE(second != MusicDirector::npos);
    REQUIRE(first != second);
    REQUIRE(director.onEvent(MusicEvent::Discovery, 200.0).verdict == MusicVerdict::Cooldown);
}

TEST_CASE("an event with no cue in the data is refused")
{
    std::vector<MusicCue> cues = {cue("find", MusicEvent::Discovery)};
    MusicDirector director(cues, config(), 1);
    REQUIRE(director.onEvent(MusicEvent::Death, 0.0).verdict == MusicVerdict::NoCue);
}

TEST_CASE("death cuts a combat cue, which fades out before the death cue starts")
{
    MusicDirector director(oneOfEach(), config(), 1);
    REQUIRE(startNow(director, MusicEvent::Combat, 0.0) == 2);
    const MusicDecision decision = director.onEvent(MusicEvent::Death, 4.0);
    REQUIRE(decision.verdict == MusicVerdict::Accepted);
    REQUIRE(decision.cue == 3);
    // The combat cue fades over its 2 s fade-out, then the gap, then death.
    REQUIRE(decision.startAt == 4.0 + 2.0 + 0.5);

    const MusicUpdate cut = director.update(4.0);
    REQUIRE(cut.cut);
    REQUIRE(cut.cutFadeSeconds == 2.0f);
    REQUIRE_FALSE(cut.start.has_value());
    REQUIRE(director.level(5.0) < director.level(4.1));
    REQUIRE(director.level(6.0) == 0.0f);
    REQUIRE_FALSE(director.update(6.4).start.has_value());
    REQUIRE_FALSE(director.playing(6.2));

    const MusicUpdate started = director.update(6.5);
    REQUIRE(started.start.has_value());
    REQUIRE(started.start->cue == 3);
    REQUIRE(director.playing(6.6));
}

TEST_CASE("cutting a cue with no fade-out ends it at once and is still reported")
{
    std::vector<MusicCue> cues = oneOfEach();
    cues[2].fadeOut = 0.0f;
    MusicDirector director(cues, config(), 1);
    REQUIRE(startNow(director, MusicEvent::Combat, 0.0) == 2);
    REQUIRE(director.onEvent(MusicEvent::Death, 4.0).verdict == MusicVerdict::Accepted);
    const MusicUpdate cut = director.update(4.0);
    REQUIRE(cut.cut);
    REQUIRE(cut.cutFadeSeconds == 0.0f);
    REQUIRE_FALSE(director.playing(4.0));
}

TEST_CASE("combat beats discovery and death beats combat, but never the other way")
{
    {
        MusicDirector director(oneOfEach(), config(), 1);
        REQUIRE(startNow(director, MusicEvent::Discovery, 0.0) == 1);
        REQUIRE(director.onEvent(MusicEvent::Combat, 2.0).verdict == MusicVerdict::Accepted);
    }
    {
        MusicDirector director(oneOfEach(), config(), 1);
        REQUIRE(startNow(director, MusicEvent::Combat, 0.0) == 2);
        REQUIRE(director.onEvent(MusicEvent::Death, 2.0).verdict == MusicVerdict::Accepted);
    }
    {
        MusicDirector director(oneOfEach(), config(), 1);
        REQUIRE(startNow(director, MusicEvent::Death, 0.0) == 3);
        REQUIRE(director.onEvent(MusicEvent::Combat, 2.0).verdict == MusicVerdict::Busy);
    }
}

TEST_CASE("a waiting cue is replaced by a higher priority event and not by a lower one")
{
    MusicDirector director(oneOfEach(), config(), 1);
    REQUIRE(startNow(director, MusicEvent::Vista, 0.0) == 0);
    const MusicDecision discovery = director.onEvent(MusicEvent::Discovery, 1.0);
    REQUIRE(discovery.verdict == MusicVerdict::Accepted);
    REQUIRE(director.onEvent(MusicEvent::Vista, 1.1).verdict == MusicVerdict::Busy);
    const MusicDecision death = director.onEvent(MusicEvent::Death, 1.2);
    REQUIRE(death.verdict == MusicVerdict::Accepted);
    director.update(1.2);
    const MusicUpdate later = director.update(60.0);
    REQUIRE(later.start.has_value());
    REQUIRE(later.start->cue == 3);
}

TEST_CASE("a higher priority event inside the gap waits only the short gap, a lower one is dropped")
{
    MusicDirector director(oneOfEach(), config(), 1);
    REQUIRE(startNow(director, MusicEvent::Combat, 0.0) == 2);
    director.update(10.0);
    REQUIRE(director.onEvent(MusicEvent::Discovery, 12.0).verdict == MusicVerdict::Gap);
    const MusicDecision death = director.onEvent(MusicEvent::Death, 12.0);
    REQUIRE(death.verdict == MusicVerdict::Accepted);
    REQUIRE(death.startAt == 12.0);
}

TEST_CASE("there is always silence between two cues")
{
    MusicDirector director(oneOfEach(), config(), 3);
    const MusicEvent events[] = {MusicEvent::Vista, MusicEvent::Discovery, MusicEvent::Combat, MusicEvent::Death};
    double lastLevelNonZero = -1.0;
    double smallestGap = 1e9;
    std::vector<double> starts;
    for (int step = 0; step < 4000; ++step) {
        const double now = step * 0.1;
        director.onEvent(events[(step * 7 + step / 13) % 4], now);
        const MusicUpdate update = director.update(now);
        if (update.start) {
            starts.push_back(now);
            if (lastLevelNonZero >= 0.0) {
                smallestGap = std::min(smallestGap, now - lastLevelNonZero);
            }
        }
        if (director.level(now) > 0.0f) {
            lastLevelNonZero = now;
        }
    }
    REQUIRE(starts.size() > 3);
    // Every start comes at least the short gap after the previous cue's level reached zero.
    REQUIRE(smallestGap >= 0.4);
}

TEST_CASE("the same seed and events give the same cues and different seeds can differ")
{
    std::vector<MusicCue> cues;
    for (int i = 0; i < 8; ++i) {
        cues.push_back(cue("d" + std::to_string(i), MusicEvent::Discovery, 4.0f));
    }
    const auto run = [&](std::uint64_t seed) {
        MusicDirector director(cues, config(), seed);
        std::vector<std::size_t> chosen;
        for (int i = 0; i < 20; ++i) {
            chosen.push_back(startNow(director, MusicEvent::Discovery, i * 100.0));
        }
        return chosen;
    };
    REQUIRE(run(42) == run(42));
    bool differs = false;
    for (std::uint64_t seed = 1; seed < 6; ++seed) {
        differs = differs || run(seed) != run(42);
    }
    REQUIRE(differs);
    const std::vector<std::size_t> chosen = run(42);
    REQUIRE(std::count(chosen.begin(), chosen.end(), MusicDirector::npos) == 0);
}

TEST_CASE("rejected events do not move the seeded choice")
{
    std::vector<MusicCue> cues;
    for (int i = 0; i < 8; ++i) {
        cues.push_back(cue("d" + std::to_string(i), MusicEvent::Discovery, 4.0f));
    }
    MusicDirector quiet(cues, config(), 9);
    MusicDirector noisy(cues, config(), 9);
    REQUIRE(startNow(quiet, MusicEvent::Discovery, 0.0) == startNow(noisy, MusicEvent::Discovery, 0.0));
    for (int i = 0; i < 10; ++i) {
        REQUIRE(noisy.onEvent(MusicEvent::Discovery, 5.0 + i * 0.1).verdict != MusicVerdict::Accepted);
    }
    REQUIRE(startNow(quiet, MusicEvent::Discovery, 100.0) == startNow(noisy, MusicEvent::Discovery, 100.0));
}

TEST_CASE("the music table loads, and a bad record loads nothing")
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("ichor_music_" + std::to_string(ICHOR_PID));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "music");
    const auto write = [&](const std::string& text) {
        std::ofstream(root / "music" / "cues.json", std::ios::trunc) << text;
        DataSet set;
        REQUIRE(set.loadDirectory(root));
        return set;
    };

    std::vector<MusicCue> cues;
    const DataSet good = write(R"([{"id":"find","event":"discovery","duration":9,"fade_in":1,"fade_out":2,"cooldown":30,"gain":0.8,"root_hz":220}])");
    REQUIRE(loadMusicCues(good, cues));
    REQUIRE(cues.size() == 1);
    REQUIRE(cues[0].event == MusicEvent::Discovery);
    REQUIRE(cues[0].cooldown == 30.0f);

    const DataSet unknownEvent = write(R"([{"id":"x","event":"party","duration":9,"fade_in":1,"fade_out":2,"cooldown":30,"gain":0.8,"root_hz":220}])");
    REQUIRE_FALSE(loadMusicCues(unknownEvent, cues));
    REQUIRE(cues.size() == 1);

    const DataSet fadesTooLong = write(R"([{"id":"x","event":"death","duration":3,"fade_in":2,"fade_out":2,"cooldown":30,"gain":0.8,"root_hz":220}])");
    REQUIRE_FALSE(loadMusicCues(fadesTooLong, cues));

    const DataSet missing = write(R"([{"id":"x","event":"death","duration":3}])");
    REQUIRE_FALSE(loadMusicCues(missing, cues));
    std::filesystem::remove_all(root);
}

TEST_CASE("the shipped music data is valid and covers every event")
{
    DataSet set;
    REQUIRE(set.loadDirectory(ICHOR_REPO_DATA));
    std::vector<MusicCue> cues;
    REQUIRE(loadMusicCues(set, cues));
    for (const MusicEvent event : {MusicEvent::Vista, MusicEvent::Discovery, MusicEvent::Combat, MusicEvent::Death}) {
        REQUIRE(std::any_of(cues.begin(), cues.end(), [&](const MusicCue& c) { return c.event == event; }));
    }
}

TEST_CASE("a stinger is audible while it plays and silent before and after")
{
    MusicCue c = cue("find", MusicEvent::Discovery, 4.0f);
    NullAudio audio;
    SceneBundle bundle;
    bundle.sounds.push_back(makeMusicClip(c));
    audio.addBundle(bundle);
    MusicPlayer player({c}, config(), 1);
    player.bind(audio);

    const auto rms = [&](double seconds) {
        const std::vector<float> out = audio.render(static_cast<std::uint32_t>(seconds * Mixer::kRate));
        double sum = 0.0;
        for (const float s : out) {
            sum += static_cast<double>(s) * s;
        }
        return std::sqrt(sum / static_cast<double>(out.size()));
    };

    REQUIRE(rms(0.5) == 0.0);
    player.trigger(MusicEvent::Discovery, 0.0);
    player.update(0.0, audio);
    REQUIRE(audio.playsRequested() == 1);
    const double during = rms(2.0); // after the fade in, before the fade out
    const double middle = rms(1.0);
    REQUIRE(during > 0.001);
    REQUIRE(middle > 0.001);
    rms(2.0);
    REQUIRE(rms(1.0) == 0.0);
}

TEST_CASE("a cut fades the playing stinger to silence within the fade")
{
    MusicCue c = cue("fight", MusicEvent::Combat, 20.0f);
    c.fadeOut = 1.0f;
    MusicCue d = cue("dies", MusicEvent::Death, 2.0f);
    d.fadeIn = 0.5f;
    d.fadeOut = 0.5f;
    NullAudio audio;
    SceneBundle bundle;
    bundle.sounds.push_back(makeMusicClip(c));
    bundle.sounds.push_back(makeMusicClip(d));
    audio.addBundle(bundle);
    MusicPlayer player({c, d}, config(), 1);
    player.bind(audio);

    const auto rms = [&](double seconds) {
        const std::vector<float> out = audio.render(static_cast<std::uint32_t>(seconds * Mixer::kRate));
        double sum = 0.0;
        for (const float s : out) {
            sum += static_cast<double>(s) * s;
        }
        return std::sqrt(sum / static_cast<double>(out.size()));
    };

    player.trigger(MusicEvent::Combat, 0.0);
    player.update(0.0, audio);
    const double playing = rms(5.0);
    REQUIRE(playing > 0.001);
    player.trigger(MusicEvent::Death, 5.0);
    player.update(5.0, audio);
    const double fading = rms(0.5); // first half of the fade
    const double fadingLate = rms(0.5);
    REQUIRE(fadingLate < fading);
    REQUIRE(fading < playing);
    REQUIRE(rms(0.2) < 0.005); // the rest of the fade, and the gap after it, are quiet
}
