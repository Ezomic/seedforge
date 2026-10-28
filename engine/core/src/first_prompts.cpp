#include "ichor/core/first_prompts.h"

#include "ichor/core/controls_overlay.h"
#include "ichor/core/input.h"

#include <cmath>
#include <format>

namespace ichor {

namespace {

// "J, Left click" reads as "J or Left click" in a sentence.
std::string spoken(std::string_view keys)
{
    std::string text(keys);
    for (std::size_t at = text.find(", "); at != std::string::npos; at = text.find(", ", at)) {
        text.replace(at, 2, " or ");
    }
    return text;
}

std::uint32_t performed(const FirstPromptsFrame& frame)
{
    std::uint32_t done = 0;
    if (frame.moveX != 0.0f || frame.moveY != 0.0f) {
        done |= firstPromptBit(FirstPrompt::Move);
    }
    if ((frame.buttons & InputFrame::Attack) != 0) {
        done |= firstPromptBit(FirstPrompt::Attack);
    }
    if ((frame.buttons & InputFrame::Block) != 0) {
        done |= firstPromptBit(FirstPrompt::Block);
    }
    if ((frame.buttons & InputFrame::Dodge) != 0) {
        done |= firstPromptBit(FirstPrompt::Dodge);
    }
    return done;
}

std::uint32_t possible(const FirstPromptsFrame& frame)
{
    std::uint32_t open = 0;
    if (frame.canMove) {
        open |= firstPromptBit(FirstPrompt::Move);
    }
    if (frame.foeInRange) {
        open |= firstPromptBit(FirstPrompt::Attack) | firstPromptBit(FirstPrompt::Block) | firstPromptBit(FirstPrompt::Dodge);
    }
    return open;
}

}

std::string firstPromptText(FirstPrompt prompt)
{
    switch (prompt) {
    case FirstPrompt::Move:
        return std::format("{} to move", spoken(kMoveKeys));
    case FirstPrompt::Attack:
        return std::format("{} to attack", spoken(kAttackKeys));
    case FirstPrompt::Block:
        return std::format("Hold {} to block", spoken(kBlockKeys));
    case FirstPrompt::Dodge:
        return std::format("{} to dodge", spoken(kDodgeKeys));
    }
    return {};
}

FirstPrompts stepFirstPrompts(FirstPrompts state, const FirstPromptsFrame& frame, double dt)
{
    if (frame.held) {
        return state;
    }
    const std::uint32_t done = performed(frame);
    state.seen |= done;
    state.queued &= ~done;
    if (state.showing && (done & firstPromptBit(*state.showing)) != 0) {
        state.showing.reset();
    }
    state.queued |= possible(frame) & ~state.seen;
    if (state.showing) {
        if (std::isfinite(dt) && dt > 0.0) {
            state.secondsLeft -= dt;
        }
        if (state.secondsLeft <= 0.0) {
            state.showing.reset();
        }
    }
    if (!state.showing) {
        for (int i = 0; i < kFirstPromptCount; ++i) {
            const auto next = static_cast<FirstPrompt>(i);
            if ((state.queued & firstPromptBit(next)) != 0) {
                state.showing = next;
                state.secondsLeft = kFirstPromptSeconds;
                state.seen |= firstPromptBit(next);
                state.queued &= ~firstPromptBit(next);
                break;
            }
        }
    }
    if (!state.showing) {
        state.secondsLeft = 0.0;
    }
    return state;
}

std::optional<std::string> firstPromptLine(const FirstPrompts& state, bool held)
{
    if (held || !state.showing) {
        return std::nullopt;
    }
    return firstPromptText(*state.showing);
}

bool firstPromptsShown(const FirstPromptsRun& run)
{
    return !run.headless && !run.screenshot && !run.replay && !run.bot;
}

}
