#pragma once

#include "ichor/economy/merchant.h"
#include "ichor/economy/wallet.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 06 sink: funding a settlement, a bridge or a route. A site is ruined,
// damaged or restored, and a project pays coin under the "rebuild" sink until
// it has raised its cost, at which point the site moves to the project's
// target condition. A route site reads back as the RouteStatus the merchant
// pricing already understands (ruined = cut, damaged = strained, restored =
// open), so restoring a route lowers prices without further wiring.

enum class SiteKind : std::uint8_t { Settlement, Bridge, Route };
enum class SiteCondition : std::uint8_t { Ruined, Damaged, Restored };

constexpr std::size_t kMaxRebuildSites = 4096;
constexpr std::size_t kMaxRebuildProjects = 65536;
constexpr std::size_t kMaxSiteIdLength = 64;

struct RebuildSite {
    std::string id; // 1 to kMaxSiteIdLength printable ASCII characters, no spaces
    SiteKind kind = SiteKind::Settlement;
    SiteCondition condition = SiteCondition::Ruined;
};

struct RebuildProject {
    std::string site;
    SiteCondition target = SiteCondition::Restored;
    std::uint64_t cost = 0;   // 1 to kMaxCurrencyCap
    std::uint64_t funded = 0; // never above cost
    bool done = false;

    friend bool operator==(const RebuildProject&, const RebuildProject&) = default;
};

enum class StartStatus { Ok, UnknownSite, NotAnImprovement, SiteBusy, BadCost, TooManyProjects };
enum class FundStatus { Ok, UnknownProject, AlreadyDone, NotAllowed, Insufficient };

struct FundResult {
    FundStatus status = FundStatus::Ok;
    std::uint64_t spent = 0; // coin drawn; an overpay is clamped to what the project still needs
    bool completed = false;  // this payment finished the project
};

class RebuildBoard {
public:
    // Nothing (with `error` set) for an empty, overlong, non-printable or
    // duplicate site id, an out-of-range kind or condition, or more than
    // kMaxRebuildSites sites.
    static std::optional<RebuildBoard> create(std::span<const RebuildSite> sites, std::string& error);

    // Opens a project to bring `site` up to `target` for `cost` coin. One
    // unfinished project per site; later projects on the same site are
    // allowed once it is done. `projectId` is set only on Ok.
    [[nodiscard]] StartStatus start(std::string_view site, SiteCondition target, std::uint64_t cost, std::uint32_t& projectId);

    // Draws min(amount, remaining) coin from the wallet under the "rebuild"
    // sink. The overpay is clamped before the wallet is checked, so a project
    // costing more than the wallet holds is paid in instalments. All or nothing: a refused payment changes neither wallet nor project.
    [[nodiscard]] FundResult fund(Wallet& wallet, const CurrencyTable& table, std::uint32_t projectId, std::uint64_t amount);

    std::optional<SiteCondition> condition(std::string_view site) const;
    // Only for Route sites.
    std::optional<RouteStatus> routeStatus(std::string_view site) const;
    const RebuildProject* project(std::uint32_t projectId) const;
    const std::vector<RebuildProject>& projects() const { return projects_; }

private:
    RebuildBoard() = default;

    std::map<std::string, RebuildSite, std::less<>> sites_;
    std::vector<RebuildProject> projects_;
};

}
