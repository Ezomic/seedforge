#pragma once

#include "ichor/core/key_bindings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

enum class DisplayMode : std::uint8_t { Windowed, Fullscreen };

enum class ColourPalette : std::uint8_t { Default, ColourblindSafe };

// A window or display size in pixels. 0 x 0 is "as the window was made": the default window size, or the
// display's own mode in fullscreen.
struct Resolution {
    int width = 0;
    int height = 0;

    bool set() const { return width > 0 && height > 0; }
    bool operator==(const Resolution&) const = default;
};

// What the player can change from the pause menu. None of it is simulation state: it is read
// when the picture is built and when look input is turned into motion, never hashed or saved
// in a slot.
struct Settings {
    static constexpr float kDefaultFovDegrees = 45.0f;
    static constexpr float kMinFovDegrees = 40.0f;
    static constexpr float kMaxFovDegrees = 110.0f;
    static constexpr float kDefaultMouseSensitivity = 1.0f;
    static constexpr float kMinMouseSensitivity = 0.1f;
    static constexpr float kMaxMouseSensitivity = 5.0f;
    static constexpr DisplayMode kDefaultDisplayMode = DisplayMode::Windowed;
    static constexpr int kMaxResolutionSide = 16384;
    static constexpr bool kDefaultVsync = true;
    static constexpr float kDefaultUiScale = 1.0f;
    static constexpr float kMinUiScale = 0.75f;
    static constexpr float kMaxUiScale = 1.5f;
    static constexpr ColourPalette kDefaultPalette = ColourPalette::Default;

    float fovDegrees = kDefaultFovDegrees; // vertical
    float mouseSensitivity = kDefaultMouseSensitivity; // scales the right-button mouse look
    // The first-minute prompts already shown, a bit per FirstPrompt. promptsSeen=0 in the file shows them all again.
    std::uint32_t promptsSeen = 0;
    DisplayMode displayMode = kDefaultDisplayMode;
    Resolution resolution; // the window size, or the fullscreen mode; 0 x 0 leaves the window as made
    bool vsync = kDefaultVsync;
    float uiScale = kDefaultUiScale; // the menus and the HUD are drawn this many times their size
    ColourPalette palette = kDefaultPalette;
    KeyBindings bindings; // the keys of the actions the player can rebind; bind.<action>=<key> in the file

    // Every field inside its range; a NaN or infinity becomes the default, as does a resolution with a side
    // that is zero, negative or absurd. A bad or doubled key goes back to the action's default (sanitizedBindings).
    Settings clamped() const;

    bool operator==(const Settings&) const = default;
};

// The sensitivity this run looks with: the --mouse-sens value when one is given, else the stored
// one, clamped into the setting's range. The override is never written back to the file.
float effectiveMouseSensitivity(const Settings& stored, std::optional<float> override);

// The settings after the player steps the sensitivity from the one they look with now, landing on
// a tenth and inside the range.
Settings stepMouseSensitivity(const Settings& stored, float current, int steps);

// The settings after the player steps the UI scale a quarter at a time, inside the range.
Settings stepUiScale(const Settings& stored, int steps);

// The settings after the player steps the resolution through the display's sizes, starting from the one
// nearest the size shown now and stopping at either end. An empty list changes nothing.
Settings stepResolution(const Settings& stored, const std::vector<Resolution>& modes, Resolution current, int steps);

std::string serializeSettings(const Settings& settings);

// Reads the text serializeSettings wrote. A file that is empty, cut short, of another version
// or not text at all gives the defaults; a single bad value gives that field's default, an
// out-of-range one is clamped, and keys it does not know are ignored.
Settings parseSettings(std::string_view text);

// A missing or unreadable file gives the defaults. Saving goes through a temporary file, so a
// crash leaves the old settings, not half of the new ones.
Settings loadSettings(const std::filesystem::path& path);
bool saveSettings(const std::filesystem::path& path, const Settings& settings);

}
