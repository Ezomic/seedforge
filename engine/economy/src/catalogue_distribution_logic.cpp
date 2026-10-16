#include "ichor/economy/catalogue_distribution_logic.h"

#include "ichor/core/rng.h"

#include <algorithm>
#include <map>
#include <variant>

namespace ichor {

std::vector<DistributedItem> availableItems(const std::vector<CatalogueHome>& catalogue, const std::set<std::string>& activeAxioms)
{
    std::map<std::string, std::set<std::string>> matched;
    for (const CatalogueHome& entry : catalogue) {
        if (entry.item.empty()) continue;
        std::set<std::string>& hits = matched[entry.item];
        const std::size_t limit = std::min<std::size_t>(entry.homes.size(), kMaxDistributionHomes);
        for (std::size_t i = 0; i < limit; ++i) {
            if (activeAxioms.contains(entry.homes[i])) hits.insert(entry.homes[i]);
        }
    }
    std::vector<DistributedItem> out;
    for (const auto& [item, hits] : matched) {
        if (!hits.empty()) out.push_back({item, static_cast<std::uint32_t>(hits.size())});
    }
    return out;
}

std::vector<CatalogueHome> catalogueHomes(const DataTable* table)
{
    std::vector<CatalogueHome> out;
    if (table == nullptr) return out;
    for (const auto& [id, record] : table->records) {
        CatalogueHome entry{id, {}};
        const auto it = record.fields.find("home");
        if (it != record.fields.end()) {
            if (const auto* homes = std::get_if<std::vector<std::string>>(&it->second)) entry.homes = *homes;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

std::vector<DistributedItem> chooseWorldSubset(const std::vector<DistributedItem>& candidates, std::uint64_t seed, std::uint32_t count)
{
    std::map<std::string, std::uint32_t> pool;
    for (const DistributedItem& candidate : candidates) {
        if (!candidate.item.empty() && candidate.weight > 0) pool[candidate.item] = candidate.weight;
    }
    std::vector<DistributedItem> remaining;
    for (const auto& [item, weight] : pool) remaining.push_back({item, weight});

    Rng rng(seed);
    std::vector<DistributedItem> chosen;
    while (chosen.size() < count && !remaining.empty()) {
        std::uint64_t total = 0;
        for (const DistributedItem& entry : remaining) total += entry.weight;
        std::uint64_t roll = rng.nextU64() % total;
        std::size_t pick = 0;
        while (roll >= remaining[pick].weight) {
            roll -= remaining[pick].weight;
            ++pick;
        }
        chosen.push_back(remaining[pick]);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(pick));
    }
    std::sort(chosen.begin(), chosen.end(), [](const DistributedItem& a, const DistributedItem& b) { return a.item < b.item; });
    return chosen;
}

}
