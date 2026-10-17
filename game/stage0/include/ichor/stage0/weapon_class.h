#pragma once

#include "ichor/data/data_set.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ichor::stage0 {

constexpr std::int32_t kMaxMoveTicks = 240;
constexpr std::int32_t kMaxMoveStamina = 200;
constexpr std::int32_t kMaxMovePoise = 1000;
constexpr std::int32_t kMaxReachPermille = 50000;

// The moveset parameters shared by every weapon of one class. Individual weapons scale these with their own stats.
// All times are fixed steps; reach is in permille of the baseline one-handed reach.
struct WeaponClass {
    std::string id;
    std::int32_t windupTicks = 0;
    std::int32_t strikeTicks = 0;
    std::int32_t recoveryTicks = 0;
    std::int32_t staminaCost = 0;
    std::int32_t poiseDamage = 0;
    std::int32_t reachPermille = 0;
    bool ranged = false;

    // Windup, strike and recovery in full: there are no cancels, so this is the whole commitment.
    std::int32_t commitmentTicks() const { return windupTicks + strikeTicks + recoveryTicks; }
};

// Rejects a record with a missing, non-numeric, non-finite, fractional or out-of-range field instead of clamping it.
std::optional<WeaponClass> weaponClassFromRecord(const DataRecord& record);

// Every valid record of the "weapon_classes" table, in id order. Invalid records are skipped.
std::vector<WeaponClass> weaponClasses(const DataSet& data);

// The class a weapon record names in "weapon_class", or nullopt when it names none or an unknown one.
std::optional<WeaponClass> classOfWeapon(const DataSet& data, const DataRecord& weapon);

}
