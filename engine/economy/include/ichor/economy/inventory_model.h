#pragma once

#include "ichor/data/data_set.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 06 and doc 13: weight is the system that makes looting a decision, so it
// is kept in exact integer units (a thousandth of the catalogue unit) and
// every sum is checked. Floating point appears once, in weightToUnits.
constexpr std::uint64_t kWeightUnitsPerWeight = 1000;
constexpr std::uint64_t kMaxItemWeightUnits = 1'000'000; // 1000 catalogue weight units
constexpr std::uint32_t kMaxStackLimit = 1'000'000;

// Rejects NaN, infinity, negatives and anything above kMaxItemWeightUnits,
// so nothing reaches a cast or a multiply unchecked.
std::optional<std::uint64_t> weightToUnits(double weight);

// The most of one item the inventory holds, by catalogue class. The catalogue
// names no stack size, so this is the one place the rule lives.
struct StackRules {
    std::uint32_t consumable = 5;
    std::uint32_t material = 10;
    std::uint32_t information = 1;
    std::uint32_t gear = 1;
    std::uint32_t coin = 100;
};

struct ItemDef {
    std::string id;
    std::string itemClass;
    std::uint64_t weightUnits = 0;
    std::uint32_t stackLimit = 1;
};

// The catalogue table (data/catalogue/*.json) reduced to what the inventory
// needs. A record with a missing or invalid weight, or an unknown class, fails
// the whole load with an error naming it. Stack limits must be 1 to kMaxStackLimit.
class ItemCatalogue {
public:
    static std::optional<ItemCatalogue> fromTable(const DataTable& table, const StackRules& rules, std::string& error);

    const ItemDef* find(std::string_view id) const;
    std::size_t size() const { return items_.size(); }

private:
    ItemCatalogue() = default;

    std::map<std::string, ItemDef, std::less<>> items_;
};

// Where the load reads at a glance. The thresholds are permille of capacity;
// a total exactly on a threshold belongs to the higher level, and exactly at
// capacity is still Heavy. Over means strictly past capacity.
enum class EncumbranceLevel : std::uint8_t { Light, Loaded, Heavy, Over };

struct EncumbranceThresholds {
    std::uint32_t loadedPermille = 500;
    std::uint32_t heavyPermille = 800;

    bool valid() const { return loadedPermille > 0 && loadedPermille < heavyPermille && heavyPermille <= 1000; }
};

const char* encumbranceName(EncumbranceLevel level);

// An invalid threshold set is treated as the defaults.
EncumbranceLevel encumbranceLevel(std::uint64_t totalUnits, std::uint64_t capacityUnits,
                                  const EncumbranceThresholds& thresholds = {});

enum class InventoryStatus : std::uint8_t { Ok, UnknownItem, ZeroCount, StackFull, OverCapacity, Overflow, NotHeld };

struct InventoryResult {
    InventoryStatus status = InventoryStatus::Ok;
    std::uint32_t changed = 0; // all or nothing: the count when Ok, else 0
};

struct InventoryRow {
    std::string id;
    std::uint32_t count = 0;
    std::uint64_t unitWeightUnits = 0;
    std::uint64_t totalWeightUnits = 0;
};

enum class InventorySort : std::uint8_t { ById, ByWeightDescending, ByCountDescending };

// One stack per item id, capped at the item's stack limit. The catalogue must
// outlive the inventory. add refuses anything that would pass capacity;
// addForced is for rewards the player cannot refuse and may take the load into
// Over. Both refuse a change that would break a stack limit or overflow, and
// every operation is all or nothing.
class Inventory {
public:
    Inventory(const ItemCatalogue& catalogue, std::uint64_t capacityUnits);

    InventoryResult add(std::string_view id, std::uint32_t count) { return addImpl(id, count, false); }
    InventoryResult addForced(std::string_view id, std::uint32_t count) { return addImpl(id, count, true); }
    InventoryResult remove(std::string_view id, std::uint32_t count);

    std::uint32_t count(std::string_view id) const;
    std::uint64_t totalWeightUnits() const { return total_; }
    std::uint64_t capacityUnits() const { return capacity_; }
    void setCapacity(std::uint64_t capacityUnits) { capacity_ = capacityUnits; }

    EncumbranceLevel level(const EncumbranceThresholds& thresholds = {}) const
    {
        return encumbranceLevel(total_, capacity_, thresholds);
    }

    std::vector<InventoryRow> rows(InventorySort sort = InventorySort::ById) const;

private:
    InventoryResult addImpl(std::string_view id, std::uint32_t count, bool force);

    const ItemCatalogue* catalogue_;
    std::uint64_t capacity_;
    std::uint64_t total_ = 0;
    std::map<std::string, std::uint32_t, std::less<>> held_;
};

}
