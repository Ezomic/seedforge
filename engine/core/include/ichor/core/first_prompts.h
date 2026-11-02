#pragma once

#include "ichor/core/key_bindings.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ichor {

// The four actions a new player is taught, one prompt each, in the order they queue.
enum class FirstPrompt : std::uint8_t { Move, Attack, Block, Dodge };

constexpr int kFirstPromptCount = 4;
constexpr std::uint32_t kAllFirstPrompts = (1u << kFirstPromptCount) - 1u;
constexpr double kFirstPromptSeconds = 8.0;
// The most one frame runs a prompt down, so a stall (a dungeon bake, a dragged window) cannot end it unread.
constexpr double kFirstPromptMaxStep = 0.1;
// Attack, block and dodge become possible once a living foe is this close, in metres along the ground.
constexpr double kFirstPromptFoeRange = 12.0;

constexpr std::uint32_t firstPromptBit(FirstPrompt prompt)
{
    return 1u << static_cast<std::uint32_t>(prompt);
}

// "W A S D to move" and so on, from the same key text as the F1 card.
std::string firstPromptText(FirstPrompt prompt, const KeyBindings& bindings = {});

// What one frame offers the prompts. A held frame (a screen open, a pause, the player dead) neither runs a
// prompt's time nor starts one, and its input is not counted as the action.
struct FirstPromptsFrame {
    bool held = false;
    bool canMove = false;    // the player stands in a running world
    bool foeInRange = false; // a living foe within kFirstPromptFoeRange
    std::uint32_t buttons = 0; // InputFrame buttons
    float moveX = 0.0f;
    float moveY = 0.0f;
};

// Presentation only: it never reaches the simulation or a hash.
struct FirstPrompts {
    std::uint32_t seen = 0;   // the stored flags: a prompt with its bit set never shows again
    std::uint32_t queued = 0; // possible, not yet shown, waiting for the one on screen to end
    std::optional<FirstPrompt> showing;
    double secondsLeft = 0.0;
};

// One frame. Performing an action ends its prompt and sets its flag, even before the prompt showed. A prompt
// becomes eligible when its action does, queues behind the one on screen, and sets its flag when it appears; it
// ends after kFirstPromptSeconds. A negative, NaN or infinite dt counts as zero.
FirstPrompts stepFirstPrompts(FirstPrompts state, const FirstPromptsFrame& frame, double dt);

// The prompt's line while one is up and the frame is not held, else nothing.
std::optional<std::string> firstPromptLine(const FirstPrompts& state, bool held, const KeyBindings& bindings = {});

// Who gets prompts: a person playing at the window. A headless run, a screenshot, a replay or a bot never does,
// so neither the picture they produce nor the stored flags depend on the prompts.
struct FirstPromptsRun {
    bool headless = false;
    bool screenshot = false;
    bool replay = false;
    bool bot = false;
};
bool firstPromptsShown(const FirstPromptsRun& run);

}
