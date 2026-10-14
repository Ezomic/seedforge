#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The six roles of doc 03, in the doc's order.
enum class FactionRole { Incumbent, Challenger, Parasite, Custodian, Refusenik, Herald };

constexpr std::size_t kFactionRoleCount = 6;

// Lowercase JSON spelling: "incumbent", "challenger", ...
std::string_view factionRoleName(FactionRole role);
std::optional<FactionRole> factionRoleFromName(std::string_view name);

struct Faction {
    std::string id;
    std::string name;
    FactionRole role = FactionRole::Incumbent;
};

// Parses and validates a JSON array of factions, each {"id", "name", "role"}.
// Every problem is appended to `problems` as "<label>: <path>: <what is wrong>",
// with paths like "[2].role". Returns true, and fills `out`, only when there are
// no problems. Never throws on bad input.
bool parseFactions(std::string_view label, std::string_view text, std::vector<Faction>& out, std::vector<std::string>& problems);

// Compact JSON sorted by id with sorted keys, so the same factions always yield
// the same text whatever their order in the source.
std::string factionsCanonicalJson(const std::vector<Faction>& factions);
std::uint64_t factionsDigest(const std::vector<Faction>& factions);

// Doc 03 role rules for one world's factions. Returns every violation, empty
// when the set is valid:
//  - 5 to 7 factions in total;
//  - ids are unique;
//  - one to two Challengers;
//  - at least one each of Parasite, Custodian, Refusenik and Herald;
//  - any number of Incumbents, including none (The Dark Age has no Incumbent,
//    The Poleis run several at reduced scale), within the total.
// The rules that need the world's axioms (which Incumbent the Powers axiom
// allows) belong to the role mapping, not here.
std::vector<std::string> validateFactionSet(const std::vector<Faction>& factions);

}
