#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// One discrete verb the player can be allowed to do ("pick locks of this
// class"). It is all-or-nothing: there are no ranks and no numeric fields, so
// nothing here can become a "+5%" bonus.
struct Capability {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> prerequisites;
};

// Parses and validates a JSON array of capabilities, each {"id", "name",
// "description"} with an optional "requires": [ids]. Every problem is appended
// to `problems` as "<label>: <path>: <what is wrong>", with paths like
// "[2].requires[0]". Prerequisites must name capabilities in the same table and
// must not form a cycle. Returns true, and fills `out`, only when there are no
// problems. Never throws on bad input.
bool parseCapabilities(std::string_view label, std::string_view text, std::vector<Capability>& out, std::vector<std::string>& problems);

// The first prerequisite cycle as a closed id path ("a", "c", "a"), or empty
// when there is none. Ids are visited in sorted order, so the answer does not
// depend on table order. Prerequisites that name no capability are ignored.
std::vector<std::string> findPrerequisiteCycle(const std::vector<Capability>& table);

// Compact JSON sorted by id with sorted keys, so the same table always yields
// the same text whatever its order in the source.
std::string capabilitiesCanonicalJson(const std::vector<Capability>& table);
std::uint64_t capabilitiesDigest(const std::vector<Capability>& table);

enum class UnlockResult { Unlocked, UnknownCapability, AlreadyUnlocked, PrerequisiteUnmet };

// One player's unlocked capabilities against a table that passed
// parseCapabilities. Unlocks are the only state; there is no progress toward an
// unlock, no experience and no rank.
class CapabilityUnlocks {
public:
    explicit CapabilityUnlocks(std::vector<Capability> table);

    bool can(std::string_view id) const;
    UnlockResult unlock(std::string_view id);

    // Sorted by id.
    std::vector<std::string> unlocked() const;

    // Depends only on the set of unlocked ids, not on the order they were unlocked in.
    std::uint64_t digest() const;

    // Version, then the unlocked ids sorted, through ichor/core/binary.h.
    std::vector<std::uint8_t> serialise() const;

    // Replaces the state, or leaves it alone and returns false when the bytes are
    // truncated, padded, of another version, name an unknown or repeated id, or
    // unlock something whose prerequisites are not also unlocked.
    bool deserialise(std::span<const std::uint8_t> bytes);

private:
    std::vector<Capability> table_;
    std::map<std::string, std::size_t, std::less<>> index_;
    std::set<std::string, std::less<>> unlocked_;
};

}
