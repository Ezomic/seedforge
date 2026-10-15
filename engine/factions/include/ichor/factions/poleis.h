#pragma once

#include "ichor/factions/incumbent.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

constexpr std::uint32_t kMaxIncumbentScale = 1u << 20;

struct IncumbentShare {
    std::string id;
    // This Incumbent's slice of the full-scale budget.
    std::uint32_t scale = 0;
};

struct IncumbentScaling {
    IncumbentShape shape = IncumbentShape::Single;
    // Shares sum to the full scale: one Incumbent holds all of it, the Poleis
    // split it evenly (the first polis takes any remainder), the Dark Age has none.
    std::vector<IncumbentShare> shares;
    // Poleis only: ids of neighbours along the horizontal map, in plan order.
    // Each pair is in constant low-grade war.
    std::vector<std::pair<std::string, std::string>> wars;
};

// Splits `fullScale` over the plan's Incumbents. Returns nullopt when
// `fullScale` exceeds kMaxIncumbentScale. Pure and deterministic.
std::optional<IncumbentScaling> scaleIncumbents(const IncumbentPlan& plan, std::uint32_t fullScale);

// Finds the single Powers axiom among a world's axiom ids (other slots are
// ignored), plans its Incumbents and scales them. Returns nullopt when there
// is no Powers axiom, more than one, an unknown one, a bad Poleis count or an
// oversized `fullScale`.
std::optional<IncumbentScaling> scaleIncumbentsForAxioms(const std::vector<std::string>& axiomIds, int poleisCount, std::uint32_t fullScale);

}
