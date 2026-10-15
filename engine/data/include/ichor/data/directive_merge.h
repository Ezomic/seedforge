#pragma once

#include "ichor/data/axiom.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace ichor {

// Merged numbers are clamped to [-kDirectiveMaxMagnitude, kDirectiveMaxMagnitude];
// weights to [0, kDirectiveMaxMagnitude]. Non-finite numbers count as not set.
constexpr double kDirectiveMaxMagnitude = 1.0e6;

// The world directive set (doc 08 stage 0, the WorldConfig): the merged blocks, same shape as Axiom::blocks.
struct MergedDirectives {
    std::map<std::string, AxiomBlock, std::less<>> blocks;
};

// Which slots supplied one merged field, in slot order A..E. Scalars have one
// slot; unions and weight maps list every slot that contributed an entry.
struct FieldSource {
    std::string block;
    std::string key;
    std::vector<AxiomSlot> slots;
};

struct MergeResult {
    MergedDirectives directives;
    // Sorted by block then key, one per field present in `directives`.
    std::vector<FieldSource> provenance;
    // A slot given more than one axiom; the lowest id is kept and the rest skipped.
    std::vector<std::string> problems;
};

// Doc 01 rule 2: directives merge, conflicts resolved by slot priority A > B > C > D > E.
//  - number, text: the highest slot that sets it wins.
//  - word list: union, sorted. Weight map: union by key, the highest slot wins a shared key.
//  - opposed lists (biomes inject/suppress, enemies roster_add/roster_remove, economy abundant/scarce):
//    the highest slot that mentions a word decides which list it lands in; within one axiom the
//    removing list wins.
//  - a value of another type than the highest slot's is ignored; empty lists and maps and
//    non-finite numbers count as not set. Opposed keys only take word lists.
//  - two axioms with the same slot and id are told apart by their digest.
// Missing blocks and keys are neutral. Pure, and independent of the order of `axioms`.
MergeResult mergeDirectives(std::span<const Axiom> axioms);

// Blocks (key-sorted, numbers in fixed-point with six decimals), then "|" and the provenance (quoted block.key, slot letters) as text.
std::string mergedCanonicalText(const MergeResult& result);
std::uint64_t mergedDigest(const MergeResult& result);

}
