#include "ichor/factions/faction_palette.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 64 * 1024;
constexpr int kMaxDepth = 8;
constexpr double kMaxMinDifference = 100.0;

constexpr std::array<std::string_view, kFactionRoleCount> kRoleNames = {"incumbent", "challenger", "parasite", "custodian", "refusenik", "herald"};

using Vec = std::array<double, 3>;
using Matrix = std::array<Vec, 3>;

constexpr Matrix kProtanopia = {{{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}}};
constexpr Matrix kDeuteranopia = {{{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}}};
constexpr Matrix kTritanopia = {{{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}}};

double toLinear(std::uint8_t channel)
{
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double fromLinear(double linear)
{
    const double c = std::clamp(linear, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

Vec simulateLinear(Rgb8 colour, ColourVision vision)
{
    const Vec in = {toLinear(colour.r), toLinear(colour.g), toLinear(colour.b)};
    const Matrix* matrix = nullptr;
    switch (vision) {
    case ColourVision::Protanopia: matrix = &kProtanopia; break;
    case ColourVision::Deuteranopia: matrix = &kDeuteranopia; break;
    case ColourVision::Tritanopia: matrix = &kTritanopia; break;
    case ColourVision::Normal: return in;
    }
    Vec out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out[row] = (*matrix)[row][0] * in[0] + (*matrix)[row][1] * in[1] + (*matrix)[row][2] * in[2];
    }
    return out;
}

Vec toLab(const Vec& linear)
{
    const Vec c = {std::clamp(linear[0], 0.0, 1.0), std::clamp(linear[1], 0.0, 1.0), std::clamp(linear[2], 0.0, 1.0)};
    const double x = (0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2]) / 0.95047;
    const double y = 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2];
    const double z = (0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2]) / 1.08883;
    const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    return {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
}

// Nesting is counted before parsing, so a hostile file never reaches a recursive walk.
bool nestsTooDeep(std::string_view text)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > kMaxDepth) {
                return true;
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return false;
}

void note(std::vector<std::string>& problems, std::string_view label, std::string_view path, std::string_view what)
{
    problems.push_back(std::string(label) + ": " + std::string(path) + ": " + std::string(what));
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}

std::string_view colourVisionName(ColourVision vision)
{
    switch (vision) {
    case ColourVision::Normal: return "normal";
    case ColourVision::Protanopia: return "protanopia";
    case ColourVision::Deuteranopia: return "deuteranopia";
    case ColourVision::Tritanopia: return "tritanopia";
    }
    return "normal";
}

std::optional<Rgb8> parseHexColour(std::string_view text)
{
    if (text.size() != 7 || text[0] != '#') {
        return std::nullopt;
    }
    int digits[6];
    for (std::size_t i = 0; i < 6; ++i) {
        digits[i] = hexDigit(text[i + 1]);
        if (digits[i] < 0) {
            return std::nullopt;
        }
    }
    return Rgb8{static_cast<std::uint8_t>(digits[0] * 16 + digits[1]), static_cast<std::uint8_t>(digits[2] * 16 + digits[3]),
        static_cast<std::uint8_t>(digits[4] * 16 + digits[5])};
}

std::string toHexColour(Rgb8 colour)
{
    constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out = "#";
    for (const std::uint8_t channel : {colour.r, colour.g, colour.b}) {
        out += kDigits[channel >> 4];
        out += kDigits[channel & 15];
    }
    return out;
}

Rgb8 simulateColourVision(Rgb8 colour, ColourVision vision)
{
    const Vec linear = simulateLinear(colour, vision);
    const auto channel = [](double v) { return static_cast<std::uint8_t>(std::lround(fromLinear(v) * 255.0)); };
    return Rgb8{channel(linear[0]), channel(linear[1]), channel(linear[2])};
}

double colourDifference(Rgb8 a, Rgb8 b, ColourVision vision)
{
    const Vec la = toLab(simulateLinear(a, vision));
    const Vec lb = toLab(simulateLinear(b, vision));
    const double dl = la[0] - lb[0];
    const double da = la[1] - lb[1];
    const double db = la[2] - lb[2];
    return std::sqrt(dl * dl + da * da + db * db);
}

bool parseFactionPalette(std::string_view label, std::string_view text, FactionPalette& out, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    if (text.size() > kMaxFileBytes) {
        note(problems, label, "$", "file is too large");
        return false;
    }
    if (nestsTooDeep(text)) {
        note(problems, label, "$", "nests too deeply");
        return false;
    }
    const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        note(problems, label, "$", "not a JSON object");
        return false;
    }
    for (const auto& item : root.items()) {
        if (item.key() != "min_difference" && item.key() != "roles") {
            note(problems, label, item.key(), "unknown key");
        }
    }

    FactionPalette palette;
    const auto minIt = root.find("min_difference");
    if (minIt == root.end() || !minIt->is_number()) {
        note(problems, label, "min_difference", "must be a number");
    } else {
        const double value = minIt->get<double>();
        if (!std::isfinite(value) || value <= 0.0 || value > kMaxMinDifference) {
            note(problems, label, "min_difference", "must be above 0 and at most 100");
        } else {
            palette.minDifference = value;
        }
    }

    const auto rolesIt = root.find("roles");
    if (rolesIt == root.end() || !rolesIt->is_object()) {
        note(problems, label, "roles", "must be an object");
    } else {
        for (const auto& item : rolesIt->items()) {
            if (!factionRoleFromName(item.key())) {
                note(problems, label, "roles." + item.key(), "unknown role");
            }
        }
        for (std::size_t i = 0; i < kFactionRoleCount; ++i) {
            const std::string path = "roles." + std::string(kRoleNames[i]);
            const auto it = rolesIt->find(std::string(kRoleNames[i]));
            if (it == rolesIt->end()) {
                note(problems, label, path, "missing");
                continue;
            }
            const std::optional<Rgb8> colour = it->is_string() ? parseHexColour(it->get_ref<const std::string&>()) : std::nullopt;
            if (!colour) {
                note(problems, label, path, "must be a \"#RRGGBB\" string");
                continue;
            }
            palette.roles[i] = *colour;
        }
    }

    if (problems.size() != before) {
        return false;
    }
    out = palette;
    return true;
}

double minPaletteDifference(const FactionPalette& palette, ColourVision vision)
{
    double smallest = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < kFactionRoleCount; ++i) {
        for (std::size_t j = i + 1; j < kFactionRoleCount; ++j) {
            smallest = std::min(smallest, colourDifference(palette.roles[i], palette.roles[j], vision));
        }
    }
    return smallest;
}

std::vector<std::string> checkPaletteDistinguishable(const FactionPalette& palette)
{
    std::vector<std::string> problems;
    for (const ColourVision vision : kColourVisions) {
        for (std::size_t i = 0; i < kFactionRoleCount; ++i) {
            for (std::size_t j = i + 1; j < kFactionRoleCount; ++j) {
                const double difference = colourDifference(palette.roles[i], palette.roles[j], vision);
                if (!(difference >= palette.minDifference)) {
                    problems.push_back(std::string(kRoleNames[i]) + " and " + std::string(kRoleNames[j]) + " are too close under " + std::string(colourVisionName(vision))
                        + " (" + std::to_string(difference) + " < " + std::to_string(palette.minDifference) + ")");
                }
            }
        }
    }
    return problems;
}

}
