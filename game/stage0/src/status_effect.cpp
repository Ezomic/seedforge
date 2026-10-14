#include "ichor/stage0/status_effect.h"

#include <algorithm>
#include <array>

namespace ichor::stage0 {

namespace {

constexpr std::array<StatusDef, 2> kDefs{{
    {StatusId::Bleed, 180, Stacking::Stack, 3, 400, 0},
    {StatusId::Slow, 240, Stacking::Refresh, 1, 0, 350},
}};

}

const StatusDef* statusDef(StatusId id)
{
    for (const StatusDef& def : kDefs) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

const StatusEffect* StatusSet::find(StatusId id) const
{
    for (const StatusEffect& e : effects) {
        if (e.id == static_cast<std::uint8_t>(id)) return &e;
    }
    return nullptr;
}

bool StatusSet::apply(StatusId id, std::int32_t durationTicks)
{
    const StatusDef* def = statusDef(id);
    if (def == nullptr || durationTicks < 0) return false;
    const std::int32_t duration = std::min(durationTicks == 0 ? def->durationTicks : durationTicks, kMaxStatusDurationTicks);
    if (duration <= 0) return false;

    const auto key = static_cast<std::uint8_t>(id);
    const auto at = std::lower_bound(effects.begin(), effects.end(), key, [](const StatusEffect& e, std::uint8_t k) { return e.id < k; });
    if (at != effects.end() && at->id == key) {
        at->remainingTicks = std::max(at->remainingTicks, duration);
        if (def->stacking == Stacking::Stack) at->stacks = std::min(at->stacks + 1, std::max(def->maxStacks, 1));
        return true;
    }
    effects.insert(at, StatusEffect{key, 1, duration});
    return true;
}

std::int64_t StatusSet::tick()
{
    std::int64_t damage = 0;
    for (StatusEffect& e : effects) {
        const StatusDef* def = statusDef(static_cast<StatusId>(e.id));
        // Loaded state is data: out-of-range stacks and durations are pulled back in before use.
        e.stacks = std::clamp(e.stacks, 1, def != nullptr ? std::max(def->maxStacks, 1) : 1);
        if (def != nullptr) damage += static_cast<std::int64_t>(def->damageMilliPerTick) * e.stacks;
        e.remainingTicks = std::min(e.remainingTicks, kMaxStatusDurationTicks);
        if (e.remainingTicks > 0) --e.remainingTicks;
    }
    std::erase_if(effects, [](const StatusEffect& e) { return e.remainingTicks <= 0; });
    return damage;
}

float StatusSet::moveScale() const
{
    std::int64_t reduce = 0;
    for (const StatusEffect& e : effects) {
        const StatusDef* def = statusDef(static_cast<StatusId>(e.id));
        if (def != nullptr) reduce += static_cast<std::int64_t>(def->speedReducePermille) * e.stacks;
    }
    return static_cast<float>(std::clamp<std::int64_t>(1000 - reduce, 0, 1000)) / 1000.0f;
}

}
