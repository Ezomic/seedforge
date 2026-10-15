#include "ichor/progression/discovery_xp.h"

#include <algorithm>
#include <limits>

namespace ichor {

namespace {

constexpr std::int64_t kPermille = 1000;
constexpr std::size_t kMaxRepeatSteps = 200;

bool validKind(DiscoveryKind kind)
{
    return static_cast<std::size_t>(kind) < kDiscoveryKindCount;
}

// NaN fails both comparisons, so it is rejected along with negatives and infinities.
bool validScore(double score)
{
    return score >= 0.0 && score <= std::numeric_limits<double>::max();
}

}

std::int64_t discoveryBaseXp(DiscoveryKind kind)
{
    switch (kind) {
    case DiscoveryKind::Poi:
        return kPoiBaseXp;
    case DiscoveryKind::Landmark:
        return kLandmarkBaseXp;
    case DiscoveryKind::Vista:
        return kVistaBaseXp;
    }
    return 0;
}

std::int64_t discoveryRepeatPermille(std::size_t priorOfKind)
{
    std::int64_t permille = kPermille;
    const std::size_t steps = std::min(priorOfKind, kMaxRepeatSteps);
    for (std::size_t i = 0; i < steps && permille > kRepeatFloorPermille; ++i) {
        permille = permille * kRepeatKeepPermille / kPermille;
    }
    return std::max(permille, kRepeatFloorPermille);
}

std::int64_t discoveryXp(DiscoveryKind kind, double score, std::size_t priorOfKind)
{
    if (!validKind(kind) || !validScore(score)) {
        return 0;
    }
    const double clamped = std::min(score, kMaxDiscoveryScore);
    const auto scorePermille = static_cast<std::int64_t>(clamped * static_cast<double>(kPermille));
    return discoveryBaseXp(kind) * scorePermille * discoveryRepeatPermille(priorOfKind) / (kPermille * kPermille);
}

DiscoveryResult DiscoveryLedger::record(const Discovery& discovery)
{
    if (discovery.id.empty() || discovery.id.size() > kMaxDiscoveryIdBytes) {
        return {DiscoveryStatus::InvalidId, 0};
    }
    if (!validKind(discovery.kind)) {
        return {DiscoveryStatus::InvalidKind, 0};
    }
    if (!validScore(discovery.score)) {
        return {DiscoveryStatus::InvalidScore, 0};
    }
    if (seen_.find(std::string_view(discovery.id)) != seen_.end()) {
        return {DiscoveryStatus::Duplicate, 0};
    }
    if (seen_.size() >= kMaxDiscoveries) {
        return {DiscoveryStatus::LedgerFull, 0};
    }
    const auto index = static_cast<std::size_t>(discovery.kind);
    const std::int64_t xp = discoveryXp(discovery.kind, discovery.score, perKind_[index]);
    seen_.insert(discovery.id);
    ++perKind_[index];
    totalXp_ = std::min(kMaxTotalXp, totalXp_ + xp);
    return {DiscoveryStatus::Awarded, xp};
}

bool DiscoveryLedger::has(std::string_view id) const
{
    return seen_.find(id) != seen_.end();
}

std::size_t DiscoveryLedger::countOf(DiscoveryKind kind) const
{
    return validKind(kind) ? perKind_[static_cast<std::size_t>(kind)] : 0;
}

}
