#include "ichor/stage0/audio_telegraph.h"

#include <algorithm>

namespace ichor::stage0 {

namespace {

TelegraphCue cueOf(TelegraphRole role)
{
    const RoleAudioIdentity* id = findRoleAudio(name(role));
    return id != nullptr ? id->cue : TelegraphCue::GroundPlant;
}

bool roleKnown(TelegraphRole role)
{
    TelegraphRole parsed{};
    return parseTelegraphRole(name(role), parsed);
}

void add(std::vector<AttackCueProblem>& out, AttackCueIssue issue, TelegraphRole role, const std::string& attack)
{
    out.push_back({issue, role, attack});
}

} // namespace

const char* name(AttackCueIssue issue)
{
    switch (issue) {
    case AttackCueIssue::Missing: return "Missing";
    case AttackCueIssue::Duplicate: return "Duplicate";
    case AttackCueIssue::UnknownRole: return "UnknownRole";
    case AttackCueIssue::EmptyCueId: return "EmptyCueId";
    case AttackCueIssue::DuplicateCueId: return "DuplicateCueId";
    case AttackCueIssue::BadLead: return "BadLead";
    case AttackCueIssue::BadPitchStep: return "BadPitchStep";
    case AttackCueIssue::PitchStepClash: return "PitchStepClash";
    case AttackCueIssue::CueFamilyMismatch: return "CueFamilyMismatch";
    case AttackCueIssue::LeadMismatch: return "LeadMismatch";
    }
    return "Unknown";
}

// Lead equals the wind-up length in the role tunings (see visualTelegraphTable): the sound starts
// with the wind-up. Within a role the pitch step rises with the attack's weight.
const std::vector<AttackAudioCue>& attackAudioCues()
{
    using R = TelegraphRole;
    static const std::vector<AttackAudioCue> table{
        {R::Skirmisher, "tell", "skirmisher.tell", cueOf(R::Skirmisher), 9, 0},
        {R::Skirmisher, "light_windup", "skirmisher.light_windup", cueOf(R::Skirmisher), 21, 1},
        {R::Skirmisher, "heavy_windup", "skirmisher.heavy_windup", cueOf(R::Skirmisher), 42, 2},
        {R::Swarm, "lunge", "swarm.lunge", cueOf(R::Swarm), 30, 0},
        {R::Ambusher, "ambush", "ambusher.ambush", cueOf(R::Ambusher), 12, 0},
        {R::Ambusher, "strike", "ambusher.strike", cueOf(R::Ambusher), 30, 1},
        {R::Anchor, "slam", "anchor.slam", cueOf(R::Anchor), 60, 0},
        {R::Artillery, "shell", "artillery.shell", cueOf(R::Artillery), 50, 0},
        {R::Controller, "cast", "controller.cast", cueOf(R::Controller), 45, 0},
    };
    return table;
}

const AttackAudioCue* findAttackCue(std::span<const AttackAudioCue> table, TelegraphRole role, std::string_view attack)
{
    for (const AttackAudioCue& c : table)
        if (c.role == role && c.attack == attack) return &c;
    return nullptr;
}

std::vector<AttackCueProblem> validateAttackCues(std::span<const AttackAudioCue> table, std::span<const AudioTelegraph> attacks,
                                                 std::span<const RoleAudioIdentity> identities, std::span<const VisualTelegraph> visuals)
{
    std::vector<AttackCueProblem> out;
    for (const AudioTelegraph& a : attacks)
        if (findAttackCue(table, a.role, a.attack) == nullptr) add(out, AttackCueIssue::Missing, a.role, a.attack);

    for (std::size_t i = 0; i < table.size(); ++i) {
        const AttackAudioCue& c = table[i];
        if (!roleKnown(c.role)) {
            add(out, AttackCueIssue::UnknownRole, c.role, c.attack);
            continue;
        }
        if (c.cueId.empty()) add(out, AttackCueIssue::EmptyCueId, c.role, c.attack);
        if (c.leadTicks < kMinTelegraphLeadTicks) add(out, AttackCueIssue::BadLead, c.role, c.attack);
        if (c.pitchStep < 0 || c.pitchStep >= kPitchSteps) add(out, AttackCueIssue::BadPitchStep, c.role, c.attack);

        const RoleAudioIdentity* id = findRoleAudio(identities, name(c.role));
        if (id == nullptr || id->cue != c.cue) add(out, AttackCueIssue::CueFamilyMismatch, c.role, c.attack);

        for (const VisualTelegraph& v : visuals)
            if (v.role == c.role && v.attack == c.attack && v.durationTicks != c.leadTicks) {
                add(out, AttackCueIssue::LeadMismatch, c.role, c.attack);
                break;
            }

        for (std::size_t j = 0; j < i; ++j)
            if (table[j].role == c.role && table[j].attack == c.attack) {
                add(out, AttackCueIssue::Duplicate, c.role, c.attack);
                break;
            }
        for (std::size_t j = 0; j < i; ++j)
            if (!c.cueId.empty() && table[j].cueId == c.cueId) {
                add(out, AttackCueIssue::DuplicateCueId, c.role, c.attack);
                break;
            }
        for (std::size_t j = 0; j < i; ++j)
            if (table[j].role == c.role && table[j].attack != c.attack && table[j].pitchStep == c.pitchStep) {
                add(out, AttackCueIssue::PitchStepClash, c.role, c.attack);
                break;
            }
    }
    return out;
}

int cueStartOffset(const AttackAudioCue& cue, int windupTicks)
{
    if (windupTicks <= 0 || cue.leadTicks < 0) return 0;
    return std::max(0, windupTicks - cue.leadTicks);
}

} // namespace ichor::stage0
