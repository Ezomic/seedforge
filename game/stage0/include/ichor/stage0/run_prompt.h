#pragma once

#include "ichor/stage0/death_cause.h"

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

// The lines drawn under the combat overlay: the controls while a fight is on, and
// a death or win message with the retry instruction once it is over.
std::vector<std::string> promptLines(const PromptInput& input);

}
