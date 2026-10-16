#pragma once

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

}
