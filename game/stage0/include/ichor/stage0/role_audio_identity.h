#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

// Audio identity per enemy role (doc 12, combat audio contract): what a player hears of a
// wind-up when fog or darkness hides the animation. Pure data plus a validator; nothing here
// plays a sound. A role collides with another when any one of its four traits is shared,
// because a single shared trait is enough to make two wind-ups hard to tell apart.

enum class Timbre : std::uint8_t { Sub, Reed, Chime, Click, Rasp, Buzz };
enum class TelegraphCue : std::uint8_t { GroundPlant, BowDraw, ChantRise, LungeScrape, SkitterBurst, StalkHush };

constexpr int kRhythmSteps = 16;

struct RoleAudioIdentity {
    std::string_view role;
    float pitchLowHz = 0.0f;
    float pitchHighHz = 0.0f;
    std::uint16_t rhythm = 0; // one bit per step of a 16 step bar, bit 0 first
    Timbre timbre = Timbre::Sub;
    TelegraphCue cue = TelegraphCue::GroundPlant;
};

enum class RoleAudioIssue : std::uint8_t {
    EmptyRole,
    DuplicateRole,
    BadPitchBand,
    EmptyRhythm,
    PitchOverlap,
    RhythmCollision,
    TimbreCollision,
    CueCollision,
};
const char* name(RoleAudioIssue issue);

struct RoleAudioProblem {
    RoleAudioIssue issue;
    std::string roleA;
    std::string roleB; // empty for single-role issues
};

// The six authored roles of doc 05, in a fixed order.
std::span<const RoleAudioIdentity> roleAudioTable();

// Null for an unknown or empty id; never throws.
const RoleAudioIdentity* findRoleAudio(std::string_view role);
const RoleAudioIdentity* findRoleAudio(std::span<const RoleAudioIdentity> table, std::string_view role);

// Empty result means the table is valid. Rejects NaN/inf/non-positive pitch before comparing.
std::vector<RoleAudioProblem> validateRoleAudio(std::span<const RoleAudioIdentity> table);

// True when two rhythm bars are the same pattern up to rotation.
bool rhythmsEquivalent(std::uint16_t a, std::uint16_t b);

} // namespace ichor::stage0
