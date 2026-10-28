#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ichor {

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

    float fovDegrees = kDefaultFovDegrees; // vertical
    float mouseSensitivity = kDefaultMouseSensitivity; // scales the right-button mouse look

    // Every field inside its range; a NaN or infinity becomes the default.
    Settings clamped() const;

    bool operator==(const Settings&) const = default;
};

// The sensitivity this run looks with: the --mouse-sens value when one is given, else the stored
// one, clamped into the setting's range. The override is never written back to the file.
float effectiveMouseSensitivity(const Settings& stored, std::optional<float> override);

// The settings after the player steps the sensitivity from the one they look with now, landing on
// a tenth and inside the range.
Settings stepMouseSensitivity(const Settings& stored, float current, int steps);

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
