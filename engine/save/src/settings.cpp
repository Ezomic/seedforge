#include "ichor/save/settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

namespace ichor {

namespace {

constexpr std::string_view kHeader = "ichor-settings 1";
constexpr std::string_view kTrailer = "end";
constexpr std::size_t kMaxBytes = 4096;
constexpr float kMouseSensitivityStep = 0.1f;

float clampField(float value, float low, float high, float fallback)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

Resolution clampResolution(Resolution resolution)
{
    const auto sane = [](int side) { return side > 0 && side <= Settings::kMaxResolutionSide; };
    return sane(resolution.width) && sane(resolution.height) ? resolution : Resolution{};
}

bool parseInt(std::string_view text, int& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

// "WxH"; anything else is false.
bool parseResolution(std::string_view text, Resolution& out)
{
    const std::size_t x = text.find('x');
    return x != std::string_view::npos && parseInt(text.substr(0, x), out.width) && parseInt(text.substr(x + 1), out.height);
}

std::string_view displayModeName(DisplayMode mode)
{
    return mode == DisplayMode::Fullscreen ? "fullscreen" : "windowed";
}

bool parseFloat(std::string_view text, float& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    if (result.ec == std::errc::result_out_of_range && result.ptr == text.data() + text.size()) {
        out = text.front() == '-' ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max(); // too big a number is still a big number, so it clamps
        return true;
    }
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

}

Settings Settings::clamped() const
{
    return {clampField(fovDegrees, kMinFovDegrees, kMaxFovDegrees, kDefaultFovDegrees),
            clampField(mouseSensitivity, kMinMouseSensitivity, kMaxMouseSensitivity, kDefaultMouseSensitivity), promptsSeen,
            displayMode == DisplayMode::Fullscreen ? DisplayMode::Fullscreen : DisplayMode::Windowed, clampResolution(resolution), vsync};
}

float effectiveMouseSensitivity(const Settings& stored, std::optional<float> override)
{
    const float storedValue = stored.clamped().mouseSensitivity;
    return override ? clampField(*override, Settings::kMinMouseSensitivity, Settings::kMaxMouseSensitivity, storedValue) : storedValue;
}

Settings stepMouseSensitivity(const Settings& stored, float current, int steps)
{
    if (steps == 0) {
        return stored;
    }
    Settings stepped = stored;
    stepped.mouseSensitivity = std::round((current + static_cast<float>(steps) * kMouseSensitivityStep) * 10.0f) / 10.0f;
    return stepped.clamped();
}

Settings stepResolution(const Settings& stored, const std::vector<Resolution>& modes, Resolution current, int steps)
{
    if (modes.empty() || steps == 0) {
        return stored;
    }
    const auto distance = [&](const Resolution& mode) {
        return std::abs(static_cast<long long>(mode.width) * mode.height - static_cast<long long>(current.width) * current.height)
            + std::abs(static_cast<long long>(mode.width) - current.width);
    };
    const auto nearest = std::min_element(modes.begin(), modes.end(), [&](const Resolution& a, const Resolution& b) { return distance(a) < distance(b); });
    const long long from = nearest - modes.begin();
    const long long to = std::clamp(from + steps, 0LL, static_cast<long long>(modes.size()) - 1);
    Settings stepped = stored;
    stepped.resolution = modes[static_cast<std::size_t>(to)];
    return stepped.clamped();
}

std::string serializeSettings(const Settings& settings)
{
    const Settings safe = settings.clamped();
    return std::format("{}\nfov={}\nmouseSensitivity={}\npromptsSeen={}\ndisplay={}\nresolution={}x{}\nvsync={}\n{}\n", kHeader, safe.fovDegrees, safe.mouseSensitivity, safe.promptsSeen,
        displayModeName(safe.displayMode), safe.resolution.width, safe.resolution.height, safe.vsync ? 1 : 0, kTrailer);
}

Settings parseSettings(std::string_view text)
{
    Settings settings;
    if (text.size() > kMaxBytes) {
        return settings;
    }
    bool headerSeen = false;
    bool trailerSeen = false;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line = trim(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        if (!headerSeen) {
            if (line != kHeader) {
                return Settings{};
            }
            headerSeen = true;
        } else if (line == kTrailer) {
            trailerSeen = true;
            break;
        } else if (const std::size_t equals = line.find('='); equals != std::string_view::npos) {
            const std::string_view key = line.substr(0, equals);
            const std::string_view rest = line.substr(equals + 1);
            if (key == "display") {
                settings.displayMode = rest == "fullscreen" ? DisplayMode::Fullscreen : DisplayMode::Windowed;
                continue;
            }
            if (key == "resolution") {
                Resolution resolution;
                settings.resolution = parseResolution(rest, resolution) ? resolution : Resolution{};
                continue;
            }
            if (key == "vsync") {
                settings.vsync = rest == "1" ? true : rest == "0" ? false : Settings::kDefaultVsync;
                continue;
            }
            if (key == "promptsSeen") {
                const std::string_view digits = line.substr(equals + 1);
                std::uint32_t flags = 0;
                const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), flags);
                settings.promptsSeen = result.ec == std::errc{} && result.ptr == digits.data() + digits.size() ? flags : 0u;
                continue;
            }
            float value = 0.0f;
            const bool valid = parseFloat(line.substr(equals + 1), value);
            if (key == "fov") {
                settings.fovDegrees = valid ? value : Settings::kDefaultFovDegrees;
            } else if (key == "mouseSensitivity") {
                settings.mouseSensitivity = valid ? value : Settings::kDefaultMouseSensitivity;
            }
        }
    }
    return trailerSeen ? settings.clamped() : Settings{};
}

Settings loadSettings(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    // One byte more than the most a good file holds, so an oversized file is seen as such.
    std::string text(kMaxBytes + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return parseSettings(text);
}

bool saveSettings(const std::filesystem::path& path, const Settings& settings)
{
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << serializeSettings(settings);
        if (!out.flush()) {
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

}
