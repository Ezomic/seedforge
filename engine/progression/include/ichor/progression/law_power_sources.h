#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Doc 05 "The Law axiom decides what combat spends": the three Law power sources that remain
// after Orichalcum, Hecatomb and The Last Favour. Each is pure integer logic (Temenos takes the
// field value as a float and returns integers), so the simulation agrees bit for bit. Nothing
// here is wired into abilities yet.

// A growth in permille (1000 = none) is clamped to the campaign cap of docs/07-progression.md, so
// no source can hand out more than twice its starting capacity. A negative growth reads as 1000.
std::int64_t cappedGrowthPermille(std::int64_t growthPermille);

// ---- The Mysteries: rites with fixed uses, held as items, admission can be revoked ----

constexpr int kMaxRiteUses = 99;
constexpr std::size_t kMaxRiteLoadout = 64;

struct Rite {
    std::string id;
    int usesLeft = 0;
    bool revoked = false;
};

struct MysteriesLoadout {
    std::vector<Rite> rites;
    bool locked = false; // set on leaving the sanctuary: the loadout is lived with
};

// Slots open to a character: baseSlots grown by the capped growth, rounded down, never above
// kMaxRiteLoadout. A base below 0 gives 0.
int mysteriesSlotCapacity(int baseSlots, std::int64_t growthPermille);

// Adds a rite with `uses` uses (1 to kMaxRiteUses). False, and no change, when the loadout is
// locked, the id is empty or already present, the uses are out of range, or capacity is full.
bool mysteriesAdmit(MysteriesLoadout& loadout, const std::string& id, int uses, int capacity);

// Revokes admission to a rite. False when the id is unknown or already revoked.
bool mysteriesRevoke(MysteriesLoadout& loadout, const std::string& id);

// Spends one use. False, and no change, when the rite is unknown, revoked, or out of uses.
bool mysteriesCast(MysteriesLoadout& loadout, const std::string& id);

// ---- The Oath: a debt meter with escalating consequence and no absolution ----

enum class OathTier : std::uint8_t { Clear, Warned, Marked, Called };

struct OathDebt {
    std::int64_t debt = 0;
    std::int64_t ceiling = 0; // the debt ceiling; more power available, more attention on the reckoning
};

// Debt ceiling: base grown by the capped growth. Zero for a base below 0, saturating at int32 range.
std::int64_t oathCeiling(std::int64_t base, std::int64_t growthPermille);

// Borrows power: adds `amount` (positive) to the debt. False, and no change, when the amount is not
// positive or the result would pass the ceiling.
bool oathBorrow(OathDebt& oath, std::int64_t amount);

// Pays down debt. It never forgives: at most kOathSettleMaxPermille of the current debt, rounded
// down, comes off in one call. Returns the amount removed (0 for a non-positive request).
constexpr std::int64_t kOathSettleMaxPermille = 100;
std::int64_t oathSettle(OathDebt& oath, std::int64_t amount);

// Consequence tier from how full the meter is: Clear below 250 permille, Warned from 250, Marked
// from 600, Called from 900. A ceiling of 0 or less reads as Called whenever there is any debt.
OathTier oathTier(const OathDebt& oath);

// ---- Temenos: power exists only on consecrated ground ----

constexpr std::int64_t kTemenosFullPermille = 1000;

// Strength of a power in permille of its full value at a Temenos field value (0 to 1, see
// ichor/world/temenos_field.h): offGroundFloorPermille on unconsecrated ground rising linearly to
// 1000 at full field. A floor outside 0..1000 is clamped; a field that is not a number counts as 0.
std::int64_t temenosStrengthPermille(float field, std::int64_t offGroundFloorPermille);

// A damage or effect value scaled by that strength, rounded down. Zero for a negative base.
std::int64_t temenosScaled(std::int64_t base, float field, std::int64_t offGroundFloorPermille);

}
