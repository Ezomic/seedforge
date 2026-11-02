#include "ichor/audio/ambience_suppress_rules.h"

#include <algorithm>

namespace ichor {

namespace {

struct Rule {
    std::string_view axiom;
    std::string_view source;
    float suppression;
};

constexpr std::string_view kFaunaClips[] = {
    "A_Dawn_Birds", "A_Day_Insects", "A_Dusk_Crickets", "A_Night_Insects", "A_Night_Wolves"};

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

std::string_view bedSource(std::string_view clip)
{
    return std::find(std::begin(kFaunaClips), std::end(kFaunaClips), clip) != std::end(kFaunaClips) ? kSourceFauna : std::string_view{};
}

void suppressBeds(std::vector<BedGain>& beds, std::span<const std::string> activeAxioms)
{
    for (BedGain& bed : beds)
        bed.gain *= 1.0f - sourceSuppression(activeAxioms, bedSource(bed.clip));
    std::erase_if(beds, [](const BedGain& bed) { return !(bed.gain > 0.0f); });
}

std::vector<BedGain> axiomBeds(const Soundscape& soundscape, std::span<const std::string> activeAxioms,
    std::string_view primary, std::string_view secondary, float weight, float hour)
{
    std::vector<BedGain> beds = blendedBeds(soundscape, primary, secondary, weight, hour);
    suppressBeds(beds, activeAxioms);
    return beds;
}

}
