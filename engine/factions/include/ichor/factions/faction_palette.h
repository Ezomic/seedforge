#pragma once

#include "ichor/factions/factions.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

struct Rgb8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(const Rgb8&, const Rgb8&) = default;
};

enum class ColourVision { Normal, Protanopia, Deuteranopia, Tritanopia };

constexpr std::array<ColourVision, 4> kColourVisions = {ColourVision::Normal, ColourVision::Protanopia, ColourVision::Deuteranopia, ColourVision::Tritanopia};

std::string_view colourVisionName(ColourVision vision);

// "#RRGGBB", either case. Anything else is nullopt.
std::optional<Rgb8> parseHexColour(std::string_view text);
std::string toHexColour(Rgb8 colour);

// Machado 2009 full-severity dichromacy, applied in linear light.
Rgb8 simulateColourVision(Rgb8 colour, ColourVision vision);

// CIE76 distance between two colours as seen under `vision`. About 2.3 is one
// just-noticeable difference; territory tints need far more than that.
double colourDifference(Rgb8 a, Rgb8 b, ColourVision vision);

// One territory colour per faction role, plus the smallest distance any two of
// them may have under every simulated vision (and normal vision).
struct FactionPalette {
    std::array<Rgb8, kFactionRoleCount> roles{};
    double minDifference = 0.0;

    Rgb8 colour(FactionRole role) const { return roles[static_cast<std::size_t>(role)]; }
};

// Parses {"min_difference": n, "roles": {"incumbent": "#RRGGBB", ...}}. All six
// roles are required and unknown keys are rejected. Problems are appended as
// "<label>: <path>: <what is wrong>". Returns true, and fills `out`, only when
// there are none. Never throws on bad input.
bool parseFactionPalette(std::string_view label, std::string_view text, FactionPalette& out, std::vector<std::string>& problems);

// The smallest distance between any two role colours under `vision`.
double minPaletteDifference(const FactionPalette& palette, ColourVision vision);

// Every role pair, under every vision, closer than palette.minDifference.
// Empty when the palette is safe.
std::vector<std::string> checkPaletteDistinguishable(const FactionPalette& palette);

}
