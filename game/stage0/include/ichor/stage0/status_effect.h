#pragma once

#include "ichor/core/reflect.h"

#include <cstdint>
#include <vector>

namespace ichor::stage0 {

enum class StatusId : std::uint8_t { None, Bleed, Slow };
enum class Stacking : std::uint8_t { Refresh, Stack };

// Static rules for one effect. All numbers are integers so the tick is exact.
struct StatusDef {
    StatusId id = StatusId::None;
    std::int32_t durationTicks = 0;
    Stacking stacking = Stacking::Refresh;
    std::int32_t maxStacks = 1;
    std::int32_t damageMilliPerTick = 0;      // per stack, thousandths of a health point
    std::int32_t speedReducePermille = 0;     // per stack, off the movement speed
};

const StatusDef* statusDef(StatusId id);

constexpr std::int32_t kMaxStatusDurationTicks = 60 * 60 * 10;

struct StatusEffect {
    std::uint8_t id = 0;
    std::int32_t stacks = 1;
    std::int32_t remainingTicks = 0;
};

// One instance per id, kept sorted by id, so ticking order never depends on who applied what first.
struct StatusSet {
    std::vector<StatusEffect> effects;

    // A non-positive duration, or an unknown id, applies nothing. durationTicks 0 means the definition's own.
    bool apply(StatusId id, std::int32_t durationTicks = 0);
    // One fixed step: returns the damage in thousandths of a health point, then ages and expires.
    std::int64_t tick();
    // 1.0 untouched, 0.0 rooted.
    float moveScale() const;
    const StatusEffect* find(StatusId id) const;
    bool empty() const { return effects.empty(); }
};

}

namespace ichor {

template <>
struct Reflect<stage0::StatusEffect> {
    static constexpr auto fields()
    {
        using S = stage0::StatusEffect;
        return std::make_tuple(field("id", &S::id), field("stacks", &S::stacks), field("remainingTicks", &S::remainingTicks));
    }
};

template <>
struct Reflect<stage0::StatusSet> {
    static constexpr auto fields()
    {
        using S = stage0::StatusSet;
        return std::make_tuple(field("effects", &S::effects));
    }
};

}
