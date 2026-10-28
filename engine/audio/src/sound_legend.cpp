#include "ichor/audio/sound_legend.h"

#include "ichor/assets/bundle.h"
#include "ichor/audio/mixer.h"
#include "ichor/audio/music_stinger.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace ichor {

namespace {

using G = CueGroup;

constexpr SoundCue kCues[] = {
    {"combat.tell", G::Combat, "A_Tell", "tell", "A boot scuff: a fast enemy is shifting its weight and will strike in a moment", "enemy about to strike"},
    {"combat.windup_light", G::Combat, "A_WindupLight", "light wind-up", "A short bright rising whoosh: an enemy winds up a light attack, dodge or block now", "enemy winding up"},
    {"combat.windup_heavy", G::Combat, "A_WindupHeavy", "heavy wind-up", "A long low falling growl: an enemy winds up a heavy attack, get clear or dodge", "enemy heavy attack coming"},

    {"music.vista", G::Music, "", "vista", "A calm open chord: you are looking out over a wide view", "music: a wide view"},
    {"music.discovery", G::Music, "", "discovery", "A rising four-note figure: you found something new", "music: a discovery"},
    {"music.combat", G::Music, "", "combat", "A pulsing minor chord: a fight has started", "music: a fight has started"},
    {"music.death", G::Music, "", "death", "A sinking minor chord with a low drone: you died", "music: you died"},

    {"footstep.grass", G::Footstep, "A_Step_Grass", "grass", "Your footsteps on grass and soil", "footsteps on grass"},
    {"footstep.rock", G::Footstep, "A_Step_Rock", "rock", "Your footsteps on bare rock", "footsteps on rock"},
    {"footstep.road", G::Footstep, "A_Step_Road", "road", "Your footsteps on a road", "footsteps on a road"},
    {"footstep.snow", G::Footstep, "A_Step_Snow", "snow", "Your footsteps on snow", "footsteps in snow"},
    {"footstep.splash", G::Footstep, "A_Splash", "wading", "A splash: you are wading through water, louder when it is deeper", "wading splash"},

    {"weather.wind", G::Weather, "A_Wind", "wind", "Wind, stronger under cloud, and the bed of cold open country", "wind"},
    {"weather.rain", G::Weather, "A_Rain", "rain", "Rain falling around you", "rain"},

    {"biome.surf", G::Biome, "A_Bed_Surf", "surf", "Waves on a shore: the sea or a tidal ruin is near", "surf"},
    {"biome.lake", G::Biome, "A_Bed_Lake", "lake", "Water lapping: you are by a lake", "lake"},
    {"biome.taiga", G::Biome, "A_Bed_Taiga", "taiga", "Wind in pines: cold northern forest", "taiga"},
    {"biome.forest", G::Biome, "A_Bed_Forest", "forest", "Leaves and branches: woodland", "forest"},
    {"biome.grassland", G::Biome, "A_Bed_Grassland", "grassland", "Wind through grass: open plain", "grassland"},
    {"biome.marsh", G::Biome, "A_Bed_Marsh", "marsh", "Wet, low and buzzing: a marsh", "marsh"},
    {"biome.rainforest", G::Biome, "A_Bed_Rainforest", "rainforest", "Dense dripping green: a rainforest", "rainforest"},
    {"biome.savanna", G::Biome, "A_Bed_Savanna", "savanna", "Dry grass and distant calls: savanna", "savanna"},
    {"biome.blight", G::Biome, "A_Bed_Blight", "blight", "A sick, hollow drone: blighted ground", "blight"},
    {"biome.farmland", G::Biome, "A_Bed_Farmland", "farmland", "Quiet fields: farmed land, people are near", "farmland"},

    {"time.dawn_birds", G::TimeOfDay, "A_Dawn_Birds", "dawn birds", "Birdsong: it is dawn", "dawn birds"},
    {"time.day_insects", G::TimeOfDay, "A_Day_Insects", "day insects", "Insects humming: it is day", "day insects"},
    {"time.dusk_crickets", G::TimeOfDay, "A_Dusk_Crickets", "dusk crickets", "Crickets: it is dusk", "dusk crickets"},
    {"time.night_insects", G::TimeOfDay, "A_Night_Insects", "night insects", "Night insects: it is night", "night insects"},
    {"time.night_wolves", G::TimeOfDay, "A_Night_Wolves", "night wolves", "Distant wolves: it is night in the cold country", "night wolves"},

    {"settlement.chatter", G::Settlement, "A_Settlement_Chatter", "chatter", "Voices and a crowd: a settlement is ahead", "settlement chatter"},
    {"settlement.bell", G::Settlement, "A_Settlement_Bell", "bell", "A bell: a settlement is ahead", "settlement bell"},
    {"settlement.mill", G::Settlement, "A_Settlement_Mill", "mill", "A turning mill: a milling settlement is ahead", "settlement mill"},
    {"settlement.surf", G::Settlement, "A_Settlement_Surf", "harbour", "Surf and gulls: a harbour settlement is ahead", "settlement harbour"},
    {"settlement.machinery", G::Settlement, "A_Settlement_Machinery", "machinery", "Clanking machinery: an industrial settlement is ahead", "settlement machinery"},
};

constexpr const char* kAmbientSummary[kCueGroupCount] = {
    "", "", "Your steps", "Weather", "Country beds", "Joins a bed by the hour", "Settlement ahead",
};

constexpr std::uint32_t kPlaceholderRate = 44100;

SoundClip makeBlip(std::string name, double hz)
{
    constexpr double kNote = 0.07;
    constexpr double kGap = 0.03;
    const auto noteFrames = static_cast<std::size_t>(kPlaceholderRate * kNote);
    const auto gapFrames = static_cast<std::size_t>(kPlaceholderRate * kGap);
    SoundClip clip;
    clip.name = std::move(name);
    clip.sampleRate = kPlaceholderRate;
    for (int note = 0; note < 2; ++note) {
        const double pitch = note == 0 ? hz : hz * 1.5;
        for (std::size_t i = 0; i < noteFrames; ++i) {
            const double t = static_cast<double>(i) / kPlaceholderRate;
            const double edge = std::min({1.0, static_cast<double>(i) / 80.0, static_cast<double>(noteFrames - i) / 80.0});
            const double square = std::sin(2.0 * std::numbers::pi * pitch * t) >= 0.0 ? 1.0 : -1.0;
            clip.samples.push_back(static_cast<std::int16_t>(square * edge * 9000.0));
        }
        if (note == 0) clip.samples.insert(clip.samples.end(), gapFrames, 0);
    }
    return clip;
}

}

