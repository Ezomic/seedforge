#pragma once

#include "ichor/audio/soundscape.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// One ambience layer as the suppression rules see it: an id of the caller's choosing and the kind
// of sound source it is made of ("fauna", "wind", "water", ...). Tags the rules do not know are
// never suppressed.
struct AmbienceSuppressLayer {
    std::string id;
    std::string source;
};

// What the active axioms do to one layer. suppression is 0 (untouched) to 1 (silent); gainScale is
// 1 - suppression, the factor to multiply the layer's requested gain by.
struct AmbienceSuppression {
    std::string id;
    float suppression = 0.0f;
    float gainScale = 1.0f;
};

inline constexpr std::string_view kSourceFauna = "fauna";
inline constexpr std::string_view kAxiomArtemisWithdrew = "cataclysm.artemis_withdrew";

// The suppression of a single source tag under the active axiom ids. Several axioms suppressing
// the same tag take the strongest, never the sum, so the result does not depend on their order or
// on duplicates. Unknown axioms and unknown tags give 0.
float sourceSuppression(std::span<const std::string> activeAxioms, std::string_view source);

// One result per layer, in the order given. Pure and deterministic: the same axioms and layers
// always give the same answer, and nothing here feeds the world hash.
std::vector<AmbienceSuppression> suppressAmbience(
    std::span<const std::string> activeAxioms, std::span<const AmbienceSuppressLayer> layers);

// The kind of sound a soundscape clip is made of ("fauna" for the birds, insects and wolves), or "" for
// a clip the rules know nothing about, which no axiom touches.
std::string_view bedSource(std::string_view clip);

// Scales every bed by the suppression the active axioms put on its source and drops the ones that fall
// to silence. Pure: it touches only the beds it is given.
void suppressBeds(std::vector<BedGain>& beds, std::span<const std::string> activeAxioms);

// The blended biome beds of a place under a world's axioms: blendedBeds, then suppressBeds. Nothing here
// feeds the world hash.
std::vector<BedGain> axiomBeds(const Soundscape& soundscape, std::span<const std::string> activeAxioms,
    std::string_view primary, std::string_view secondary, float weight, float hour);

}
