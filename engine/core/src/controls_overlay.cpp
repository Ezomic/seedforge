#include "ichor/core/controls_overlay.h"

#include "ichor/core/version.h"

#include <cmath>
#include <format>

namespace ichor {

ControlsOverlay startControlsOverlay(bool firstRun, bool openAtStart)
{
    ControlsOverlay state;
    state.open = openAtStart;
    state.hintSecondsLeft = firstRun && !openAtStart ? kControlsHintSeconds : 0.0;
    return state;
}

ControlsOverlay stepControlsOverlay(ControlsOverlay state, bool keyDown, double dt)
{
    if (keyDown && !state.held) {
        state.open = !state.open;
    }
    state.held = keyDown;
    if (state.open) {
        state.hintSecondsLeft = 0.0;
    } else if (std::isfinite(dt) && dt > 0.0) {
        state.hintSecondsLeft = state.hintSecondsLeft > dt ? state.hintSecondsLeft - dt : 0.0;
    }
    return state;
}

bool controlsHintVisible(const ControlsOverlay& state)
{
    return !state.open && state.hintSecondsLeft > 0.0;
}

std::vector<std::string> controlsOverlayLines(const ControlsOverlay& state)
{
    if (state.open) {
        return {
            "CONTROLS (F1 closes)",
            std::format("{:<15}move (Shift sprints)", kMoveKeys),
            "Right mouse    hold and move the mouse to look (or Left/Right arrows)",
            std::format("{:<15}attack", kAttackKeys),
            "K, Ctrl+click  heavy attack",
            std::format("{:<15}block (hold)", kBlockKeys),
            std::format("{:<15}dodge", kDodgeKeys),
            "E              lock on",
            "F              interact: pick up, or talk to someone",
            "Enter          interact / confirm (also picks up)",
            "I              inventory",
            "M              map",
            "Tab            journal",
            "N              notes",
            "C              character",
            "F2             sound legend: what each sound means",
            "F3             debug overlay: compact, full, off",
            "F4             sound captions on or off (on by default)",
            "F5 / F9        quick save / quick load",
            "Esc            menu",
            std::format("Build {}", versionStamp()),
        };
    }
    if (controlsHintVisible(state)) {
        return {"Press F1 for the controls"};
    }
    return {};
}

}
