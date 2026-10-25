#include "ichor/audio/null_audio.h"
#include "ichor/audio/music_stinger.h"
#include "ichor/audio/soundscape.h"
#include "ichor/audio/sound_legend.h"
#include "ichor/core/controls_overlay.h"
#include "ichor/data/data_set.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace ichor;

namespace {

// The clips the app plays by name in code (main.cpp, the combat tuning, the wading splash and the weather beds), kept here
// by hand so a cue dropped from the catalog fails this file.
const std::vector<std::string> kCodeClips = {"A_Tell", "A_WindupLight", "A_WindupHeavy", "A_Splash", "A_Rain", "A_Wind"};

std::vector<MusicCue> realMusic(const DataSet& data)
{
    std::vector<MusicCue> cues;
    REQUIRE(loadMusicCues(data, cues));
    return cues;
}

DataSet realData()
{
    DataSet data;
    REQUIRE(data.loadDirectory(std::filesystem::path(ICHOR_GAME_DATA_DIR)));
    return data;
}

bool legendMentions(const std::vector<std::string>& lines, std::string_view name)
{
    return std::ranges::any_of(lines, [&](const std::string& line) { return line.find(name) != std::string::npos; });
}

SoundClip clipOf(const char* name)
{
    SoundClip clip;
    clip.name = name;
    clip.sampleRate = 44100;
    clip.samples.assign(441, 1000);
    return clip;
}

}

TEST_CASE("every cue has an id, a clip or a music event, a legend line and a caption string")
{
    std::set<std::string_view> ids;
    std::set<std::string_view> clips;
    for (const SoundCue& cue : soundCues()) {
        REQUIRE_FALSE(cue.id.empty());
        REQUIRE_FALSE(cue.name.empty());
        REQUIRE_FALSE(cue.legend.empty());
        REQUIRE_FALSE(cue.caption.empty());
        REQUIRE(ids.insert(cue.id).second);
        if (!cue.clip.empty()) REQUIRE(clips.insert(cue.clip).second);
        REQUIRE(findCue(cue.id) == &cue);
    }
    REQUIRE(findCue("combat.nonsense") == nullptr);
}

TEST_CASE("every clip the code plays, the soundscape names or the music data makes has a cue")
{
    const DataSet data = realData();
    Soundscape soundscape;
    std::string error;
    REQUIRE(Soundscape::load(data, soundscape, error));
    std::vector<std::string> wanted = soundscape.clipNames();
    wanted.insert(wanted.end(), kCodeClips.begin(), kCodeClips.end());
    REQUIRE(wanted.size() > 20);
    for (const std::string& clip : wanted) {
        INFO("no cue for the clip " << clip);
        REQUIRE(findCueByClip(clip) != nullptr);
    }
    const std::vector<MusicCue> music = realMusic(data);
    REQUIRE_FALSE(music.empty());
    for (const MusicCue& m : music) {
        INFO("no cue for the music event of " << m.id);
        REQUIRE(musicCueFor(m.event) != nullptr);
    }
}

TEST_CASE("the legend card carries a line for every clip, event or music cue")
{
    const DataSet data = realData();
    Soundscape soundscape;
    std::string error;
    REQUIRE(Soundscape::load(data, soundscape, error));
    std::vector<std::string> wanted = soundscape.clipNames();
    wanted.insert(wanted.end(), kCodeClips.begin(), kCodeClips.end());
    const std::vector<std::string> lines = soundLegendLines();
    for (const std::string& clip : wanted) {
        const SoundCue* cue = findCueByClip(clip);
        REQUIRE(cue != nullptr);
        INFO("the legend does not mention " << clip);
        REQUIRE(legendMentions(lines, cue->name));
    }
    for (const MusicCue& m : realMusic(data)) {
        REQUIRE(legendMentions(lines, musicCueFor(m.event)->name));
    }
}

