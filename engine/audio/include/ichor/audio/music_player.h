#pragma once

#include "ichor/audio/audio_host.h"
#include "ichor/audio/music_director.h"
#include "ichor/audio/music_stinger.h"

#include <cstdint>
#include <vector>

namespace ichor {

// The thin wiring between the director and the audio host: game events go in, the chosen stinger plays
// as a Music voice, and a cut fades the voice that is playing. Not thread safe; the game loop owns it.
class MusicPlayer {
public:
    MusicPlayer(std::vector<MusicCue> cues, MusicConfig config, std::uint64_t seed);

    // One stinger per cue, to be added to the host as a bundle before bind().
    SceneBundle makeBundle() const;
    // Looks the stingers up in the host by name; a cue whose clip is missing stays silent.
    void bind(const AudioHost& host);

    MusicDecision trigger(MusicEvent event, double now) { return director_.onEvent(event, now); }
    void update(double now, AudioHost& host);

    const MusicDirector& director() const { return director_; }

private:
    MusicDirector director_;
    std::vector<std::uint32_t> sounds_;
};

}
