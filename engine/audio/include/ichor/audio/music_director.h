#pragma once

#include "ichor/core/rng.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

class DataSet;

// What the game tells the music about. Music is a rare event, not a bed: each of these may start one
// cue, and between cues there is silence.
enum class MusicEvent : std::uint8_t { Vista, Discovery, Combat, Death };

// Death beats combat beats discovery beats vista. A higher priority event cuts a playing cue short; an
// equal or lower one waits for nothing and is dropped.
constexpr int musicPriority(MusicEvent event)
{
    return static_cast<int>(event);
}

// One authored cue: which event it answers and the shape of its envelope. Times are seconds.
struct MusicCue {
    std::string id;
    MusicEvent event = MusicEvent::Vista;
    float duration = 0.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;
    float cooldown = 0.0f; // this cue may not start again until this long after it last started
    float gain = 1.0f;
    float rootHz = 220.0f; // the pitch the placeholder stinger is built on
};

// Positive duration, fades that fit inside it, a gain in 0..1, an audible root.
bool validMusicCue(const MusicCue& cue);

// 0 outside the cue, rising over fadeIn, 1 between the fades, falling over fadeOut. Smooth at both
// ends. `t` is seconds since the cue started.
float cueEnvelope(const MusicCue& cue, double t);

// Reads the "music" table: id, event (vista|discovery|combat|death), duration, fade_in, fade_out,
// cooldown, gain, root_hz. All or nothing: on any bad record `out` is left as it was. A missing table
// loads zero cues and succeeds.
bool loadMusicCues(const DataSet& data, std::vector<MusicCue>& out);

struct MusicConfig {
    double minGap = 30.0;    // silence after a cue ends before an equal or lower priority event may start one
    double preemptGap = 0.5; // silence between a cut cue and the one that cut it, and for outranking the last cue
};

enum class MusicVerdict : std::uint8_t {
    Accepted, // a cue is scheduled
    NoCue,    // the data has no cue for this event
    Cooldown, // every cue for this event is cooling down
    Busy,     // a cue is playing or waiting that this event does not outrank
    Gap,      // inside the minimum gap after the last cue
};

struct MusicDecision {
    MusicVerdict verdict = MusicVerdict::NoCue;
    std::size_t cue = std::numeric_limits<std::size_t>::max();
    double startAt = 0.0;
};

struct MusicStart {
    std::size_t cue = 0;
    double duration = 0.0;
};

struct MusicUpdate {
    std::optional<MusicStart> start;
    bool cut = false;            // true once, when the playing cue must fade out early
    float cutFadeSeconds = 0.0f; // over this long; 0 ends it at once
};

// Event in, decision out. No audio, no clock: the caller passes the time in seconds, which must not go
// backwards. The cue choice draws from a seeded generator only when an event is accepted, so the same
// seed and the same events always pick the same cues.
class MusicDirector {
public:
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    MusicDirector(std::vector<MusicCue> cues, MusicConfig config, std::uint64_t seed);

    MusicDecision onEvent(MusicEvent event, double now);
    // Starts a scheduled cue that has come due and reports a cut that has just been decided.
    MusicUpdate update(double now);

    // The envelope of the playing cue at `now`, 0 when none is.
    float level(double now) const;
    bool playing(double now) const;

    const std::vector<MusicCue>& cues() const { return cues_; }

private:
    struct Playing {
        std::size_t cue = npos;
        double start = 0.0;
        double end = 0.0;
        double cutAt = 0.0;
        double cutFade = 0.0;
        bool cut = false;
    };
    struct Pending {
        std::size_t cue = npos;
        double startAt = 0.0;
        int priority = 0;
    };

    void advance(double now);

    std::vector<MusicCue> cues_;
    MusicConfig config_;
    Rng rng_;
    std::vector<double> lastStart_;
    std::optional<Playing> current_;
    std::optional<Pending> pending_;
    double lastEnd_ = -std::numeric_limits<double>::infinity();
    int lastPriority_ = -1;
    std::optional<float> cutToReport_;
};

}