std::span<const SoundCue> soundCues()
{
    return kCues;
}

const SoundCue* findCue(std::string_view id)
{
    for (const SoundCue& cue : kCues)
        if (cue.id == id) return &cue;
    return nullptr;
}

const SoundCue* findCueByClip(std::string_view clip)
{
    if (clip.empty()) return nullptr;
    for (const SoundCue& cue : kCues)
        if (cue.clip == clip) return &cue;
    return nullptr;
}

const SoundCue* musicCueFor(MusicEvent event)
{
    switch (event) {
    case MusicEvent::Vista: return findCue("music.vista");
    case MusicEvent::Discovery: return findCue("music.discovery");
    case MusicEvent::Combat: return findCue("music.combat");
    case MusicEvent::Death: return findCue("music.death");
    }
    return nullptr;
}

bool announces(const SoundCue& cue)
{
    return cue.group == CueGroup::Combat || cue.group == CueGroup::Music;
}

std::vector<std::string> soundLegendLines()
{
    std::vector<std::string> lines{"SOUND LEGEND (F2 closes, F4 turns captions on or off)"};
    for (const SoundCue& cue : kCues)
        if (announces(cue)) lines.push_back(std::string(cue.name) + ": " + std::string(cue.legend));
    for (std::size_t g = 0; g < kCueGroupCount; ++g) {
        const auto group = static_cast<CueGroup>(g);
        std::string line;
        for (const SoundCue& cue : kCues) {
            if (cue.group != group || announces(cue)) continue;
            line += line.empty() ? "" : ", ";
            line += cue.name;
        }
        if (!line.empty()) lines.push_back(std::string(kAmbientSummary[g]) + ": " + line);
    }
    return lines;
}

std::string captionFor(const SoundCue& cue)
{
    return announces(cue) ? std::string(cue.caption) : std::string();
}

SoundHelp stepSoundHelp(SoundHelp state, bool legendKey, bool captionKey)
{
    if (legendKey && !state.legendHeld) state.legendOpen = !state.legendOpen;
    if (captionKey && !state.captionHeld) state.captionsOn = !state.captionsOn;
    state.legendHeld = legendKey;
    state.captionHeld = captionKey;
    return state;
}

