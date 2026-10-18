#include "ichor/economy/loot_use.h"

#include <limits>

namespace ichor {

EncumbrancePenalty encumbrancePenalty(EncumbranceLevel level)
{
    switch (level) {
    case EncumbranceLevel::Light:
        return {1000, 1000, true};
    case EncumbranceLevel::Loaded:
        return {900, 1250, true};
    case EncumbranceLevel::Heavy:
        return {700, 1750, false};
    case EncumbranceLevel::Over:
        break;
    }
    return {400, 3000, false};
}

std::uint64_t scalePermille(std::uint64_t value, std::uint32_t permille)
{
    const std::uint64_t whole = value / 1000;
    const std::uint64_t rest = value % 1000;
    const std::uint64_t high = whole * permille;
    if (permille != 0 && whole > std::numeric_limits<std::uint64_t>::max() / permille) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t low = rest * permille / 1000;
    if (high > std::numeric_limits<std::uint64_t>::max() - low) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return high + low;
}

std::uint32_t ChargeLedger::remaining(std::string_view id) const
{
    const auto it = open_.find(id);
    return it == open_.end() ? 0 : it->second;
}

UseResult useConsumable(Inventory& inventory, ChargeLedger& ledger, const ConsumableDef& def)
{
    UseResult result;
    if (def.id.empty() || def.maxCharges == 0 || def.maxCharges > kMaxStackLimit) {
        result.status = UseStatus::InvalidDef;
        return result;
    }
    auto it = ledger.open_.find(def.id);
    if (it == ledger.open_.end()) {
        if (inventory.remove(def.id, 1).status != InventoryStatus::Ok) {
            result.status = UseStatus::NotHeld;
            return result;
        }
        it = ledger.open_.emplace(def.id, def.maxCharges).first;
    }
    // A def whose maxCharges shrank after opening must not leave more than it allows.
    if (it->second > def.maxCharges) {
        it->second = def.maxCharges;
    }
    --it->second;
    result.effect = def.effect;
    result.magnitude = def.magnitude;
    result.chargesLeft = it->second;
    if (it->second == 0) {
        ledger.open_.erase(it);
        result.itemSpent = true;
    }
    return result;
}

bool lootTableValid(const std::vector<LootEntry>& table)
{
    if (table.empty() || table.size() > kMaxLootEntries) {
        return false;
    }
    for (const LootEntry& e : table) {
        if (e.itemId.empty() || e.weight == 0 || e.minCount == 0 || e.minCount > e.maxCount) {
            return false;
        }
    }
    return true;
}

std::vector<LootDrop> rollLoot(const std::vector<LootEntry>& table, std::uint32_t rolls, Rng& rng)
{
    std::vector<LootDrop> drops;
    if (!lootTableValid(table)) {
        return drops;
    }
    if (rolls > kMaxLootRolls) {
        rolls = kMaxLootRolls;
    }
    std::uint64_t total = 0;
    for (const LootEntry& e : table) {
        total += e.weight;
    }
    for (std::uint32_t i = 0; i < rolls; ++i) {
        std::uint64_t pick = rng.nextU64() % total;
        const LootEntry* chosen = &table.back();
        for (const LootEntry& e : table) {
            if (pick < e.weight) {
                chosen = &e;
                break;
            }
            pick -= e.weight;
        }
        const std::uint32_t count = rng.range(chosen->minCount, chosen->maxCount);
        bool merged = false;
        for (LootDrop& d : drops) {
            if (d.itemId == chosen->itemId) {
                d.count = count > std::numeric_limits<std::uint32_t>::max() - d.count
                              ? std::numeric_limits<std::uint32_t>::max()
                              : d.count + count;
                merged = true;
                break;
            }
        }
        if (!merged) {
            drops.push_back({chosen->itemId, count});
        }
    }
    return drops;
}

CollectResult collectLoot(Inventory& inventory, const std::vector<LootDrop>& drops)
{
    CollectResult result;
    for (const LootDrop& d : drops) {
        if (inventory.add(d.itemId, d.count).status == InventoryStatus::Ok) {
            result.taken.push_back(d);
        } else {
            result.leftBehind.push_back(d);
        }
    }
    return result;
}

}