TEST_CASE("the legend card gives each event cue its own line with its meaning")
{
    const std::vector<std::string> lines = soundLegendLines();
    REQUIRE(lines.front().find("F2") != std::string::npos);
    for (const SoundCue& cue : soundCues()) {
        if (!announces(cue)) continue;
        const std::string expected = std::string(cue.name) + ": " + std::string(cue.legend);
        REQUIRE(std::ranges::find(lines, expected) != lines.end());
    }
}

TEST_CASE("a caption is a pure function of the cue, and only event cues are announced")
{
    for (const SoundCue& cue : soundCues()) {
        REQUIRE(captionFor(cue) == captionFor(cue));
        if (announces(cue)) {
            REQUIRE(captionFor(cue) == cue.caption);
        } else {
            REQUIRE(captionFor(cue).empty());
        }
    }
    REQUIRE(captionFor(*findCue("combat.windup_light")) == "enemy winding up");
    REQUIRE(announces(*findCue("combat.tell")));
    REQUIRE(announces(*findCue("music.death")));
    REQUIRE_FALSE(announces(*findCue("biome.forest")));
    REQUIRE_FALSE(announces(*findCue("footstep.grass")));
}

TEST_CASE("captions last a fixed number of ticks, renew rather than stack, and never pass the cap")
{
    CaptionFeed feed;
    const SoundCue& light = *findCue("combat.windup_light");
    const SoundCue& heavy = *findCue("combat.windup_heavy");
    REQUIRE(feed.visible(0).empty());
    feed.push(light, 10);
    REQUIRE(feed.visible(10) == std::vector<std::string>{"enemy winding up"});
    REQUIRE(feed.visible(10 + kCaptionTicks - 1).size() == 1);
    REQUIRE(feed.visible(10 + kCaptionTicks).empty());
    feed.push(light, 100);
    feed.push(light, 110);
    REQUIRE(feed.visible(111).size() == 1);
    feed.push(heavy, 112);
    feed.push(*findCue("combat.tell"), 113);
    feed.push(*findCue("music.combat"), 114);
    const std::vector<std::string> shown = feed.visible(115);
    REQUIRE(shown.size() == kMaxCaptions);
    REQUIRE(shown.back() == "music: a fight has started");
    feed.push(*findCue("footstep.grass"), 116);
    REQUIRE(feed.visible(116).size() == kMaxCaptions);
}

TEST_CASE("a caption from before a load does not come back when the tick climbs again")
{
    CaptionFeed feed;
    feed.push(*findCue("combat.tell"), 1000);
    feed.prune(5);
    REQUIRE(feed.visible(1001).empty());
    feed.push(*findCue("combat.tell"), 10);
    feed.prune(200);
    REQUIRE(feed.visible(200).empty());
}

TEST_CASE("captions are on by default and F4 flips them once per press, F2 does the same for the legend")
{
    SoundHelp help;
    REQUIRE(help.captionsOn);
    REQUIRE_FALSE(help.legendOpen);
    for (int i = 0; i < 30; ++i) help = stepSoundHelp(help, false, true);
    REQUIRE_FALSE(help.captionsOn);
    help = stepSoundHelp(help, false, false);
    help = stepSoundHelp(help, false, true);
    REQUIRE(help.captionsOn);
    for (int i = 0; i < 30; ++i) help = stepSoundHelp(help, true, false);
    REQUIRE(help.legendOpen);
}

TEST_CASE("the F1 card shows the legend key and the caption toggle key")
{
    const std::vector<std::string> lines = controlsOverlayLines(startControlsOverlay(false, true));
    REQUIRE(legendMentions(lines, "F2"));
    REQUIRE(legendMentions(lines, "F4"));
}

