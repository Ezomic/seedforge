#include "ichor/stage0/run_prompt.h"

#include <format>

namespace ichor::stage0 {

const char* plainCause(DeathCause cause)
{
    switch (cause) {
    case DeathCause::OutOfStamina: return "You ran out of stamina and could not guard or dodge.";
    case DeathCause::WhiffPunished: return "You swung and missed, and were hit while recovering.";
    case DeathCause::Cornered: return "You were backed against a wall with nowhere to go.";
    case DeathCause::MistimedBlock: return "You blocked too early or too late.";
    case DeathCause::OffScreen: return "The blow came from outside your view.";
    case DeathCause::Unreadable: return "You did not see that attack coming.";
    case DeathCause::Bleed: return "You bled out from an earlier wound.";
    default: return "Cause not recorded.";
    }
}

const char* controlsHint()
{
    return "Move WASD  Look mouse or arrows  E lock-on  J light  K heavy  L block  Space dodge  Enter retry  Esc quit";
}

const char* retryHint()
{
    return "Press Enter to try again.";
}

std::vector<std::string> deathCard(DeathCause cause)
{
    return {"YOU DIED", plainCause(cause), retryHint()};
}

InputFrame RetryGuard::filter(InputFrame input, bool playerDead, double now)
{
    const bool confirm = (input.buttons & InputFrame::Confirm) != 0;
    if (!playerDead) {
        diedAt_.reset();
        armed_ = false;
    } else {
        // An earlier clock than the death's is a load of an older save with the player dead in it too.
        if (!diedAt_ || now < *diedAt_) {
            diedAt_ = now;
        }
        if (!armed_ && !confirm && now - *diedAt_ >= kArmSeconds) {
            armed_ = true;
        }
    }
    // A key held back stays held back until it is let go, even past a respawn, so it never turns into a fresh press.
    if (!confirm) {
        heldBack_ = false;
    } else if (playerDead && !armed_) {
        heldBack_ = true;
    }
    if (heldBack_) {
        input.buttons &= ~static_cast<std::uint32_t>(InputFrame::Confirm);
    }
    return input;
}

std::vector<std::string> promptLines(const PromptInput& input)
{
    std::vector<std::string> lines;
    switch (input.outcome) {
    case FightOutcome::PlayerDied:
        lines.push_back(std::format("YOU DIED. {}", plainCause(input.cause)));
        lines.push_back(retryHint());
        break;
    case FightOutcome::FoeDied:
        lines.push_back("YOU WON.");
        lines.push_back("Press Enter for the next fight.");
        break;
    case FightOutcome::Ongoing:
        lines.push_back(controlsHint());
        break;
    }
    lines.push_back(std::format("Fights recorded: {}", input.fightsRecorded));
    return lines;
}

}
