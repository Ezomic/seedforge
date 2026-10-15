#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/audio/music_director.h"

namespace ichor {

// The sound a cue is named by in the bundle.
std::string musicClipName(const MusicCue& cue);

// A placeholder stinger synthesised from the cue: a few sine partials on its root, shaped by the event
// (an open fifth for a vista, a rising figure for a discovery, a pulsing minor for combat, a sinking
// minor with a low drone for death) and by cueEnvelope, so the clip fades itself in and out and is
// exactly cue.duration long. The same cue always makes the same samples.
SoundClip makeMusicClip(const MusicCue& cue);

}
