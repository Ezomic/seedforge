#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 07 "The Law axiom decides what stronger means": for each Law, what advances a character and
// how that advance is measured. Static data and pure lookups; nothing here is wired into a
// progression screen or into the existing capability, trainer or growth code yet.

enum class LawProgressionKind { Logistics, Vitality, Initiation, Cartography, Conservation, DebtCeiling };

// How a Law's progress is counted. Count: a whole number (rite slots, debt ceiling, sanctuary
// types). GrowthPermille: a capacity grown in permille of its base, capped by power_growth.h.
// CostPermille: a reduction of what things cost, so the character gets cheaper, not stronger.
enum class ProgressionMeasure { Count, GrowthPermille, CostPermille };

struct LawProgressionDef {
    std::string_view axiomId;
    LawProgressionKind kind;
    std::string_view summary;
    std::span<const std::string_view> advancedBy; // stat ids that grow under this Law
    ProgressionMeasure measure;
    bool raisesRawPower; // false for Conservation: efficiency only
    bool sharedPool;     // true for Hecatomb: one stat is both survival and casting cost
};

// Exact, case sensitive match on the axiom id. Null for an empty, unknown or non-Law id.
const LawProgressionDef* lawProgression(std::string_view axiomId);

// All six, in doc 07 order.
std::span<const LawProgressionDef> allLawProgressions();

// The Law meanings among a list of axiom ids: input order, each at most once, everything else
// ignored. Output is bounded by the table, whatever the input length.
std::vector<const LawProgressionDef*> lawProgressionsFor(const std::vector<std::string>& axiomIds);

std::string_view lawProgressionKindName(LawProgressionKind kind);

}
