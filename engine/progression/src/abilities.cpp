#include "ichor/progression/abilities.h"

namespace ichor {

bool abilityDefValid(const AbilityDef& def)
{
    return !def.id.empty() && def.cost >= 1 && def.cost <= kMaxAbilityCost && def.effect >= 0
        && def.effect <= kMaxAbilityEffect;
}

AbilityResult resolveAbility(const AbilityDef& def, PowerSource& source)
{
    if (!abilityDefValid(def))
        return {AbilityOutcome::InvalidDef, 0};
    if (!source.pay(def.cost))
        return {AbilityOutcome::Unaffordable, 0};
    return {AbilityOutcome::Resolved, def.effect};
}

ChargeSource::ChargeSource(std::int64_t charges)
    : charges_(charges > 0 ? charges : 0)
{
}

bool ChargeSource::canPay(std::int64_t cost) const
{
    return cost >= 1 && cost <= charges_;
}

bool ChargeSource::pay(std::int64_t cost)
{
    if (!canPay(cost))
        return false;
    charges_ -= cost;
    return true;
}

HealthSource::HealthSource(std::int64_t health)
    : health_(health > 0 ? health : 0)
{
}

bool HealthSource::canPay(std::int64_t cost) const
{
    return cost >= 1 && cost < health_;
}

bool HealthSource::pay(std::int64_t cost)
{
    if (!canPay(cost))
        return false;
    health_ -= cost;
    return true;
}

} // namespace ichor
