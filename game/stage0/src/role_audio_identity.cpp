#include "ichor/stage0/role_audio_identity.h"

#include <array>
#include <cmath>

namespace ichor::stage0 {

namespace {

constexpr std::array<RoleAudioIdentity, 6> kTable{{
    {"anchor", 55.0f, 110.0f, 0x0001, Timbre::Sub, TelegraphCue::GroundPlant},
    {"controller", 140.0f, 220.0f, 0x1111, Timbre::Chime, TelegraphCue::ChantRise},
    {"artillery", 260.0f, 420.0f, 0x0101, Timbre::Reed, TelegraphCue::BowDraw},
    {"ambusher", 480.0f, 700.0f, 0x0007, Timbre::Rasp, TelegraphCue::StalkHush},
    {"skirmisher", 800.0f, 1200.0f, 0x0005, Timbre::Click, TelegraphCue::LungeScrape},
    {"swarm", 1400.0f, 2200.0f, 0x5555, Timbre::Buzz, TelegraphCue::SkitterBurst},
}};

bool goodPitch(float v)
{
    return std::isfinite(v) && v > 0.0f;
}

std::uint16_t rotl(std::uint16_t v, int n)
{
    n &= kRhythmSteps - 1;
    const unsigned w = v;
    return static_cast<std::uint16_t>((w << n) | (w >> ((kRhythmSteps - n) & (kRhythmSteps - 1))));
}

} // namespace

const char* name(RoleAudioIssue issue)
{
    switch (issue) {
    case RoleAudioIssue::EmptyRole: return "EmptyRole";
    case RoleAudioIssue::DuplicateRole: return "DuplicateRole";
    case RoleAudioIssue::BadPitchBand: return "BadPitchBand";
    case RoleAudioIssue::EmptyRhythm: return "EmptyRhythm";
    case RoleAudioIssue::PitchOverlap: return "PitchOverlap";
    case RoleAudioIssue::RhythmCollision: return "RhythmCollision";
    case RoleAudioIssue::TimbreCollision: return "TimbreCollision";
    case RoleAudioIssue::CueCollision: return "CueCollision";
    }
    return "Unknown";
}

std::span<const RoleAudioIdentity> roleAudioTable()
{
    return kTable;
}

const RoleAudioIdentity* findRoleAudio(std::span<const RoleAudioIdentity> table, std::string_view role)
{
    if (role.empty()) return nullptr;
    for (const RoleAudioIdentity& e : table) {
        if (e.role == role) return &e;
    }
    return nullptr;
}

const RoleAudioIdentity* findRoleAudio(std::string_view role)
{
    return findRoleAudio(roleAudioTable(), role);
}

bool rhythmsEquivalent(std::uint16_t a, std::uint16_t b)
{
    for (int n = 0; n < kRhythmSteps; ++n) {
        if (rotl(a, n) == b) return true;
    }
    return false;
}

std::vector<RoleAudioProblem> validateRoleAudio(std::span<const RoleAudioIdentity> table)
{
    std::vector<RoleAudioProblem> out;
    auto add = [&](RoleAudioIssue issue, const RoleAudioIdentity& a, const RoleAudioIdentity* b) {
        out.push_back({issue, std::string(a.role), b ? std::string(b->role) : std::string()});
    };

    std::vector<bool> usable(table.size(), true);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const RoleAudioIdentity& e = table[i];
        if (e.role.empty()) {
            add(RoleAudioIssue::EmptyRole, e, nullptr);
            usable[i] = false;
        }
        if (!goodPitch(e.pitchLowHz) || !goodPitch(e.pitchHighHz) || e.pitchLowHz >= e.pitchHighHz) {
            add(RoleAudioIssue::BadPitchBand, e, nullptr);
            usable[i] = false;
        }
        if (e.rhythm == 0) {
            add(RoleAudioIssue::EmptyRhythm, e, nullptr);
            usable[i] = false;
        }
    }

    for (std::size_t i = 0; i < table.size(); ++i) {
        for (std::size_t j = i + 1; j < table.size(); ++j) {
            const RoleAudioIdentity& a = table[i];
            const RoleAudioIdentity& b = table[j];
            if (!a.role.empty() && a.role == b.role) {
                add(RoleAudioIssue::DuplicateRole, a, &b);
                continue;
            }
            if (!usable[i] || !usable[j]) continue;
            if (a.pitchLowHz <= b.pitchHighHz && b.pitchLowHz <= a.pitchHighHz) add(RoleAudioIssue::PitchOverlap, a, &b);
            if (rhythmsEquivalent(a.rhythm, b.rhythm)) add(RoleAudioIssue::RhythmCollision, a, &b);
            if (a.timbre == b.timbre) add(RoleAudioIssue::TimbreCollision, a, &b);
            if (a.cue == b.cue) add(RoleAudioIssue::CueCollision, a, &b);
        }
    }
    return out;
}

} // namespace ichor::stage0
