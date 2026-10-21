#include "ichor/audio/soundscape.h"

#include "ichor/audio/ambience_layers.h"
#include "ichor/audio/ambience_tod.h"
#include "ichor/data/data_set.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ichor {

namespace {

constexpr float clamp01(float v)
{
    if (!(v > 0.0f)) {
        return 0.0f;
    }
    return v > 1.0f ? 1.0f : v;
}

struct NamedSurface {
    std::string_view name;
    SoundSurface surface;
};

constexpr NamedSurface kSurfaceNames[] = {
    {"grass", SoundSurface::Grass},
    {"rock", SoundSurface::Rock},
    {"road", SoundSurface::Road},
    {"water", SoundSurface::Water},
    {"snow", SoundSurface::Snow},
};

struct NamedSettlementSound {
    std::string_view name;
    SettlementSound sound;
};

constexpr NamedSettlementSound kSettlementNames[] = {
    {"chatter", SettlementSound::Chatter},
    {"bell", SettlementSound::Bell},
    {"mill", SettlementSound::Mill},
    {"surf", SettlementSound::Surf},
    {"machinery", SettlementSound::Machinery},
};

bool numberField(const DataRecord& record, const char* key, float& out)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end() || !std::holds_alternative<double>(it->second)) {
        return false;
    }
    const double value = std::get<double>(it->second);
    if (!(value >= 0.0 && value <= 1.0)) {
        return false;
    }
    out = static_cast<float>(value);
    return true;
}

bool stringField(const DataRecord& record, const char* key, std::string& out, bool required)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end()) {
        return !required;
    }
    if (!std::holds_alternative<std::string>(it->second)) {
        return false;
    }
    out = std::get<std::string>(it->second);
    return true;
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
}

BedGain* findBed(std::vector<BedGain>& beds, std::string_view clip)
{
    const auto it = std::lower_bound(beds.begin(), beds.end(), clip, [](const BedGain& bed, std::string_view name) { return bed.clip < name; });
    return it != beds.end() && it->clip == clip ? &*it : nullptr;
}

void addBed(std::vector<BedGain>& beds, std::string_view clip, float gain)
{
    if (clip.empty() || !(gain > 0.0f)) {
        return;
    }
    if (BedGain* existing = findBed(beds, clip)) {
        existing->gain += gain;
        return;
    }
    const auto at = std::lower_bound(beds.begin(), beds.end(), clip, [](const BedGain& bed, std::string_view name) { return bed.clip < name; });
    beds.insert(at, BedGain{std::string(clip), gain});
}

void clampBeds(std::vector<BedGain>& beds)
{
    for (BedGain& bed : beds) {
        bed.gain = clamp01(bed.gain);
    }
}

}

SoundSurface soundSurface(float grass, float rock, float snow, float roadCoverage, float waterDepth)
{
    if (waterDepth > kWaterSurfaceDepth) {
        return SoundSurface::Water;
    }
    if (roadCoverage >= kRoadSurfaceCoverage) {
        return SoundSurface::Road;
    }
    const float g = clamp01(grass);
    const float r = clamp01(rock);
    const float s = clamp01(snow);
    if (s > g && s > r) {
        return SoundSurface::Snow;
    }
    return r > g ? SoundSurface::Rock : SoundSurface::Grass;
}

