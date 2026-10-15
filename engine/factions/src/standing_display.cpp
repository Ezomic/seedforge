#include "ichor/factions/standing_display.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ichor {

namespace {

StandingDisplayTier shownTier(const StandingTier& tier)
{
    return {tier.id, tier.name, tier.threshold};
}

std::size_t tierIndexOf(const std::vector<StandingTier>& tiers, std::int32_t value)
{
    std::size_t index = 0;
    for (std::size_t i = 0; i < tiers.size(); ++i) {
        if (tiers[i].threshold <= value) {
            index = i;
        }
    }
    return index;
}

}

StandingDisplay buildStandingDisplay(std::string_view faction, double standing, const std::vector<Faction>& factions,
    const std::vector<StandingTier>& tiers, const std::vector<StandingGate>& gates)
{
    StandingDisplay display;
    display.faction = std::string(faction);

    if (std::none_of(factions.begin(), factions.end(), [&](const Faction& f) { return f.id == faction; })) {
        display.status = StandingDisplayStatus::UnknownFaction;
        return display;
    }
    if (!std::isfinite(standing)) {
        display.status = StandingDisplayStatus::InvalidStanding;
        return display;
    }
    if (tiers.empty()) {
        display.status = StandingDisplayStatus::NoTiers;
        return display;
    }

    display.standing = static_cast<std::int32_t>(std::clamp(standing, static_cast<double>(kStandingMin), static_cast<double>(kStandingMax)));
    const std::size_t index = tierIndexOf(tiers, display.standing);
    display.tier = shownTier(tiers[index]);
    display.tierIndex = index;
    display.tierCount = tiers.size();

    display.atTopTier = index + 1 >= tiers.size();
    if (display.atTopTier) {
        display.progress = 1.0;
    } else {
        display.nextTier = shownTier(tiers[index + 1]);
        display.pointsToNext = std::max<std::int32_t>(display.nextTier.threshold - display.standing, 0);
        const std::int64_t span = static_cast<std::int64_t>(display.nextTier.threshold) - display.tier.threshold;
        if (span > 0) {
            display.progress = std::clamp(static_cast<double>(display.standing - display.tier.threshold) / static_cast<double>(span), 0.0, 1.0);
        }
    }

    std::set<std::string> seen;
    std::set<std::string> atTier;
    std::set<std::string> now;
    std::set<std::string> atNext;
    for (const StandingGate& gate : gates) {
        if (gate.faction != faction) {
            continue;
        }
        const auto minimum = std::find_if(tiers.begin(), tiers.end(), [&](const StandingTier& t) { return t.id == gate.minTier; });
        if (minimum == tiers.end() || !seen.insert(gate.id).second) {
            continue;
        }
        const auto gateIndex = static_cast<std::size_t>(minimum - tiers.begin());
        if (gateIndex <= index) {
            now.insert(gate.id);
        }
        if (gateIndex == index) {
            atTier.insert(gate.id);
        } else if (gateIndex == index + 1) {
            atNext.insert(gate.id);
        }
    }
    display.unlocksAtTier.assign(atTier.begin(), atTier.end());
    display.unlockedNow.assign(now.begin(), now.end());
    display.unlocksAtNextTier.assign(atNext.begin(), atNext.end());
    return display;
}

}
