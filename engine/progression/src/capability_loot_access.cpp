#include "ichor/progression/capability_loot_access.h"

#include <algorithm>
#include <map>
#include <set>

namespace ichor {

namespace {

constexpr std::size_t kMaxId = 64;

using IdSet = std::set<std::string, std::less<>>;

bool isWord(std::string_view text)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    return !text.empty() && text.size() <= kMaxId && lower(text[0])
        && std::all_of(text.begin(), text.end(), [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_'; });
}

IdSet knownIds(const std::vector<Capability>& capabilities)
{
    IdSet ids;
    for (const Capability& c : capabilities) ids.insert(c.id);
    return ids;
}

void add(std::vector<std::string>& problems, std::size_t index, const std::string& field, const std::string& what)
{
    problems.push_back("capability loot: [" + std::to_string(index) + "]" + field + ": " + what);
}

}

bool validateGearGrants(const std::vector<GearGrant>& grants, const std::vector<Capability>& capabilities, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    if (grants.size() > kMaxLootGrants) {
        problems.push_back("capability loot: more than " + std::to_string(kMaxLootGrants) + " items");
        return false;
    }
    const IdSet known = knownIds(capabilities);
    IdSet items;
    for (std::size_t i = 0; i < grants.size(); ++i) {
        const GearGrant& g = grants[i];
        if (!isWord(g.item)) {
            add(problems, i, ".item", "must be a lowercase word of at most 64 bytes");
        } else if (!items.insert(g.item).second) {
            add(problems, i, ".item", "repeats an earlier item");
        }
        if (g.grants.empty()) {
            add(problems, i, ".grants", "grants nothing");
        } else if (g.grants.size() > kMaxLootListed) {
            add(problems, i, ".grants", "more than " + std::to_string(kMaxLootListed) + " grants");
            continue;
        }
        IdSet seen;
        for (std::size_t k = 0; k < g.grants.size(); ++k) {
            const std::string path = ".grants[" + std::to_string(k) + "]";
            if (!known.contains(g.grants[k])) {
                add(problems, i, path, "names no capability");
            } else if (!seen.insert(g.grants[k]).second) {
                add(problems, i, path, "repeats an earlier grant");
            }
        }
    }
    return problems.size() == before;
}

bool validateMapRegions(const std::vector<MapRegion>& regions, const std::vector<Capability>& capabilities, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    if (regions.size() > kMaxLootRegions) {
        problems.push_back("capability loot: more than " + std::to_string(kMaxLootRegions) + " regions");
        return false;
    }
    const IdSet known = knownIds(capabilities);
    IdSet ids;
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const MapRegion& r = regions[i];
        if (!isWord(r.id)) {
            add(problems, i, ".id", "must be a lowercase word of at most 64 bytes");
        } else if (!ids.insert(r.id).second) {
            add(problems, i, ".id", "repeats an earlier region");
        }
        if (r.needs.size() > kMaxLootListed) {
            add(problems, i, ".needs", "more than " + std::to_string(kMaxLootListed) + " needs");
            continue;
        }
        for (std::size_t k = 0; k < r.needs.size(); ++k) {
            if (!known.contains(r.needs[k])) add(problems, i, ".needs[" + std::to_string(k) + "]", "names no capability");
        }
    }
    return problems.size() == before;
}

std::vector<std::string> effectiveCapabilities(const std::vector<GearGrant>& grants, const std::vector<Capability>& capabilities, const std::vector<std::string>& baseline, const std::vector<std::string>& items)
{
    std::map<std::string, const Capability*, std::less<>> table;
    for (const Capability& c : capabilities) table.emplace(c.id, &c);

    IdSet held;
    for (const std::string& id : baseline) {
        if (table.contains(id)) held.insert(id);
    }

    std::map<std::string, const GearGrant*, std::less<>> byItem;
    for (const GearGrant& g : grants) byItem.emplace(g.item, &g);
    const std::size_t read = std::min(items.size(), kMaxLootListed);
    for (std::size_t i = 0; i < read; ++i) {
        const auto it = byItem.find(items[i]);
        if (it == byItem.end()) continue;
        const std::size_t count = std::min(it->second->grants.size(), kMaxLootListed);
        for (std::size_t k = 0; k < count; ++k) {
            if (table.contains(it->second->grants[k])) held.insert(it->second->grants[k]);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = held.begin(); it != held.end();) {
            const Capability& c = *table.find(*it)->second;
            const bool met = std::all_of(c.prerequisites.begin(), c.prerequisites.end(), [&](const std::string& p) { return !table.contains(p) || held.contains(p); });
            if (met) {
                ++it;
            } else {
                it = held.erase(it);
                changed = true;
            }
        }
    }
    return {held.begin(), held.end()};
}

std::vector<RegionAccess> assessRegions(const std::vector<MapRegion>& regions, const std::vector<std::string>& held)
{
    const IdSet have(held.begin(), held.end());
    std::vector<RegionAccess> out;
    out.reserve(std::min(regions.size(), kMaxLootRegions));
    for (std::size_t i = 0; i < regions.size() && i < kMaxLootRegions; ++i) {
        RegionAccess access;
        access.id = regions[i].id;
        IdSet missing;
        const bool oversized = regions[i].needs.size() > kMaxLootListed;
        if (!oversized) {
            for (const std::string& need : regions[i].needs) {
                if (!have.contains(need)) missing.insert(need);
            }
        }
        access.reachable = !oversized && missing.empty();
        access.missing.assign(missing.begin(), missing.end());
        out.push_back(std::move(access));
    }
    return out;
}

std::vector<std::string> reachableRegionIds(const std::vector<MapRegion>& regions, const std::vector<std::string>& held)
{
    std::vector<std::string> ids;
    for (const RegionAccess& access : assessRegions(regions, held)) {
        if (access.reachable) ids.push_back(access.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

}