bool Soundscape::load(const DataSet& data, Soundscape& out, std::string& error)
{
    Soundscape loaded;
    if (const DataTable* table = data.table("soundscape")) {
        for (const auto& [id, record] : table->records) {
            std::string kind;
            if (!stringField(record, "kind", kind, true)) {
                error = "soundscape " + id + ": kind is missing";
                return false;
            }
            if (kind == "biome") {
                BiomeSounds sounds;
                if (!startsWith(id, "biome_") || id.size() == 6 || !numberField(record, "bed_gain", sounds.bedGain) || !stringField(record, "bed", sounds.bed, true)
                    || !stringField(record, "dawn", sounds.dawn, false) || !stringField(record, "day", sounds.day, false) || !stringField(record, "dusk", sounds.dusk, false)
                    || !stringField(record, "night", sounds.night, false)) {
                    error = "soundscape " + id + ": a biome record needs the id biome_<name>, bed, a bed_gain in 0..1 and string slots";
                    return false;
                }
                loaded.biomes_[id.substr(6)] = std::move(sounds);
            } else if (kind == "step") {
                StepSound sound;
                const auto named = std::find_if(std::begin(kSurfaceNames), std::end(kSurfaceNames), [&](const NamedSurface& n) { return id == "step_" + std::string(n.name); });
                if (named == std::end(kSurfaceNames) || !stringField(record, "clip", sound.clip, true) || !numberField(record, "gain", sound.gain)) {
                    error = "soundscape " + id + ": a step record needs the id step_<surface>, a clip and a gain in 0..1";
                    return false;
                }
                loaded.steps_[named->surface] = std::move(sound);
            } else if (kind == "settlement") {
                SettlementClip clip;
                const auto named = std::find_if(std::begin(kSettlementNames), std::end(kSettlementNames), [&](const NamedSettlementSound& n) { return id == "settlement_" + std::string(n.name); });
                if (named == std::end(kSettlementNames) || !stringField(record, "clip", clip.clip, true) || !numberField(record, "gain", clip.gain)) {
                    error = "soundscape " + id + ": a settlement record needs the id settlement_<sound>, a clip and a gain in 0..1";
                    return false;
                }
                loaded.settlements_[named->sound] = std::move(clip);
            } else {
                error = "soundscape " + id + ": unknown kind " + kind;
                return false;
            }
        }
    }
    out = std::move(loaded);
    return true;
}

const BiomeSounds* Soundscape::biome(std::string_view name) const
{
    const auto it = biomes_.find(name);
    return it == biomes_.end() ? nullptr : &it->second;
}

const StepSound* Soundscape::step(SoundSurface surface) const
{
    const auto it = steps_.find(surface);
    return it == steps_.end() ? nullptr : &it->second;
}

const SettlementClip* Soundscape::settlement(SettlementSound sound) const
{
    const auto it = settlements_.find(sound);
    return it == settlements_.end() ? nullptr : &it->second;
}

std::vector<std::string> Soundscape::clipNames() const
{
    std::set<std::string> names;
    const auto add = [&](const std::string& clip) {
        if (!clip.empty()) {
            names.insert(clip);
        }
    };
    for (const auto& [name, sounds] : biomes_) {
        add(sounds.bed);
        add(sounds.dawn);
        add(sounds.day);
        add(sounds.dusk);
        add(sounds.night);
    }
    for (const auto& [surface, sound] : steps_) {
        add(sound.clip);
    }
    for (const auto& [kind, clip] : settlements_) {
        add(clip.clip);
    }
    return {names.begin(), names.end()};
}

std::vector<BedGain> biomeBeds(const Soundscape& soundscape, std::string_view biome, float hour)
{
    std::vector<BedGain> beds;
    const BiomeSounds* sounds = soundscape.biome(biome);
    if (sounds == nullptr) {
        return beds;
    }
    const AmbienceTimeOfDay tod = ambienceTimeOfDay(hour);
    addBed(beds, sounds->bed, clamp01(sounds->bedGain));
    addBed(beds, sounds->dawn, tod.dawnBirds);
    addBed(beds, sounds->day, tod.dayInsects);
    addBed(beds, sounds->dusk, tod.dusk);
    addBed(beds, sounds->night, tod.night);
    clampBeds(beds);
    return beds;
}

std::vector<BedGain> blendedBeds(const Soundscape& soundscape, std::string_view primary, std::string_view secondary, float weight, float hour)
{
    const float w = clamp01(weight);
    std::vector<BedGain> beds;
    for (const BedGain& bed : biomeBeds(soundscape, primary, hour)) {
        addBed(beds, bed.clip, bed.gain * (1.0f - w));
    }
    for (const BedGain& bed : biomeBeds(soundscape, secondary, hour)) {
        addBed(beds, bed.clip, bed.gain * w);
    }
    clampBeds(beds);
    return beds;
}

