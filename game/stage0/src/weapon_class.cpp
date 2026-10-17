#include "ichor/stage0/weapon_class.h"

#include <cmath>

namespace ichor::stage0 {

namespace {

bool integer(const DataRecord& record, const char* key, std::int32_t lo, std::int32_t hi, std::int32_t& out)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end()) {
        return false;
    }
    const double* value = std::get_if<double>(&it->second);
    if (value == nullptr || !std::isfinite(*value) || *value < lo || *value > hi || *value != std::floor(*value)) {
        return false;
    }
    out = static_cast<std::int32_t>(*value);
    return true;
}

}

std::optional<WeaponClass> weaponClassFromRecord(const DataRecord& record)
{
    WeaponClass cls;
    cls.id = record.id;
    if (cls.id.empty() || !integer(record, "windup_ticks", 1, kMaxMoveTicks, cls.windupTicks)
        || !integer(record, "strike_ticks", 1, kMaxMoveTicks, cls.strikeTicks)
        || !integer(record, "recovery_ticks", 1, kMaxMoveTicks, cls.recoveryTicks)
        || !integer(record, "stamina_cost", 0, kMaxMoveStamina, cls.staminaCost)
        || !integer(record, "poise_damage", 0, kMaxMovePoise, cls.poiseDamage)
        || !integer(record, "reach_permille", 1, kMaxReachPermille, cls.reachPermille)) {
        return std::nullopt;
    }
    const auto ranged = record.fields.find("ranged");
    if (ranged != record.fields.end()) {
        const bool* flag = std::get_if<bool>(&ranged->second);
        if (flag == nullptr) {
            return std::nullopt;
        }
        cls.ranged = *flag;
    }
    return cls;
}

std::vector<WeaponClass> weaponClasses(const DataSet& data)
{
    std::vector<WeaponClass> out;
    const DataTable* table = data.table("weapon_classes");
    if (table == nullptr) {
        return out;
    }
    for (const auto& [id, record] : table->records) {
        if (auto cls = weaponClassFromRecord(record)) {
            out.push_back(std::move(*cls));
        }
    }
    return out;
}

std::optional<WeaponClass> classOfWeapon(const DataSet& data, const DataRecord& weapon)
{
    const auto it = weapon.fields.find("weapon_class");
    if (it == weapon.fields.end()) {
        return std::nullopt;
    }
    const std::string* name = std::get_if<std::string>(&it->second);
    const DataTable* table = data.table("weapon_classes");
    if (name == nullptr || table == nullptr) {
        return std::nullopt;
    }
    const auto found = table->records.find(*name);
    return found == table->records.end() ? std::nullopt : weaponClassFromRecord(found->second);
}

}
