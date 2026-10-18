#pragma once

#include "ichor/core/rng.h"
#include "ichor/economy/inventory_model.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 06: encumbrance is where looting becomes a decision. Each level scales
// movement and stamina; all values are permille so the sim stays integer.
struct EncumbrancePenalty {
    std::uint32_t movePermille = 1000;       // share of base move speed kept
    std::uint32_t staminaCostPermille = 1000; // multiplier on stamina spent
    bool canSprint = true;
};

EncumbrancePenalty encumbrancePenalty(EncumbranceLevel level);

// value * permille / 1000, rounded down, with no overflow for any value.
std::uint64_t scalePermille(std::uint64_t value, std::uint32_t permille);

struct ConsumableDef {
    std::string id;
    std::uint32_t maxCharges = 1; // 1 to kMaxStackLimit
    std::string effect;
    std::uint32_t magnitude = 0;
};

enum class UseStatus : std::uint8_t { Ok, NotHeld, InvalidDef };

struct UseResult {
    UseStatus status = UseStatus::Ok;
    std::string effect;
    std::uint32_t magnitude = 0;
    std::uint32_t chargesLeft = 0;
    bool itemSpent = false;
};

// Charges left on the one opened item of each consumable id. Opening takes a
// whole item from the inventory; the item is spent when its last charge goes.
class ChargeLedger {
public:
    std::uint32_t remaining(std::string_view id) const;

private:
    friend UseResult useConsumable(Inventory&, ChargeLedger&, const ConsumableDef&);
    std::map<std::string, std::uint32_t, std::less<>> open_;
};

// Spends one charge. With none open it opens a held item first; with none held
// it changes nothing and reports NotHeld.
UseResult useConsumable(Inventory& inventory, ChargeLedger& ledger, const ConsumableDef& def);

struct LootEntry {
    std::string itemId;
    std::uint32_t weight = 1; // relative chance, integer
    std::uint32_t minCount = 1;
    std::uint32_t maxCount = 1;
};

struct LootDrop {
    std::string itemId;
    std::uint32_t count = 0;
};

// A bad table (empty, zero weight, min > max, zero count,
// empty id, more than kMaxLootEntries entries) yields no drops rather than a partial roll.
constexpr std::size_t kMaxLootEntries = 4096;
constexpr std::uint32_t kMaxLootRolls = 1024;

bool lootTableValid(const std::vector<LootEntry>& table);

// Deterministic: the same table, rolls and Rng state give the same drops.
// Rolls above kMaxLootRolls are clamped to it. Identical item ids are merged, in first-drawn order.
std::vector<LootDrop> rollLoot(const std::vector<LootEntry>& table, std::uint32_t rolls, Rng& rng);

struct CollectResult {
    std::vector<LootDrop> taken;
    std::vector<LootDrop> leftBehind; // what the load or a full stack refused
};

// Takes each drop whole if it fits, else leaves it behind: the player's
// decision, not a walk back.
CollectResult collectLoot(Inventory& inventory, const std::vector<LootDrop>& drops);

}
