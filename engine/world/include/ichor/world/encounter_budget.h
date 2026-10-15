#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The six authored roles of doc 05, in the doc's order.
enum class EnemyRole { Skirmisher, Anchor, Artillery, Controller, Swarm, Ambusher };

constexpr std::size_t kEnemyRoleCount = 6;

std::string_view enemyRoleName(EnemyRole role);

// What one unit of a role costs against the budget, how likely the composer is to
// pick it, and how many of it one encounter may hold.
struct RoleBudget {
    EnemyRole role = EnemyRole::Skirmisher;
    std::uint32_t cost = 1;
    std::uint32_t weight = 1;
    std::uint32_t maxCount = 1;
};

// One face for a role. An empty faction or biome matches any; the most specific
// matching skin wins (faction outranks biome), ties go to the smaller id.
struct Skin {
    std::string id;
    EnemyRole role = EnemyRole::Skirmisher;
    std::string faction;
    std::string biome;
};

struct EncounterRequest {
    std::uint64_t seed = 0;
    std::uint32_t budget = 0;
    std::string faction;
    std::string biome;
};

struct EncounterMember {
    EnemyRole role = EnemyRole::Skirmisher;
    std::string skin;
    std::uint32_t cost = 0;
};

struct Encounter {
    std::vector<EncounterMember> members;
    std::uint32_t spent = 0;
};

constexpr std::size_t kMaxEncounterMembers = 64;
constexpr std::uint32_t kMaxRoleWeight = 1000000;

// Problems with a role table: zero cost, weight or cap, duplicate roles, a role
// missing from the table. Empty when the table is usable.
std::vector<std::string> validateRoleBudgets(const std::vector<RoleBudget>& table);

// Fills the budget by drawing affordable roles by weight until nothing fits, every
// role hits its cap, or kMaxEncounterMembers is reached, then dresses each pick in
// the request's faction and biome. Deterministic in the request and tables. A role
// with no matching skin is never drawn. Every problem with the inputs is appended to
// `problems` and yields an empty encounter; never throws on bad input.
Encounter composeEncounter(const EncounterRequest& request, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems);

}
