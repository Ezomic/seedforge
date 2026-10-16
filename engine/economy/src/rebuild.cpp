#include "ichor/economy/rebuild.h"

#include <algorithm>

namespace ichor {

namespace {

bool validId(std::string_view id)
{
    return !id.empty() && id.size() <= kMaxSiteIdLength
        && std::all_of(id.begin(), id.end(), [](char c) { return c > 0x20 && c < 0x7f; });
}

bool validKind(SiteKind kind)
{
    return kind == SiteKind::Settlement || kind == SiteKind::Bridge || kind == SiteKind::Route;
}

bool validCondition(SiteCondition condition)
{
    return condition == SiteCondition::Ruined || condition == SiteCondition::Damaged || condition == SiteCondition::Restored;
}

}

std::optional<RebuildBoard> RebuildBoard::create(std::span<const RebuildSite> sites, std::string& error)
{
    if (sites.size() > kMaxRebuildSites) {
        error = "too many rebuild sites";
        return std::nullopt;
    }
    RebuildBoard board;
    for (const RebuildSite& site : sites) {
        if (!validId(site.id)) {
            error = "site id must be 1 to " + std::to_string(kMaxSiteIdLength) + " printable characters without spaces";
            return std::nullopt;
        }
        if (!validKind(site.kind) || !validCondition(site.condition)) {
            error = "site '" + site.id + "': unknown kind or condition";
            return std::nullopt;
        }
        if (!board.sites_.emplace(site.id, site).second) {
            error = "site '" + site.id + "' is listed twice";
            return std::nullopt;
        }
    }
    return board;
}

StartStatus RebuildBoard::start(std::string_view site, SiteCondition target, std::uint64_t cost, std::uint32_t& projectId)
{
    const auto found = sites_.find(site);
    if (found == sites_.end()) {
        return StartStatus::UnknownSite;
    }
    if (!validCondition(target) || target <= found->second.condition) {
        return StartStatus::NotAnImprovement;
    }
    if (cost == 0 || cost > kMaxCurrencyCap) {
        return StartStatus::BadCost;
    }
    if (projects_.size() >= kMaxRebuildProjects) {
        return StartStatus::TooManyProjects;
    }
    const bool busy = std::any_of(projects_.begin(), projects_.end(),
        [&](const RebuildProject& p) { return !p.done && p.site == site; });
    if (busy) {
        return StartStatus::SiteBusy;
    }
    projects_.push_back({found->first, target, cost, 0, false});
    projectId = static_cast<std::uint32_t>(projects_.size() - 1);
    return StartStatus::Ok;
}

FundResult RebuildBoard::fund(Wallet& wallet, const CurrencyTable& table, std::uint32_t projectId, std::uint64_t amount)
{
    if (projectId >= projects_.size()) {
        return {FundStatus::UnknownProject, 0, false};
    }
    RebuildProject& project = projects_[projectId];
    if (project.done) {
        return {FundStatus::AlreadyDone, 0, false};
    }
    const std::uint64_t charge = std::min(amount, project.cost - project.funded);
    switch (wallet.spend(table, CurrencySlot::Coin, charge, "rebuild")) {
    case TxStatus::NotAllowed:
        return {FundStatus::NotAllowed, 0, false};
    case TxStatus::Insufficient:
        return {FundStatus::Insufficient, 0, false};
    case TxStatus::Ok:
        break;
    }
    project.funded += charge;
    const bool completed = project.funded == project.cost;
    if (completed) {
        project.done = true;
        sites_.find(project.site)->second.condition = project.target;
    }
    return {FundStatus::Ok, charge, completed};
}

std::optional<SiteCondition> RebuildBoard::condition(std::string_view site) const
{
    const auto found = sites_.find(site);
    if (found == sites_.end()) {
        return std::nullopt;
    }
    return found->second.condition;
}

std::optional<RouteStatus> RebuildBoard::routeStatus(std::string_view site) const
{
    const auto found = sites_.find(site);
    if (found == sites_.end() || found->second.kind != SiteKind::Route) {
        return std::nullopt;
    }
    switch (found->second.condition) {
    case SiteCondition::Ruined:
        return RouteStatus::Cut;
    case SiteCondition::Damaged:
        return RouteStatus::Strained;
    case SiteCondition::Restored:
        return RouteStatus::Open;
    }
    return std::nullopt;
}

const RebuildProject* RebuildBoard::project(std::uint32_t projectId) const
{
    return projectId < projects_.size() ? &projects_[projectId] : nullptr;
}

}
