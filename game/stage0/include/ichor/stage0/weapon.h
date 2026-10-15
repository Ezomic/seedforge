#pragma once

#include "ichor/data/data_set.h"

#include <cstdint>

namespace ichor::stage0 {

constexpr std::int32_t kMaxParryWindowTicks = 30;

// The combat-facing part of a catalogue weapon record. Parry is off unless the record opts in.
struct WeaponProfile {
    std::int32_t parryWindowTicks = 0; // fixed steps after raising the guard in which a hit is parried; 0 means no parry
    float parryPoiseDamage = 100.0f;   // poise the attacker loses to a parry

    bool parries() const { return parryWindowTicks > 0; }
};

// Reads "parry_window_ticks" and "parry_poise_damage". A missing, non-numeric or non-positive window leaves parry off.
WeaponProfile weaponFromRecord(const DataRecord& record);

}
