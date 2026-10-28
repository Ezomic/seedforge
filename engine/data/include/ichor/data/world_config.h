#pragma once

#include "ichor/data/axiom.h"
#include "ichor/data/conflicts.h"
#include "ichor/data/directive_merge.h"
#include "ichor/data/slot_roll.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 08 phase 0: the world's axioms, one per slot in slot order A..E, and their merged directive blocks. A default
// WorldConfig has no axioms and merges to empty blocks: the config every forge used before phase 0 existed.
struct WorldConfig {
    std::vector<SlotAssignment> slots;
    MergeResult merged;
};

struct WorldConfigResult {
    std::string error; // empty on success
    WorldConfig config;

    bool ok() const { return error.empty(); }
};

// Doc 01 rule 3: two axioms may share a world unless they share a slot, either lists the other in `conflicts`, or the
// blocklist pairs them.
bool axiomsMayCoexist(const Axiom& a, const Axiom& b, const ConflictList& blocklist);

// Rolls the slots from the world seed (rollSlots, kept apart by axiomsMayCoexist) and merges their directives.
WorldConfigResult rollWorldConfig(std::uint64_t worldSeed, std::span<const Axiom> pool, const ConflictList& blocklist);

// The axioms named in `ids`, comma separated, as a world: at most one per slot and none that may not coexist. Fewer
// than five slots is allowed. Fails on an empty list, an empty or unknown id, a repeated id, two axioms of one slot or
// a pair that may not coexist, naming the first such id.
WorldConfigResult pinWorldConfig(std::string_view ids, std::span<const Axiom> pool, const ConflictList& blocklist);

// The two above over the axioms (axioms/) and the blocklist (conflicts/conflicts.json) under `dataRoot`. Data that
// does not load is an error carrying its first problem.
WorldConfigResult loadRolledWorldConfig(std::uint64_t worldSeed, const std::filesystem::path& dataRoot);
WorldConfigResult loadPinnedWorldConfig(std::string_view ids, const std::filesystem::path& dataRoot);

// "A=id;B=id;...|" then mergedCanonicalText; the digest is hash64 of that text. The same axioms give the same text on
// every platform, whichever way they were chosen.
std::string worldConfigCanonicalText(const WorldConfig& config);
std::uint64_t worldConfigDigest(const WorldConfig& config);

// The axiom ids in slot order, comma separated ("" for none), for logs and manifests.
std::string worldConfigIds(const WorldConfig& config);

}
