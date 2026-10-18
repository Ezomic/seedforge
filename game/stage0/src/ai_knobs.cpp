#include "ichor/stage0/ai_knobs.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

namespace ichor::stage0 {

float clampKnob(const KnobSpec& spec, float value)
{
    if (!std::isfinite(value)) return AiKnobs{}.*spec.member;
    return std::clamp(value, spec.minimum, spec.maximum);
}

KnobLoad knobsFromRecord(const DataRecord& record)
{
    KnobLoad load;
    for (const auto& spec : kKnobSpecs) {
        const auto it = record.fields.find(spec.key);
        if (it == record.fields.end()) continue;
        const double* number = std::get_if<double>(&it->second);
        if (number == nullptr || !std::isfinite(*number)) {
            load.problems.push_back(std::format("{} is not a finite number, kept the default", spec.key));
            continue;
        }
        const double clamped = std::clamp(*number, static_cast<double>(spec.minimum), static_cast<double>(spec.maximum));
        if (clamped != *number) {
            load.problems.push_back(std::format("{} out of range [{}, {}], clamped", spec.key, spec.minimum, spec.maximum));
        }
        load.knobs.*spec.member = static_cast<float>(clamped);
    }
    for (const auto& [key, value] : record.fields) {
        const bool known = std::any_of(kKnobSpecs.begin(), kKnobSpecs.end(), [&](const KnobSpec& s) { return key == s.key; });
        if (!known) load.problems.push_back(std::format("unknown field {}", key));
    }
    return load;
}

KnobLoad loadKnobs(const DataSet& data)
{
    const DataRecord* record = data.record("ai_knobs", "skirmisher");
    if (record == nullptr) {
        KnobLoad load;
        load.problems.push_back("no ai_knobs/skirmisher record, using defaults");
        return load;
    }
    return knobsFromRecord(*record);
}

KnobLoad loadKnobsFromDirectory(const std::filesystem::path& root)
{
    DataSet data;
    if (!data.loadDirectory(root)) {
        KnobLoad load;
        load.problems.push_back(std::format("could not load {}: {}, using defaults", root.string(), data.lastError()));
        return load;
    }
    return loadKnobs(data);
}

std::string knobStampLine(const AiKnobs& knobs, std::string_view source, int firstFight)
{
    std::string line = std::format("first_fight={}", firstFight);
    for (const auto& spec : kKnobSpecs) {
        line += std::format(",{}={}", spec.key, knobs.*spec.member);
    }
    std::string clean(source);
    std::replace_if(clean.begin(), clean.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    line += std::format(",source={}", clean);
    return line;
}

std::filesystem::path knobStampPath(const std::filesystem::path& csv)
{
    std::filesystem::path out = csv;
    out += ".knobs.txt";
    return out;
}

bool appendKnobStamp(const std::filesystem::path& csv, const std::string& line)
{
    if (csv.empty()) return false;
    std::ofstream out(knobStampPath(csv), std::ios::app);
    out << line << '\n';
    return static_cast<bool>(out);
}

Tuning applyKnobs(Tuning base, const AiKnobs& knobs)
{
    const auto clamped = [&](float AiKnobs::* member) {
        for (const auto& spec : kKnobSpecs) {
            if (spec.member == member) return clampKnob(spec, knobs.*member);
        }
        return knobs.*member;
    };
    base.desireGainRate = clamped(&AiKnobs::desireGainRate);
    base.preferredSpacing = clamped(&AiKnobs::preferredSpacing);
    base.restDuration = clamped(&AiKnobs::restDuration);
    base.punishReactionTime = clamped(&AiKnobs::punishReactionTime);
    const float scale = clamped(&AiKnobs::windupScale);
    base.lightWindup *= scale;
    base.heavyWindup *= scale;
    base.trackingCapWindupDegPerSec = clamped(&AiKnobs::trackingCapDegPerSec);
    return base;
}

std::vector<OverlayLine> overlayLines(const OverlayInput& input, const Tuning& tuning)
{
    std::vector<OverlayLine> lines;
    if (input.foe == nullptr || input.brain == nullptr) {
        lines.push_back({"state", "no foe"});
        return lines;
    }
    const Combatant& foe = *input.foe;
    const SkirmisherBrain& brain = *input.brain;
    const auto finite = [](float v) { return std::isfinite(v) ? v : 0.0f; };

    lines.push_back({"state", std::format("{} / {}", name(brain.brainState()), name(foe.combatState()))});
    lines.push_back({"desire", std::format("{:.2f} / {:.2f}", finite(brain.desire), finite(brain.nextThreshold))});
    const float distance = finite(input.distance);
    const float wanted = finite(tuning.preferredSpacing);
    lines.push_back({"spacing", std::format("{:.2f} m (want {:.2f}, error {:+.2f})", distance, wanted, distance - wanted)});

    switch (brain.brainState()) {
    case BrainState::Tell:
        lines.push_back({"timer", std::format("tell {:.2f} / {:.2f} s", finite(brain.stateTime), finite(tuning.tellDuration))});
        break;
    case BrainState::Punish:
        lines.push_back({"timer", std::format("punish reaction {:.2f} / {:.2f} s", finite(brain.stateTime), finite(tuning.punishReactionTime))});
        break;
    case BrainState::Rest:
        lines.push_back({"timer", std::format("rest {:.2f} / {:.2f} s", finite(brain.stateTime), finite(tuning.restDuration))});
        break;
    case BrainState::Committing: {
        const bool heavy = foe.attack == static_cast<std::uint8_t>(AttackKind::Heavy);
        lines.push_back({"timer", std::format("{} {} {:.2f} s (windup {:.2f} s)", heavy ? "heavy" : "light", name(foe.combatState()),
                                      finite(foe.stateTime), finite(heavy ? tuning.heavyWindup : tuning.lightWindup))});
        break;
    }
    default:
        lines.push_back({"timer", "none"});
        break;
    }

    lines.push_back({"tracking", std::format("{:.1f} deg off, cap {:.1f} deg/s", finite(input.trackingAngleDeg), finite(input.allowedTurnDegPerSec))});
    lines.push_back({"pressure", std::format("{:.1f} s, commits {}, follow-ups {}", finite(brain.pressure), brain.commits, brain.followUps)});
    return lines;
}

}
