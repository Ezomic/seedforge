#pragma once

#include "ichor/audio/audio_host.h"
#include "ichor/audio/music_director.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// What each sound the game can start means, for the player. The table is the list of cues the code
// really plays: the combat wind-ups and tell, the wading splash, the weather and biome beds, the
// footsteps, the settlement sounds and the music stingers. The legend card, the captions, the audio
// log and the missing-clip audit all read it. Presentation only: nothing here reaches the simulation
// or a hash, and no wall clock is read.

enum class CueGroup : std::uint8_t {
    Combat,     // an enemy is about to act: the sounds worth reading in a fight
    Music,      // a stinger for a vista, a discovery, a fight or a death
    Footstep,   // ground and water under the player's feet
    Weather,    // wind and rain
    Biome,      // the bed under each kind of country
    TimeOfDay,  // the layer that joins a bed at dawn, day, dusk or night
    Settlement, // what a settlement sounds like from outside it
};
constexpr std::size_t kCueGroupCount = 7;

struct SoundCue {
    std::string_view id;      // unique, "<group>.<name>"
    CueGroup group = CueGroup::Combat;
    std::string_view clip;    // the clip it plays; empty for a music stinger, whose clips come from data/music
    std::string_view name;    // short, for the ambient group lines
    std::string_view legend;  // what the sound is and what it tells the player
    std::string_view caption; // the on-screen text
};

std::span<const SoundCue> soundCues();
const SoundCue* findCue(std::string_view id);
const SoundCue* findCueByClip(std::string_view clip);
const SoundCue* musicCueFor(MusicEvent event);

// Ambient cues loop or repeat under play and never get a caption; the rest are events worth announcing.
bool announces(const SoundCue& cue);

// The legend card: the event cues one line each, the ambient groups one line each listing what they hold.
std::vector<std::string> soundLegendLines();

// The text shown when `cue` fires, or empty when it is not announced.
std::string captionFor(const SoundCue& cue);

// F2 shows the legend card, F4 turns the captions on or off; a held key toggles once.
struct SoundHelp {
    bool legendOpen = false;
    bool captionsOn = true;
    bool legendHeld = false;
    bool captionHeld = false;
};
SoundHelp stepSoundHelp(SoundHelp state, bool legendKey, bool captionKey);

// Captions that fit on screen at once, and how long one lasts in simulation ticks.
constexpr std::size_t kMaxCaptions = 3;
constexpr std::uint64_t kCaptionTicks = 150;

// The captions on screen, newest last. A cue that fires again while its caption is still up
// renews it rather than stacking a second line. Time is the simulation tick, never the wall clock.
class CaptionFeed {
public:
    void push(const SoundCue& cue, std::uint64_t tick);
    std::vector<std::string> visible(std::uint64_t tick) const;
    // Drops captions that have expired or lie in the future (the tick went back after a load).
    void prune(std::uint64_t tick);

private:
    struct Entry {
        const SoundCue* cue = nullptr;
        std::uint64_t tick = 0;
    };
    std::vector<Entry> entries_;
};

// A short two-note beep for a combat cue whose clip is missing, so silence is not mistaken for a bug.
// Each cue gets its own pitch, and none is like any authored cue.
std::string placeholderClipName(const SoundCue& cue);
void addMissingCombatPlaceholders(AudioHost& host);
// The clip to start for the cue with this id: its own, or its placeholder when a combat clip is missing.
// kNoSound for an unknown id, or an ambient cue whose clip is missing (those stay silent).
std::uint32_t resolveCueSound(const AudioHost& host, std::string_view id);

struct CueClipStatus {
    const SoundCue* cue = nullptr;
    std::string clip; // the clip looked for
    bool present = false;
};
// One entry per cue, music stingers included (one per loaded music cue). `music` is the loaded music cues.
std::vector<CueClipStatus> auditCueClips(const AudioHost& host, std::span<const MusicCue> music);

// Sits in front of a host and notes which catalog cues start, so the caller can caption and log them.
// Every call is forwarded unchanged.
class AnnouncingAudio final : public AudioHost {
public:
    struct Fired {
        const SoundCue* cue = nullptr;
        bool bedStart = false; // an ambience bed becoming audible rather than a one-shot
    };

    explicit AnnouncingAudio(AudioHost& inner)
        : inner_(inner)
    {
    }

    // Looks every catalog clip up in the host; call after all bundles are added.
    void bind(std::span<const MusicCue> music);
    // The cues that fired since the last call.
    std::vector<Fired> drain();

    bool init() override { return inner_.init(); }
    void addBundle(const SceneBundle& bundle) override { inner_.addBundle(bundle); }
    std::uint32_t findSound(std::string_view name) const override { return inner_.findSound(name); }
    void play(std::uint32_t sound, glm::dvec3 position, bool spatial, float gain = 1.0f, SoundCategory category = SoundCategory::None) override;
    void setAmbience(std::uint32_t sound, float gain) override;
    void fadeOutMusic(float seconds) override { inner_.fadeOutMusic(seconds); }
    void setListener(glm::dvec3 position, glm::quat rotation) override { inner_.setListener(position, rotation); }
    void setReverb(const ReverbSettings& settings) override { inner_.setReverb(settings); }

private:
    struct Known {
        std::uint32_t sound = 0;
        const SoundCue* cue = nullptr;
        bool audible = false;
    };
    Known* find(std::uint32_t sound);

    AudioHost& inner_;
    std::vector<Known> known_;
    std::vector<Fired> fired_;
};

}