std::optional<StepSound> footstepFor(const Soundscape& soundscape, SoundSurface surface)
{
    const StepSound* sound = soundscape.step(surface);
    if (sound == nullptr || sound->clip.empty() || !(sound->gain > 0.0f)) {
        return std::nullopt;
    }
    return *sound;
}

SettlementSoundType settlementTypeForBiome(std::string_view biome)
{
    if (biome == "tidal_ruin" || biome == "ocean" || biome == "lake") {
        return SettlementSoundType::Coastal;
    }
    if (biome == "crag" || biome == "alpine") {
        return SettlementSoundType::Mining;
    }
    if (biome == "lowland_farm" || biome == "grassland") {
        return SettlementSoundType::Milling;
    }
    return SettlementSoundType::Inland;
}

std::optional<BedGain> settlementBed(const Soundscape& soundscape, std::string_view id, float size, SettlementSoundType type, float distance, float windSpeed, float windAlongPath)
{
    const SettlementSignature signature = settlementSignature(id, size, type);
    const SettlementClip* clip = soundscape.settlement(signature.dominant);
    if (clip == nullptr || clip->clip.empty()) {
        return std::nullopt;
    }
    const float gain = clamp01(clip->gain) * settlementGainAt(distance, settlementAudibleRange(size, windSpeed, windAlongPath));
    if (!(gain > 0.0f)) {
        return std::nullopt;
    }
    return BedGain{clip->clip, gain};
}

void addBeds(std::vector<BedGain>& beds, const std::vector<BedGain>& extra)
{
    for (const BedGain& bed : extra) {
        addBed(beds, bed.clip, bed.gain);
    }
}

void budgetBeds(std::vector<BedGain>& beds)
{
    float total = 0.0f;
    for (BedGain& bed : beds) {
        bed.gain = clamp01(bed.gain);
        total += bed.gain;
    }
    if (total > kAmbienceBudget) {
        const float scale = kAmbienceBudget / total;
        for (BedGain& bed : beds) {
            bed.gain *= scale;
        }
    }
}

std::vector<BedGain> BedMixer::step(const std::vector<BedGain>& targets, double dt)
{
    std::vector<BedGain> changed;
    if (!(dt > 0.0) || !std::isfinite(dt)) {
        return changed;
    }
    const float reach = static_cast<float>(std::min(dt, 1.0 / static_cast<double>(kFadePerSecond))) * kFadePerSecond;
    for (const BedGain& target : targets) {
        levels_.try_emplace(target.clip, 0.0f);
    }
    for (auto& [clip, level] : levels_) {
        float goal = 0.0f;
        const auto it = std::lower_bound(targets.begin(), targets.end(), std::string_view(clip), [](const BedGain& bed, std::string_view name) { return bed.clip < name; });
        if (it != targets.end() && it->clip == clip) {
            goal = clamp01(it->gain);
        }
        const float before = level;
        if (goal > level) {
            level = std::min(goal, level + reach);
        } else {
            level = std::max(goal, level - reach);
        }
        if (level != before) {
            changed.push_back({clip, level});
        }
    }
    return changed;
}

float BedMixer::level(std::string_view clip) const
{
    const auto it = levels_.find(clip);
    return it == levels_.end() ? 0.0f : it->second;
}

bool FootstepPacer::step(double x, double z)
{
    if (!std::isfinite(x) || !std::isfinite(z)) {
        placed_ = false;
        sinceStep_ = 0.0;
        return false;
    }
    const double moved = placed_ ? std::hypot(x - x_, z - z_) : 0.0;
    placed_ = true;
    x_ = x;
    z_ = z;
    if (moved > kMaxStepMetres) {
        sinceStep_ = 0.0;
        return false;
    }
    sinceStep_ += moved;
    if (sinceStep_ < kStrideMetres) {
        return false;
    }
    sinceStep_ = std::fmod(sinceStep_, kStrideMetres);
    return true;
}

}
