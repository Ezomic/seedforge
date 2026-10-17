#include "ichor/ledger/quest_visible_effect.h"

#include <map>
#include <utility>

namespace ichor {

namespace {

bool validTarget(std::string_view id)
{
    if (id.empty() || id.size() > kMaxEffectTargetBytes) {
        return false;
    }
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool validMetric(VisibleMetric metric)
{
    return static_cast<std::uint8_t>(metric) <= static_cast<std::uint8_t>(VisibleMetric::Route);
}

}

VisibleNumberReport checkVisibleNumber(const std::vector<QuestEffect>& effects, const std::set<std::string, std::less<>>* knownTargets)
{
    VisibleNumberReport report;
    if (effects.empty()) {
        report.verdict = VisibleNumberVerdict::NoEffects;
        return report;
    }
    if (effects.size() > kMaxQuestEffects) {
        report.verdict = VisibleNumberVerdict::TooManyEffects;
        return report;
    }
    std::map<std::pair<std::uint8_t, std::string>, std::int64_t> sums;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        const QuestEffect& effect = effects[i];
        if (!validMetric(effect.metric) || !validTarget(effect.target) || effect.delta < -kMaxEffectDelta || effect.delta > kMaxEffectDelta) {
            report.verdict = VisibleNumberVerdict::InvalidEffect;
            report.badIndex = i;
            return report;
        }
        if (knownTargets != nullptr && knownTargets->find(effect.target) == knownTargets->end()) {
            report.verdict = VisibleNumberVerdict::UnknownTarget;
            report.badIndex = i;
            return report;
        }
        sums[{static_cast<std::uint8_t>(effect.metric), effect.target}] += effect.delta;
    }
    for (const auto& entry : sums) {
        if (entry.second != 0) {
            ++report.movedNumbers;
        }
    }
    report.verdict = report.movedNumbers > 0 ? VisibleNumberVerdict::Moves : VisibleNumberVerdict::AllCancel;
    return report;
}

}
