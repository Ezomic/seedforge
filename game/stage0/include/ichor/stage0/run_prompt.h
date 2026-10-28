#pragma once

#include "ichor/core/input.h"
#include "ichor/stage0/death_cause.h"

#include <optional>
#include <string>
#include <vector>

namespace ichor::stage0 {

enum class FightOutcome : std::uint8_t { Ongoing, PlayerDied, FoeDied };

struct PromptInput {
    FightOutcome outcome = FightOutcome::Ongoing;
    DeathCause cause = DeathCause::Unknown;
    int fightsRecorded = 0;
};

// One plain sentence a first-time tester can act on, no jargon.
const char* plainCause(DeathCause cause);

const char* controlsHint();

const char* retryHint();

// The death screen where there is no game UI: a title, the cause as one plain line and the retry key.
std::vector<std::string> deathCard(DeathCause cause);

// Keeps a person's Enter from restarting the fight the instant they die. While the player is dead, Confirm is held back
// until the death has been shown for kArmSeconds and the key has been seen up, so only a fresh press after that retries.
// A key still down when the player comes back (the forged bridge's respawn) stays held back until it is let go.
// It filters the live input before the engine records it: the rule for what counts as a retry, the replays and the bots
// are untouched. `now` is the scene's clock.
class RetryGuard {
public:
    static constexpr double kArmSeconds = 0.5;

    InputFrame filter(InputFrame input, bool playerDead, double now);

private:
    std::optional<double> diedAt_;
    bool armed_ = false;
    bool heldBack_ = false;
};

// The lines drawn under the combat overlay: the controls while a fight is on, and
// a death or win message with the retry instruction once it is over.
std::vector<std::string> promptLines(const PromptInput& input);

}
