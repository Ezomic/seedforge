#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ichor {

constexpr double kControlsHintSeconds = 12.0;

// The keys the card lists for the actions the first-minute prompts teach, so the two never disagree.
constexpr std::string_view kMoveKeys = "W A S D";
constexpr std::string_view kAttackKeys = "J, Left click";
constexpr std::string_view kBlockKeys = "L, Middle btn";
constexpr std::string_view kDodgeKeys = "Space";

// The F1 controls card and the first-run hint line. Presentation only: it never reaches the simulation or a hash.
struct ControlsOverlay {
    bool open = false;
    bool held = false;            // F1 was down last frame, so a held key toggles once
    double hintSecondsLeft = 0.0; // the "press F1" line shows while this is above zero
};

// firstRun starts the hint timer; openAtStart shows the card from the first frame (a screenshot asks for it).
ControlsOverlay startControlsOverlay(bool firstRun, bool openAtStart);

// One frame: a press of the key flips the card, opening it ends the hint, and the hint runs down by dt.
// A negative, NaN or infinite dt counts as zero.
ControlsOverlay stepControlsOverlay(ControlsOverlay state, bool keyDown, double dt);

bool controlsHintVisible(const ControlsOverlay& state);

// The text shown for the state: the card when open, else the hint line while it lasts, else nothing.
std::vector<std::string> controlsOverlayLines(const ControlsOverlay& state);

}
