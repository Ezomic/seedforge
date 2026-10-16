#include "ichor/audio/ambience_suppress_rules.h"

#include <algorithm>

namespace ichor {

namespace {

struct Rule {
    std::string_view axiom;
    std::string_view source;
    float suppression;
};

constexpr Rule kRules[] = {
    {kAxiomArtemisWithdrew, kSourceFauna, 1.0f},
};

}

float sourceSuppression(std::span<const std::string> activeAxioms, std::string_view source)
{
    float strongest = 0.0f;
    for (const Rule& rule : kRules) {
        if (rule.source != source)
            continue;
        if (std::find(activeAxioms.begin(), activeAxioms.end(), rule.axiom) != activeAxioms.end())
            strongest = std::max(strongest, rule.suppression);
    }
    return strongest;
}

std::vector<AmbienceSuppression> suppressAmbience(
    std::span<const std::string> activeAxioms, std::span<const AmbienceSuppressLayer> layers)
{
    std::vector<AmbienceSuppression> out;
    out.reserve(layers.size());
    for (const AmbienceSuppressLayer& layer : layers) {
        const float s = sourceSuppression(activeAxioms, layer.source);
        out.push_back({layer.id, s, 1.0f - s});
    }
    return out;
}

}
