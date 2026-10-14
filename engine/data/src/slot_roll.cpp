#include "ichor/data/slot_roll.h"

#include "ichor/core/rng.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace ichor {

namespace {

constexpr std::array<AxiomSlot, 5> kSlots = {AxiomSlot::Cataclysm, AxiomSlot::Law, AxiomSlot::Powers, AxiomSlot::Pressure, AxiomSlot::Anomaly};

SlotRollResult failure(SlotRollError error, AxiomSlot slot, std::string message)
{
    SlotRollResult result;
    result.error = error;
    result.errorSlot = slot;
    result.message = std::move(message);
    return result;
}

}

bool axiomSlotShownAtCreation(AxiomSlot slot)
{
    return slot == AxiomSlot::Cataclysm || slot == AxiomSlot::Law || slot == AxiomSlot::Powers;
}

const SlotAssignment* SlotRollResult::find(AxiomSlot slot) const
{
    for (const SlotAssignment& assignment : slots) {
        if (assignment.slot == slot) {
            return &assignment;
        }
    }
    return nullptr;
}

SlotRollResult rollSlots(std::uint64_t worldSeed, std::span<const Axiom> pool, const AxiomCoexist& mayCoexist)
{
    std::vector<const Axiom*> sorted;
    sorted.reserve(pool.size());
    for (const Axiom& axiom : pool) {
        sorted.push_back(&axiom);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Axiom* a, const Axiom* b) { return a->id < b->id; });
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i]->id == sorted[i - 1]->id) {
            return failure(SlotRollError::DuplicateId, sorted[i]->slot, "duplicate axiom id '" + sorted[i]->id + "'");
        }
    }

    SlotRollResult result;
    std::vector<const Axiom*> picked;
    for (const AxiomSlot slot : kSlots) {
        Rng rng(hashCombine(worldSeed, static_cast<std::uint64_t>(slot)));
        if (slot == AxiomSlot::Anomaly && rng.range(0, kAnomalyOneInN - 1) != 0) {
            break;
        }

        std::vector<const Axiom*> candidates;
        bool any = false;
        for (const Axiom* axiom : sorted) {
            if (axiom->slot != slot) {
                continue;
            }
            any = true;
            const bool allowed = !mayCoexist || std::all_of(picked.begin(), picked.end(), [&](const Axiom* chosen) { return mayCoexist(*chosen, *axiom); });
            if (allowed) {
                candidates.push_back(axiom);
            }
        }
        if (!any) {
            if (slot == AxiomSlot::Anomaly) {
                break;
            }
            return failure(SlotRollError::NoCandidates, slot, "no axiom in the pool for slot " + std::string(1, axiomSlotLetter(slot)));
        }
        if (candidates.empty()) {
            if (slot == AxiomSlot::Anomaly) {
                break;
            }
            return failure(SlotRollError::AllRejected, slot, "every axiom for slot " + std::string(1, axiomSlotLetter(slot)) + " clashes with an earlier pick");
        }

        const Axiom* chosen = candidates[rng.range(0, static_cast<std::uint32_t>(candidates.size() - 1))];
        picked.push_back(chosen);
        result.slots.push_back({slot, chosen->id, axiomSlotShownAtCreation(slot)});
    }
    return result;
}

}
