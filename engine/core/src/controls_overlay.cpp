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
        const KeyBindings& b = state.bindings;
        const auto key = [&](BindAction action) { return keyName(b.key(action)); };
        return {
            "CONTROLS (F1 closes)",
            std::format("{:<15}move ({} sprints)", moveKeysText(b), key(BindAction::Sprint)),
            "Right mouse    hold and move the mouse to look (or Left/Right arrows)",
            std::format("{:<15}attack", attackKeysText(b)),
            std::format("{:<15}heavy attack", std::format("{}, Ctrl+click", key(BindAction::Heavy))),
            std::format("{:<15}block (hold)", blockKeysText(b)),
            std::format("{:<15}dodge", dodgeKeysText(b)),
            std::format("{:<15}lock on", key(BindAction::LockOn)),
            std::format("{:<15}interact: pick up, or talk to someone", key(BindAction::Interact)),
            std::format("{:<15}interact / confirm (also picks up)", key(BindAction::Confirm)),
            std::format("{:<15}cancel", key(BindAction::Cancel)),
            std::format("{:<15}inventory", key(BindAction::Inventory)),
            std::format("{:<15}map", key(BindAction::Map)),
            std::format("{:<15}journal", key(BindAction::Journal)),
            std::format("{:<15}notes", key(BindAction::Notes)),
            std::format("{:<15}character", key(BindAction::Character)),
            "F2             sound legend: what each sound means",
            "F3             debug overlay: compact, full, off",
            "F4             sound captions on or off (on by default)",
            std::format("{:<15}quick save / quick load", std::format("{} / {}", key(BindAction::QuickSave), key(BindAction::QuickLoad))),
            "F8             feedback note: type a line, Enter saves it with a screenshot",
            "Esc            menu (Controls changes the keys)",
            std::format("Build {}", versionStamp()),
        };
    }
    if (controlsHintVisible(state)) {
        return {"Press F1 for the controls"};
    }
    return {};
}

}
