#pragma once

#include "ichor/data/data_set.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

constexpr std::size_t kMinNpcTraits = 2;
constexpr std::size_t kMaxNpcTraits = 3;

struct Trait {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> incompatible;
};

// The validated trait pool (data/npcs/traits.json). A pool only exists if its
// ids are unique, every incompatibility names another trait in the pool and is
// listed from both sides, and it holds at least kMaxNpcTraits traits.
class TraitPool {
public:
    // Reads string "name" and "description" and a list of strings
    // "incompatible" from each record. On failure returns nothing and sets error.
    static std::optional<TraitPool> fromTable(const DataTable& table, std::string& error);
    static std::optional<TraitPool> fromTraits(std::vector<Trait> traits, std::string& error);

    const std::vector<Trait>& traits() const { return traits_; }
    bool incompatible(std::size_t a, std::size_t b) const { return conflicts_[a * traits_.size() + b]; }

private:
    TraitPool() = default;

    std::vector<Trait> traits_;
    std::vector<bool> conflicts_;
};

struct NpcTraits {
    std::vector<std::string> ids; // in draw order
    std::size_t wanted = 0;

    // False when the pool's conflicts left no compatible trait to reach the
    // wanted count; ids then holds as many as could be placed.
    bool complete() const { return ids.size() == wanted; }
};

// Pure: its own generator from (worldSeed, npcId), so one NPC's traits never
// depend on another NPC or on call order. Draws kMinNpcTraits to kMaxNpcTraits
// distinct traits, never an incompatible pair, and always terminates.
NpcTraits assignTraits(std::uint64_t worldSeed, std::uint64_t npcId, const TraitPool& pool);

}
