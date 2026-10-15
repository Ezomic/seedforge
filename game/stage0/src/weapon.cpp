#include "ichor/stage0/weapon.h"

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

const double* number(const DataRecord& record, const char* key)
{
    const auto it = record.fields.find(key);
    return it == record.fields.end() ? nullptr : std::get_if<double>(&it->second);
}

}

WeaponProfile weaponFromRecord(const DataRecord& record)
{
    WeaponProfile weapon;
    const double* window = number(record, "parry_window_ticks");
    if (window == nullptr || !std::isfinite(*window) || *window < 1.0) {
        return weapon;
    }
    weapon.parryWindowTicks = static_cast<std::int32_t>(std::min(*window, static_cast<double>(kMaxParryWindowTicks)));
    const double* poise = number(record, "parry_poise_damage");
    if (poise != nullptr && std::isfinite(*poise) && *poise >= 0.0) {
        weapon.parryPoiseDamage = static_cast<float>(std::min(*poise, 1000.0));
    }
    return weapon;
}

}
