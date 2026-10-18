#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ichor::stage0 {

// Stamped by the state machine at the killing blow, never reconstructed later.
// The distribution is the diagnosis (doc 18, stage0-log.md section 4).
enum class DeathCause : std::uint8_t { Unknown, OutOfStamina, WhiffPunished, Cornered, MistimedBlock, OffScreen, Unreadable, Bleed };

constexpr std::size_t kDeathCauseCount = 8;

// The CSV spelling: stamina, whiff_punished, cornered, mistimed_block, offscreen, unreadable, bleed, unknown.
const char* name(DeathCause cause);

// The swing that landed the killing blow; Unknown for a death with no swing behind it (bleed).
enum class KillingAttack : std::uint8_t { Unknown, Light, Heavy };

// The CSV spelling: light, heavy, unknown.
const char* name(KillingAttack attack);

// What the state machine knows at the killing blow.
struct DeathFacts {
    bool attackerOnScreen = true;
    bool victimRecovering = false; // the victim was in its own recovery, a whiff punished
    bool blocked = false;          // the blow landed on a raised guard
    bool outOfStamina = false;
    bool againstWall = false;
    KillingAttack attack = KillingAttack::Unknown;
    float staminaAtImpact = 0.0f;   // the victim's stamina before the blow's own drain
    float secondsSinceInput = 0.0f; // time since the player last pressed or steered anything
};

// Precedence is the diagnosis order: a telegraph that was never visible outranks everything,
// and a death with no other explanation is one the player could not read.
DeathCause classifyDeath(const DeathFacts& facts);

// Deaths per cause. Out-of-range causes are ignored rather than indexed.
struct DeathTally {
    std::array<int, kDeathCauseCount> counts{};

    void add(DeathCause cause);
    int count(DeathCause cause) const;
    int total() const;
    // Zero when the tally is empty.
    double share(DeathCause cause) const;
};

enum class Diagnosis : std::uint8_t {
    TooFewDeaths, // not enough data to say anything
    Telegraph,    // mostly unreadable or offscreen: fix wind-ups and audio, design not implicated
    Economy,      // mostly out of stamina: retune costs and regen
    Readable,     // mostly deaths with a visible cause: the design is what is being tested
    Mixed,
};

constexpr int kMinDeathsForDiagnosis = 10;

// The kill-criteria table of stage0-log.md section 5, applied to a distribution.
Diagnosis diagnose(const DeathTally& tally);
const char* name(Diagnosis diagnosis);

}
