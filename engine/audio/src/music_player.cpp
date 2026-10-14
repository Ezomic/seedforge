#include "ichor/audio/music_player.h"

#include "ichor/audio/mixer.h"

namespace ichor {

MusicPlayer::MusicPlayer(std::vector<MusicCue> cues, MusicConfig config, std::uint64_t seed)
    : director_(std::move(cues), config, seed)
{
}

SceneBundle MusicPlayer::makeBundle() const
{
    SceneBundle bundle;
    for (const MusicCue& cue : director_.cues()) {
        bundle.sounds.push_back(makeMusicClip(cue));
    }
    return bundle;
}

void MusicPlayer::bind(const AudioHost& host)
{
    sounds_.clear();
    for (const MusicCue& cue : director_.cues()) {
        sounds_.push_back(host.findSound(musicClipName(cue)));
    }
}

void MusicPlayer::update(double now, AudioHost& host)
{
    const MusicUpdate update = director_.update(now);
    if (update.cut) {
        host.fadeOutMusic(update.cutFadeSeconds);
    }
    if (update.start && update.start->cue < sounds_.size() && sounds_[update.start->cue] != Mixer::kNoSound) {
        host.play(sounds_[update.start->cue], glm::dvec3(0.0), false, director_.cues()[update.start->cue].gain, SoundCategory::Music);
    }
}

}
