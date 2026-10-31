#pragma once

#include "ichor/audio/settlement_audio.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

class DataSet;

// The soundscape of a forged world: which looping bed answers which biome, which footstep answers
// which surface, which sound a settlement makes. The selection is a pure function of the biome, the
// surface and the hour of the day, so it holds no sim state, never feeds the world hash and sounds
// the same on every platform (no wall clock). A bed whose clip does not exist is silence,
// and silence is a valid state: nothing here ever requires a clip to be present.

// What the player stands on, in the order a tie is broken.
enum class SoundSurface : std::uint8_t { Grass, Rock, Road, Water, Snow };

constexpr std::size_t kSoundSurfaceCount = 5;

// The step set a surface without one of its own falls back to.
constexpr SoundSurface kFallbackSoundSurface = SoundSurface::Grass;

constexpr float kWaterSurfaceDepth = 0.05f;
constexpr float kRoadSurfaceCoverage = 0.5f;

// The surface under the feet. Standing in water beats a road, a road beats the ground; the ground is
// the heaviest of the grass, rock and snow weights (ties go to grass, then rock). NaN counts as 0.
SoundSurface soundSurface(float grass, float rock, float snow, float roadCoverage, float waterDepth);

struct BedGain {
    std::string clip;
    float gain = 0.0f;

    friend bool operator==(const BedGain&, const BedGain&) = default;
};

// One biome's beds. A clip name that is empty means that slot is silent.
struct BiomeSounds {
    std::string bed;
    float bedGain = 0.0f;
    std::string dawn;
    std::string day;
    std::string dusk;
    std::string night;
};

struct StepSound {
    std::string clip;
    float gain = 0.0f;
};

struct SettlementClip {
    std::string clip;
    float gain = 0.0f;
};

// Everything one forged world may sound like, read from the "soundscape" data table.
class Soundscape {
public:
    // Reads the table: records with kind "biome" (id "biome_<name>"; bed, bed_gain, dawn, day, dusk,
    // night), "step" (id "step_<surface>"; clip, gain) and "settlement" (id "settlement_<sound>"; clip,
    // gain). All or nothing: on any bad record `out` is untouched and `error` says which. A missing
    // table loads an empty soundscape, which is silent.
    static bool load(const DataSet& data, Soundscape& out, std::string& error);

    const BiomeSounds* biome(std::string_view name) const;
    const StepSound* step(SoundSurface surface) const;
    const SettlementClip* settlement(SettlementSound sound) const;

    // Every clip name the soundscape mentions, sorted and unique.
    std::vector<std::string> clipNames() const;

    bool empty() const { return biomes_.empty() && steps_.empty() && settlements_.empty(); }

    void setBiome(std::string name, BiomeSounds sounds) { biomes_[std::move(name)] = std::move(sounds); }
    void setStep(SoundSurface surface, StepSound sound) { steps_[surface] = std::move(sound); }
    void setSettlement(SettlementSound sound, SettlementClip clip) { settlements_[sound] = std::move(clip); }

private:
    std::map<std::string, BiomeSounds, std::less<>> biomes_;
    std::map<SoundSurface, StepSound> steps_;
    std::map<SettlementSound, SettlementClip> settlements_;
};

// What a biome sounds like at an hour, sorted by clip, one entry per clip (gains of a clip named twice
// add), no silent entries. The bed holds its gain all day; the dawn, day, dusk and night slots follow
// ambienceTimeOfDay(hour). An unknown biome is silent. Every gain is in 0..1.
std::vector<BedGain> biomeBeds(const Soundscape& soundscape, std::string_view biome, float hour);

// Two biomes laid over each other by `weight` (0 is all `primary`, 1 all `secondary`; NaN counts as 0).
// Continuous in the weight and in the hour, so walking across a border or letting the day turn never
// steps a gain.
std::vector<BedGain> blendedBeds(const Soundscape& soundscape, std::string_view primary, std::string_view secondary, float weight, float hour);

// The footstep for a surface. Water is always silent (wading has its own splash). A surface with no
// usable step of its own (missing, or an unknown value) plays the kFallbackSoundSurface set instead;
// nullopt only when that set is missing too.
std::optional<StepSound> footstepFor(const Soundscape& soundscape, SoundSurface surface);

// The settlement type a biome gives its settlements; an unknown biome is Inland.
SettlementSoundType settlementTypeForBiome(std::string_view biome);

// The audible landmark of one settlement for a listener at `distance` metres: its dominant sound
// scaled by how near the listener is. nullopt when the sound has no clip or the listener is out of range.
std::optional<BedGain> settlementBed(const Soundscape& soundscape, std::string_view id, float size, SettlementSoundType type, float distance, float windSpeed, float windAlongPath);

// Adds `extra` into `beds` by clip name, keeping it sorted.
void addBeds(std::vector<BedGain>& beds, const std::vector<BedGain>& extra);

// Scales every gain by one factor when they add up to more than kAmbienceBudget, so the mix never
// clips; below the budget they pass through untouched.
void budgetBeds(std::vector<BedGain>& beds);

// The bed levels the host is told about. The gains ease toward their targets at a bounded rate, so a
// crossfade between two beds is continuous however fast the target jumps. Not thread safe; the game
// loop owns it.
class BedMixer {
public:
    // Gain units per second; a full fade takes 1 / rate seconds.
    static constexpr float kFadePerSecond = 0.25f;

    // Moves every bed toward its target (a bed missing from `targets` heads for 0) and returns the
    // beds whose level changed this step, in clip order. dt below 0 or NaN moves nothing.
    std::vector<BedGain> step(const std::vector<BedGain>& targets, double dt);

    float level(std::string_view clip) const;

private:
    std::map<std::string, float, std::less<>> levels_;
};

// Decides when the player's feet make a step. Fed the player's place once per fixed tick; it counts
// the ground covered, so the pace follows the speed and standing still is silent. It only listens:
// nothing here is part of the simulation's state or its hash.
class FootstepPacer {
public:
    static constexpr double kStrideMetres = 1.6;
    // No walker covers this in one tick, so a bigger step is a teleport or a load, not a stride.
    static constexpr double kMaxStepMetres = 5.0;

    // True when a footfall lands this tick.
    bool step(double x, double z);

private:
    bool placed_ = false;
    double x_ = 0.0;
    double z_ = 0.0;
    double sinceStep_ = 0.0;
};

}
