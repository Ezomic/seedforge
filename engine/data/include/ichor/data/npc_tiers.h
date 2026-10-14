#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ichor {

// The four tiers of doc 10, deepest first.
enum class NpcTier { Principal, Regular, Resident, Crowd };

std::string_view npcTierName(NpcTier tier);

constexpr std::size_t kMinRelationships = 3;
constexpr std::size_t kMaxRelationships = 5;

// Doc 10: family, rival, creditor, lover, grudge.
enum class RelationKind { Family, Rival, Creditor, Lover, Grudge };

std::string_view relationKindName(RelationKind kind);

struct Relationship {
    RelationKind kind = RelationKind::Family;
    std::string target; // id of a named NPC (any tier but Crowd)
};

// Hand-authored, cast into a role by the seed. voice, arc, vo and face name
// the authored assets; castRoles lists the roles the seed may cast them in.
struct Principal {
    std::string id;
    std::string name;
    std::string voice;
    std::string arc;
    std::string vo;
    std::string face;
    std::vector<std::string> castRoles;
};

// A person embedded in a situation: home, economic role, two or three traits,
// three to five relationships, one want, one fear, a ledger memory key.
struct Regular {
    std::string id;
    std::string name;
    std::string home;
    std::string role;
    std::vector<std::string> traits;
    std::vector<Relationship> relationships;
    std::string want;
    std::string fear;
    std::string memory;
};

// Named and located, nothing more: population and casualties.
struct Resident {
    std::string id;
    std::string name;
    std::string location;
};

// Unnamed; carries only the ambience it adds to a place.
struct Crowd {
    std::string id;
    std::string ambience;
};

using NpcRecord = std::variant<Principal, Regular, Resident, Crowd>;

NpcTier npcTier(const NpcRecord& record);
const std::string& npcId(const NpcRecord& record);

// Parses one file: a JSON array of records, each with a string "id" and a
// "tier", and exactly the fields its tier has. Every problem is appended to
// `problems` as "<label>: <path>: <what is wrong>". Returns true, and fills
// `out`, only when there are no problems. Never throws on bad input.
bool parseNpcFile(std::string_view label, std::string_view text, std::vector<NpcRecord>& out, std::vector<std::string>& problems);

// Key-sorted compact JSON, so the same record always yields the same text.
std::string npcCanonicalJson(const NpcRecord& record);
std::uint64_t npcDigest(const NpcRecord& record);

// Every *.json file directly in a directory. Loading is all or nothing: on any
// problem nothing is replaced and problems() lists every one of them, across
// all files, in file order. Ids are unique across files and tiers, and every
// relationship names another named NPC.
class NpcRoster {
public:
    bool loadDirectory(const std::filesystem::path& directory);

    const std::vector<std::string>& problems() const { return problems_; }
    const std::map<std::string, NpcRecord, std::less<>>& records() const { return records_; }
    const NpcRecord* find(std::string_view id) const;
    std::size_t count(NpcTier tier) const;

    // Over every record's digest in id order; independent of file names and load order.
    std::uint64_t digest() const;

private:
    std::map<std::string, NpcRecord, std::less<>> records_;
    std::vector<std::string> problems_;
};

}
