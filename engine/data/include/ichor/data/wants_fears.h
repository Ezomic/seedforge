#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

enum class DesireKind { Want, Fear };

enum class CompareOp { Lt, Le, Eq, Ge, Gt };

constexpr int kMinIntensity = 1;
constexpr int kMaxIntensity = 10;

std::string_view desireKindName(DesireKind kind);
std::string_view compareOpName(CompareOp op);

// Active while `tag <op> value` holds in the world snapshot. A tag the snapshot
// does not carry never holds.
struct Condition {
    std::string tag;
    CompareOp op = CompareOp::Eq;
    std::int64_t value = 0;
};

// What an NPC wants or fears, and the world state that makes it live now.
struct Desire {
    std::string id;
    DesireKind kind = DesireKind::Want;
    std::string subject;
    int intensity = kMinIntensity;
    Condition condition;
};

// An abstract snapshot of the world: integer tags by name.
using WorldSnapshot = std::map<std::string, std::int64_t, std::less<>>;

// Parses one file: a JSON array of flat records with "id", "kind" (want or
// fear), "subject", "intensity" (1 to 10), and the condition as "tag", "op"
// (<, <=, ==, >=, >) and an integer "value". Every problem is appended to
// `problems` as "<label>: <path>: <what is wrong>". Returns true, and fills
// `out`, only when there are no problems. Never throws on bad input.
bool parseDesires(std::string_view label, std::string_view text, std::vector<Desire>& out, std::vector<std::string>& problems);

bool conditionHolds(const Condition& condition, const WorldSnapshot& snapshot);

// The desires whose condition holds, strongest first, ties by id.
std::vector<Desire> activeDesires(const std::vector<Desire>& desires, const WorldSnapshot& snapshot);

// Key-sorted compact JSON, so the same record always yields the same text.
std::string desireCanonicalJson(const Desire& desire);
std::uint64_t desireDigest(const Desire& desire);

// Over every desire's digest in id order; independent of list order.
std::uint64_t desiresDigest(const std::vector<Desire>& desires);

}
