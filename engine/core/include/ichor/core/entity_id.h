#pragma once

#include "ichor/core/hash.h"

#include <cstdint>

namespace ichor {

using EntityId = std::uint64_t;

// doc 14: an ID is a function of world structure only, never of spawn order,
// so a saved delta ("chest 47 is looted") always resolves to the same chest.
// Generated entities (chests, loot, residents) use this; StableId::of(name, index)
// stays for hand-named scene objects. Each step re-mixes, so swapping two inputs or repeating one (seed == poiId)
// cannot cancel out. Callers with a string POI id pass hash64(name).
constexpr EntityId entityId(std::uint64_t worldSeed, std::uint64_t poiId, std::uint32_t roomIndex,
    std::uint32_t slotIndex)
{
    std::uint64_t h = mix64(worldSeed);
    h = mix64(h ^ poiId);
    h = mix64(h ^ roomIndex);
    return mix64(h ^ slotIndex);
}

}
