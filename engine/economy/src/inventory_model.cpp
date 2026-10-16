#include "ichor/economy/inventory_model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ichor {

namespace {

constexpr std::uint64_t kMaxU64 = std::numeric_limits<std::uint64_t>::max();

bool addChecked(std::uint64_t a, std::uint64_t b, std::uint64_t& out)
{
    if (a > kMaxU64 - b) return false;
    out = a + b;
    return true;
}

// The smallest total at or above permille/1000 of capacity, rounded up so a
// threshold never fires early. Split so no product passes 64 bits.
std::uint64_t thresholdUnits(std::uint64_t capacity, std::uint32_t permille)
{
    const std::uint64_t whole = capacity / 1000 * permille;
    const std::uint64_t rest = (capacity % 1000) * permille;
    return whole + (rest + 999) / 1000;
}

const char* classKey(const DataRecord& record)
{
    const auto it = record.fields.find("class");
    const auto* value = it == record.fields.end() ? nullptr : std::get_if<std::string>(&it->second);
    return value == nullptr ? nullptr : value->c_str();
}

std::uint32_t stackLimitFor(const std::string& itemClass, const StackRules& rules)
{
    if (itemClass == "consumable") return rules.consumable;
    if (itemClass == "material") return rules.material;
    if (itemClass == "information") return rules.information;
    if (itemClass == "gear") return rules.gear;
    if (itemClass == "coin") return rules.coin;
    return 0;
}

bool limitsValid(const StackRules& rules)
{
    for (const std::uint32_t limit : {rules.consumable, rules.material, rules.information, rules.gear, rules.coin}) {
        if (limit == 0 || limit > kMaxStackLimit) return false;
    }
    return true;
}

}

std::optional<std::uint64_t> weightToUnits(double weight)
{
    if (!std::isfinite(weight) || weight < 0.0) return std::nullopt;
    const double max = static_cast<double>(kMaxItemWeightUnits) / static_cast<double>(kWeightUnitsPerWeight);
    if (weight > max) return std::nullopt;
    const auto units = static_cast<std::uint64_t>(weight * static_cast<double>(kWeightUnitsPerWeight) + 0.5);
    if (units > kMaxItemWeightUnits) return std::nullopt;
    return units;
}

std::optional<ItemCatalogue> ItemCatalogue::fromTable(const DataTable& table, const StackRules& rules, std::string& error)
{
    if (!limitsValid(rules)) {
        error = "stack limits must be 1 to " + std::to_string(kMaxStackLimit);
        return std::nullopt;
    }
    ItemCatalogue catalogue;
    for (const auto& [id, record] : table.records) {
        const char* itemClass = classKey(record);
        if (itemClass == nullptr || stackLimitFor(itemClass, rules) == 0) {
            error = "item '" + id + "': \"class\" must be consumable, material, information, gear or coin";
            return std::nullopt;
        }
        const auto weightField = record.fields.find("weight");
        const auto* weight = weightField == record.fields.end() ? nullptr : std::get_if<double>(&weightField->second);
        const auto units = weight == nullptr ? std::nullopt : weightToUnits(*weight);
        if (!units) {
            error = "item '" + id + "': \"weight\" must be a finite number from 0 to 1000";
            return std::nullopt;
        }
        catalogue.items_[id] = ItemDef{id, itemClass, *units, stackLimitFor(itemClass, rules)};
    }
    return catalogue;
}

const ItemDef* ItemCatalogue::find(std::string_view id) const
{
    const auto it = items_.find(id);
    return it == items_.end() ? nullptr : &it->second;
}

const char* encumbranceName(EncumbranceLevel level)
{
    switch (level) {
    case EncumbranceLevel::Light: return "Light";
    case EncumbranceLevel::Loaded: return "Loaded";
    case EncumbranceLevel::Heavy: return "Heavy";
    case EncumbranceLevel::Over: return "Over";
    }
    return "Over";
}

EncumbranceLevel encumbranceLevel(std::uint64_t totalUnits, std::uint64_t capacityUnits,
                                  const EncumbranceThresholds& thresholds)
{
    const EncumbranceThresholds& t = thresholds.valid() ? thresholds : EncumbranceThresholds{};
    if (totalUnits > capacityUnits) return EncumbranceLevel::Over;
    if (totalUnits == 0) return EncumbranceLevel::Light;
    if (totalUnits >= thresholdUnits(capacityUnits, t.heavyPermille)) return EncumbranceLevel::Heavy;
    if (totalUnits >= thresholdUnits(capacityUnits, t.loadedPermille)) return EncumbranceLevel::Loaded;
    return EncumbranceLevel::Light;
}

Inventory::Inventory(const ItemCatalogue& catalogue, std::uint64_t capacityUnits)
    : catalogue_(&catalogue)
    , capacity_(capacityUnits)
{
}

InventoryResult Inventory::addImpl(std::string_view id, std::uint32_t count, bool force)
{
    const ItemDef* item = catalogue_->find(id);
    if (item == nullptr) return {InventoryStatus::UnknownItem, 0};
    if (count == 0) return {InventoryStatus::ZeroCount, 0};

    const auto held = held_.find(id);
    const std::uint32_t have = held == held_.end() ? 0 : held->second;
    if (count > item->stackLimit || have > item->stackLimit - count) return {InventoryStatus::StackFull, 0};

    // Bounded by kMaxItemWeightUnits * kMaxStackLimit, so this cannot wrap.
    const std::uint64_t added = item->weightUnits * count;
    std::uint64_t next = 0;
    if (!addChecked(total_, added, next)) return {InventoryStatus::Overflow, 0};
    if (!force && next > capacity_) return {InventoryStatus::OverCapacity, 0};

    total_ = next;
    if (held == held_.end()) held_.emplace(std::string(id), count);
    else held->second = have + count;
    return {InventoryStatus::Ok, count};
}

InventoryResult Inventory::remove(std::string_view id, std::uint32_t count)
{
    if (count == 0) return {InventoryStatus::ZeroCount, 0};
    const auto held = held_.find(id);
    if (held == held_.end() || held->second < count) return {InventoryStatus::NotHeld, 0};

    total_ -= catalogue_->find(id)->weightUnits * count;
    held->second -= count;
    if (held->second == 0) held_.erase(held);
    return {InventoryStatus::Ok, count};
}

std::uint32_t Inventory::count(std::string_view id) const
{
    const auto held = held_.find(id);
    return held == held_.end() ? 0 : held->second;
}

std::vector<InventoryRow> Inventory::rows(InventorySort sort) const
{
    std::vector<InventoryRow> rows;
    rows.reserve(held_.size());
    for (const auto& [id, count] : held_) {
        const std::uint64_t unit = catalogue_->find(id)->weightUnits;
        rows.push_back({id, count, unit, unit * count});
    }
    switch (sort) {
    case InventorySort::ById: break;
    case InventorySort::ByWeightDescending:
        std::stable_sort(rows.begin(), rows.end(),
                         [](const InventoryRow& a, const InventoryRow& b) { return a.totalWeightUnits > b.totalWeightUnits; });
        break;
    case InventorySort::ByCountDescending:
        std::stable_sort(rows.begin(), rows.end(),
                         [](const InventoryRow& a, const InventoryRow& b) { return a.count > b.count; });
        break;
    }
    return rows;
}

}