void CaptionFeed::push(const SoundCue& cue, std::uint64_t tick)
{
    if (!announces(cue)) return;
    std::erase_if(entries_, [&](const Entry& e) { return e.cue == &cue; });
    entries_.push_back({&cue, tick});
}

void CaptionFeed::prune(std::uint64_t tick)
{
    std::erase_if(entries_, [&](const Entry& e) { return e.tick > tick || tick - e.tick >= kCaptionTicks; });
}

std::vector<std::string> CaptionFeed::visible(std::uint64_t tick) const
{
    std::vector<std::string> out;
    for (const Entry& e : entries_)
        if (tick >= e.tick && tick - e.tick < kCaptionTicks) out.push_back(captionFor(*e.cue));
    if (out.size() > kMaxCaptions) out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(kMaxCaptions));
    return out;
}

std::string placeholderClipName(const SoundCue& cue)
{
    return "A_Missing_" + std::string(cue.id);
}

void addMissingCombatPlaceholders(AudioHost& host)
{
    SceneBundle bundle;
    double hz = 660.0;
    for (const SoundCue& cue : kCues) {
        if (cue.group != CueGroup::Combat) continue;
        if (host.findSound(cue.clip) == Mixer::kNoSound) bundle.sounds.push_back(makeBlip(placeholderClipName(cue), hz));
        hz += 220.0;
    }
    if (!bundle.sounds.empty()) host.addBundle(bundle);
}

std::uint32_t resolveCueSound(const AudioHost& host, std::string_view id)
{
    const SoundCue* cue = findCue(id);
    if (cue == nullptr || cue->clip.empty()) return Mixer::kNoSound;
    const std::uint32_t own = host.findSound(cue->clip);
    if (own != Mixer::kNoSound || cue->group != CueGroup::Combat) return own;
    return host.findSound(placeholderClipName(*cue));
}

std::vector<CueClipStatus> auditCueClips(const AudioHost& host, std::span<const MusicCue> music)
{
    std::vector<CueClipStatus> out;
    for (const SoundCue& cue : kCues) {
        if (!cue.clip.empty()) {
            out.push_back({&cue, std::string(cue.clip), host.findSound(cue.clip) != Mixer::kNoSound});
            continue;
        }
        for (const MusicCue& m : music) {
            if (musicCueFor(m.event) != &cue) continue;
            const std::string clip = musicClipName(m);
            out.push_back({&cue, clip, host.findSound(clip) != Mixer::kNoSound});
        }
    }
    return out;
}

void AnnouncingAudio::bind(std::span<const MusicCue> music)
{
    known_.clear();
    for (const SoundCue& cue : kCues) {
        if (cue.clip.empty()) continue;
        const std::uint32_t own = inner_.findSound(cue.clip);
        if (own != Mixer::kNoSound) known_.push_back({own, &cue, false});
        if (cue.group == CueGroup::Combat) {
            const std::uint32_t blip = inner_.findSound(placeholderClipName(cue));
            if (blip != Mixer::kNoSound) known_.push_back({blip, &cue, false});
        }
    }
    for (const MusicCue& m : music) {
        const std::uint32_t sound = inner_.findSound(musicClipName(m));
        if (sound != Mixer::kNoSound) known_.push_back({sound, musicCueFor(m.event), false});
    }
}

std::vector<AnnouncingAudio::Fired> AnnouncingAudio::drain()
{
    return std::exchange(fired_, {});
}

AnnouncingAudio::Known* AnnouncingAudio::find(std::uint32_t sound)
{
    for (Known& k : known_)
        if (k.sound == sound) return &k;
    return nullptr;
}

void AnnouncingAudio::play(std::uint32_t sound, glm::dvec3 position, bool spatial, float gain, SoundCategory category)
{
    if (const Known* k = find(sound); k != nullptr && k->cue != nullptr) fired_.push_back({k->cue, false});
    inner_.play(sound, position, spatial, gain, category);
}

void AnnouncingAudio::setAmbience(std::uint32_t sound, float gain)
{
    if (Known* k = find(sound); k != nullptr && k->cue != nullptr) {
        const bool audible = gain > 0.02f;
        if (audible && !k->audible) fired_.push_back({k->cue, true});
        k->audible = audible;
    }
    inner_.setAmbience(sound, gain);
}

}
