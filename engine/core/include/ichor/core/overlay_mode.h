#pragma once

#include <optional>
#include <string_view>

namespace ichor {

// How much of the debug overlay is drawn. Presentation only: it never reaches the simulation or a hash.
enum class OverlayMode {
    Off,
    Compact, // fps and frame time
    Full,    // every line, with the stage timers
};

constexpr OverlayMode nextOverlayMode(OverlayMode mode)
{
    switch (mode) {
    case OverlayMode::Off: return OverlayMode::Compact;
    case OverlayMode::Compact: return OverlayMode::Full;
    case OverlayMode::Full: return OverlayMode::Off;
    }
    return OverlayMode::Compact;
}

// The F3 key: one step round Off, Compact, Full per press, not per frame it is held.
struct OverlayToggle {
    OverlayMode mode = OverlayMode::Compact;
    bool held = false;

    void update(bool keyDown)
    {
        if (keyDown && !held) {
            mode = nextOverlayMode(mode);
        }
        held = keyDown;
    }
};

constexpr std::optional<OverlayMode> parseOverlayMode(std::string_view text)
{
    if (text == "off") return OverlayMode::Off;
    if (text == "compact") return OverlayMode::Compact;
    if (text == "full") return OverlayMode::Full;
    return std::nullopt;
}

// A screenshot shows the scene unless an overlay was asked for; a played game starts compact.
constexpr OverlayMode startingOverlayMode(bool screenshot, std::optional<OverlayMode> requested)
{
    if (requested) return *requested;
    return screenshot ? OverlayMode::Off : OverlayMode::Compact;
}

}
