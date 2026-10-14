#pragma once

#include "ichor/data/axiom.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace ichor {

// Doc 01: slots A-C are shown at world creation, D and E stay hidden.
bool axiomSlotShownAtCreation(AxiomSlot slot);

// Roughly one seed in three rolls an Anomaly (doc 01, slot E).
constexpr std::uint32_t kAnomalyOneInN = 3;

struct SlotAssignment {
    AxiomSlot slot = AxiomSlot::Cataclysm;
    std::string axiomId;
    bool shown = false;
};

enum class SlotRollError {
    None,
    DuplicateId,
    // Slot A-D has no axiom in the pool at all.
    NoCandidates,
    // The pool has axioms for slot B-D but every one clashes with an earlier pick.
    // Slot E is optional, so there a total clash just means no Anomaly.
    AllRejected,
};

struct SlotRollResult {
    SlotRollError error = SlotRollError::None;
    // Which slot failed, or the slot of the duplicated id; meaningful when error != None.
    AxiomSlot errorSlot = AxiomSlot::Cataclysm;
    std::string message;
    // In slot order A..E. A to D are always present on success; E is absent on
    // the seeds that roll no Anomaly (and when the pool authors none).
    std::vector<SlotAssignment> slots;

    bool ok() const { return error == SlotRollError::None; }
    const SlotAssignment* find(AxiomSlot slot) const;
};

// Whether `candidate` may share a world with `chosen`, an axiom already picked
// for an earlier slot. The seam for conflict avoidance (SEED-22).
using AxiomCoexist = std::function<bool(const Axiom& chosen, const Axiom& candidate)>;

// Rolls the slots in order A..E from the world seed. Each slot draws from its
// own generator seeded by (worldSeed, slot) over the pool's axioms of that slot
// sorted by id, so the result does not depend on pool order, and without a
// predicate no slot depends on another. A null `mayCoexist` allows every pair.
// Candidates are filtered by the predicate before the draw, so it always terminates.
SlotRollResult rollSlots(std::uint64_t worldSeed, std::span<const Axiom> pool, const AxiomCoexist& mayCoexist = {});

}
