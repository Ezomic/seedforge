#pragma once

#include "ichor/stage0/role_audio_identity.h"
#include "ichor/stage0/visual_telegraph.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::stage0 {

// Doc 12 combat audio contract: every attack wind-up has a distinct, identifiable sound. This maps
// each attack to the cue the audio layer should start (which sound, how long before impact, which
// pitch step inside the role's band). Pure data plus a validator; nothing here plays a sound and
// nothing in the sim reads it.

// Shortest lead a player can still react to: the skirmisher tell, in 60 Hz fixed steps.
constexpr int kMinTelegraphLeadTicks = 9;
constexpr int kPitchSteps = 3;

struct AttackAudioCue {
    TelegraphRole role{};
    std::string attack;
    std::string cueId; // unique per attack, "<role>.<attack>"
    TelegraphCue cue = TelegraphCue::GroundPlant; // the role's cue family
    int leadTicks = 0; // the cue starts this many fixed steps before the hit lands
    int pitchStep = 0; // 0..kPitchSteps-1 inside the role's pitch band; separates attacks of one role
};

enum class AttackCueIssue : std::uint8_t {
    Missing,           // an audio telegraph attack with no cue
    Duplicate,         // two cues for one (role, attack)
    UnknownRole,
    EmptyCueId,
    DuplicateCueId,
    BadLead,           // below kMinTelegraphLeadTicks
    BadPitchStep,
    PitchStepClash,    // two attacks of one role share a pitch step
    CueFamilyMismatch, // cue differs from the role's audio identity
    LeadMismatch,      // lead differs from the visual wind-up duration
};
const char* name(AttackCueIssue issue);

struct AttackCueProblem {
    AttackCueIssue issue;
    TelegraphRole role{};
    std::string attack;
};

const std::vector<AttackAudioCue>& attackAudioCues();

const AttackAudioCue* findAttackCue(std::span<const AttackAudioCue> table, TelegraphRole role, std::string_view attack);

// Empty result means every attack in `attacks` has a valid, distinct cue consistent with `identities`
// and the visual wind-up length in `visuals` (a cue with no visual entry, or an empty `visuals`, skips
// that one check; the visual table's own coverage is validateVisualTelegraphs). The cueId format is
// pinned by the shipped-table test, not the validator. Never throws on hostile tables.
std::vector<AttackCueProblem> validateAttackCues(std::span<const AttackAudioCue> table, std::span<const AudioTelegraph> attacks,
                                                 std::span<const RoleAudioIdentity> identities, std::span<const VisualTelegraph> visuals);

// Tick, relative to the wind-up start, at which the cue starts for a wind-up of `windupTicks`.
// Clamped to 0 when the cue's lead exceeds the wind-up; non-positive wind-ups yield 0.
int cueStartOffset(const AttackAudioCue& cue, int windupTicks);

} // namespace ichor::stage0
