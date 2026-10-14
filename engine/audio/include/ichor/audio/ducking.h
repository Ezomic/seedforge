#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ichor {

// What a one-shot voice counts as in the loudness budget. None is every voice that predates the
// categories: it neither ducks the ambience nor has its gain capped.
enum class SoundCategory : std::uint8_t {
    None,
    OneShot,
    Music,
    Voice,
};

constexpr std::size_t kSoundCategoryCount = 4;

// The loudest gain a voice of the category may play at. The ambience bed is held to 1 - ceiling
// while the category plays, so a ducked bed plus one capped voice stays within full scale once the
// duck has settled. During the attack (kDuckAttackSeconds) the cue plays at once, to keep its
// transient, and may briefly overlap the bed; the final clamp absorbs that. Several categorised
// voices at once also rely on the clamp.
constexpr float categoryCeiling(SoundCategory category)
{
    switch (category) {
    case SoundCategory::OneShot: return 0.6f;
    case SoundCategory::Music: return 0.5f;
    case SoundCategory::Voice: return 0.7f;
    case SoundCategory::None: break;
    }
    return 1.0f;
}

// Seconds. The ambience drops quickly under a louder sound and recovers slowly after it.
constexpr float kDuckAttackSeconds = 0.02f;
constexpr float kDuckReleaseSeconds = 0.4f;

// The gain a voice really plays at: its own gain (0..1) capped by the category ceiling.
// SoundCategory::None passes through. NaN counts as silence.
float voiceGain(SoundCategory category, float gain);

using CategoryActivity = std::array<bool, kSoundCategoryCount>;

// The ambience gain the active categories ask for: the lowest of 1 - categoryCeiling over those
// playing, 1 when none is.
float duckTarget(const CategoryActivity& active);

// One step of the duck gain toward the target over dt seconds, as an exponential with the attack
// time when it falls and the release time when it rises. Never overshoots the target and never
// moves away from it. The target is clamped to 0..1; a NaN target or current gain counts as 1
// (no ducking) and a NaN or non-positive dt leaves the gain where it is. It lands on the target
// exactly once it is within a hair of it.
float nextDuckGain(float current, float target, float dt);

}