TEST_CASE("a combat cue with no clip falls back to a placeholder blip that is audible and distinct per cue")
{
    NullAudio host;
    addMissingCombatPlaceholders(host);
    const std::uint32_t tell = resolveCueSound(host, "combat.tell");
    const std::uint32_t light = resolveCueSound(host, "combat.windup_light");
    const std::uint32_t heavy = resolveCueSound(host, "combat.windup_heavy");
    for (const std::uint32_t id : {tell, light, heavy}) REQUIRE(id != Mixer::kNoSound);
    REQUIRE(std::set<std::uint32_t>{tell, light, heavy}.size() == 3);

    host.play(light, glm::dvec3(0.0), false);
    const std::vector<float> out = host.render(2048);
    REQUIRE(std::ranges::any_of(out, [](float v) { return v > 0.01f || v < -0.01f; }));
}

TEST_CASE("an authored combat clip wins over the placeholder, and a missing ambient clip stays silent")
{
    NullAudio host;
    SceneBundle bundle;
    bundle.sounds.push_back(clipOf("A_WindupLight"));
    host.addBundle(bundle);
    addMissingCombatPlaceholders(host);
    REQUIRE(resolveCueSound(host, "combat.windup_light") == host.findSound("A_WindupLight"));
    REQUIRE(host.findSound(placeholderClipName(*findCue("combat.windup_light"))) == Mixer::kNoSound);
    REQUIRE(resolveCueSound(host, "combat.tell") != Mixer::kNoSound);
    REQUIRE(resolveCueSound(host, "biome.forest") == Mixer::kNoSound);
    REQUIRE(resolveCueSound(host, "no.such.cue") == Mixer::kNoSound);
}

TEST_CASE("the audit lists exactly the cues whose clip is missing")
{
    NullAudio host;
    SceneBundle bundle;
    bundle.sounds.push_back(clipOf("A_Tell"));
    bundle.sounds.push_back(clipOf("A_Wind"));
    host.addBundle(bundle);
    addMissingCombatPlaceholders(host);
    const std::vector<CueClipStatus> audit = auditCueClips(host, {});
    std::set<std::string_view> present;
    for (const CueClipStatus& status : audit) {
        REQUIRE(status.cue != nullptr);
        if (status.present) present.insert(status.cue->id);
    }
    REQUIRE(present == std::set<std::string_view>{"combat.tell", "weather.wind"});
    REQUIRE(audit.size() == soundCues().size() - 4);
}

TEST_CASE("the announcer reports the cue behind a sound that starts, once for a bed, and forwards the call")
{
    NullAudio host;
    SceneBundle bundle;
    bundle.sounds.push_back(clipOf("A_Wind"));
    bundle.sounds.push_back(clipOf("unrelated"));
    host.addBundle(bundle);
    addMissingCombatPlaceholders(host);
    MusicCue stinger;
    stinger.id = "t";
    stinger.event = MusicEvent::Death;
    stinger.duration = 1.0f;
    stinger.gain = 1.0f;
    SceneBundle music;
    music.sounds.push_back(makeMusicClip(stinger));
    host.addBundle(music);

    AnnouncingAudio announcer(host);
    announcer.bind(std::vector<MusicCue>{stinger});

    announcer.play(announcer.findSound("A_Missing_combat.windup_heavy"), glm::dvec3(0.0), false);
    announcer.play(announcer.findSound("unrelated"), glm::dvec3(0.0), false);
    announcer.play(announcer.findSound(musicClipName(stinger)), glm::dvec3(0.0), false);
    std::vector<AnnouncingAudio::Fired> fired = announcer.drain();
    REQUIRE(fired.size() == 2);
    REQUIRE(fired[0].cue->id == "combat.windup_heavy");
    REQUIRE(fired[1].cue->id == "music.death");
    REQUIRE(announcer.drain().empty());
    REQUIRE(host.playsRequested() == 3);

    const std::uint32_t wind = announcer.findSound("A_Wind");
    announcer.setAmbience(wind, 0.5f);
    announcer.setAmbience(wind, 0.6f);
    fired = announcer.drain();
    REQUIRE(fired.size() == 1);
    REQUIRE(fired[0].bedStart);
    announcer.setAmbience(wind, 0.0f);
    announcer.setAmbience(wind, 0.5f);
    REQUIRE(announcer.drain().size() == 1);
}
