#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The cross-seed codex (docs/07-progression.md, "Across seeds"): what the
// player has learned over many worlds. Informational and cosmetic only, so
// nothing here may feed back into the simulation. Pure data: it is not part of
// any save file yet, and nothing here knows about the world screen.

enum class CodexCategory : std::uint8_t {
    Axiom,
    Synergy,
    Faction,
    World,  // a world forged, keyed by a stable world id
    Seed,   // a seed completed, keyed by a stable seed id
    Count_, // keep last: the checks reject anything at or beyond it
};

struct CodexLimits {
    std::size_t maxEntriesPerCategory = 1024;
    std::size_t maxIdBytes = 64; // never effective above 255, the codec length byte
};

struct CodexEntry {
    CodexCategory category = CodexCategory::Axiom;
    std::string id; // 1..maxIdBytes of [A-Za-z0-9_.-]
};

enum class CodexResult {
    Unlocked,     // newly added
    AlreadyKnown, // the entry exists; nothing changed
    InvalidId,
    InvalidCategory,
    TooMany, // the per-category limit is reached
};

bool isValidCodexId(std::string_view id, std::size_t maxIdBytes);

class CodexStore {
public:
    explicit CodexStore(CodexLimits limits = {});

    // Unlock once: a second unlock of the same category and id is a no-op.
    // Rejected entries leave the store untouched.
    CodexResult unlock(CodexCategory category, std::string_view id);

    bool has(CodexCategory category, std::string_view id) const;
    std::size_t count(CodexCategory category) const;
    // In unlock order, which is also the order serialisation uses.
    std::span<const CodexEntry> all() const { return entries_; }
    std::size_t size() const { return entries_.size(); }
    const CodexLimits& limits() const { return limits_; }

private:
    CodexLimits limits_;
    std::vector<CodexEntry> entries_;
};

} // namespace ichor
