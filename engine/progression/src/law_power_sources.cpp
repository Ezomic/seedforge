#include "ichor/progression/law_power_sources.h"

#include "ichor/progression/power_growth.h"

#include <algorithm>
#include <limits>

namespace ichor {

namespace {

constexpr std::int64_t kOathWarnedPermille = 250;
constexpr std::int64_t kOathMarkedPermille = 600;
constexpr std::int64_t kOathCalledPermille = 900;

// value * permille / 1000 without overflow for value >= 0 and 0 <= permille <= 2000, rounded down.
std::int64_t mulPermille(std::int64_t value, std::int64_t permille)
{
    return (value / 1000) * permille + ((value % 1000) * permille) / 1000;
}

// The same, rounded up.
std::int64_t mulPermilleCeil(std::int64_t value, std::int64_t permille)
{
    return (value / 1000) * permille + ((value % 1000) * permille + 999) / 1000;
}

}

std::int64_t cappedGrowthPermille(std::int64_t growthPermille)
{
    return std::clamp<std::int64_t>(growthPermille, 1000, kPowerCapPermille);
}

int mysteriesSlotCapacity(int baseSlots, std::int64_t growthPermille)
{
    const std::int64_t base = std::clamp<std::int64_t>(baseSlots, 0, static_cast<std::int64_t>(kMaxRiteLoadout));
    const std::int64_t grown = mulPermille(base, cappedGrowthPermille(growthPermille));
    return static_cast<int>(std::min<std::int64_t>(grown, static_cast<std::int64_t>(kMaxRiteLoadout)));
}

bool mysteriesAdmit(MysteriesLoadout& loadout, const std::string& id, int uses, int capacity)
{
    if (loadout.locked || id.empty() || uses < 1 || uses > kMaxRiteUses) return false;
    if (capacity < 0 || loadout.rites.size() >= static_cast<std::size_t>(std::min<int>(capacity, static_cast<int>(kMaxRiteLoadout)))) return false;
    const bool present = std::any_of(loadout.rites.begin(), loadout.rites.end(), [&](const Rite& r) { return r.id == id; });
    if (present) return false;
    loadout.rites.push_back(Rite{id, uses, false});
    return true;
}

bool mysteriesRevoke(MysteriesLoadout& loadout, const std::string& id)
{
    for (Rite& rite : loadout.rites) {
        if (rite.id != id) continue;
        if (rite.revoked) return false;
        rite.revoked = true;
        return true;
    }
    return false;
}

bool mysteriesCast(MysteriesLoadout& loadout, const std::string& id)
{
    for (Rite& rite : loadout.rites) {
        if (rite.id != id) continue;
        if (rite.revoked || rite.usesLeft <= 0) return false;
        --rite.usesLeft;
        return true;
    }
    return false;
}

std::int64_t oathCeiling(std::int64_t base, std::int64_t growthPermille)
{
    constexpr std::int64_t kLimit = std::numeric_limits<std::int32_t>::max();
    const std::int64_t clamped = std::clamp<std::int64_t>(base, 0, kLimit);
    return std::min(mulPermille(clamped, cappedGrowthPermille(growthPermille)), kLimit);
}

bool oathBorrow(OathDebt& oath, std::int64_t amount)
{
    if (amount <= 0 || oath.debt < 0 || oath.ceiling < oath.debt) return false;
    if (amount > oath.ceiling - oath.debt) return false;
    oath.debt += amount;
    return true;
}

std::int64_t oathSettle(OathDebt& oath, std::int64_t amount)
{
    if (amount <= 0 || oath.debt <= 0) return 0;
    const std::int64_t removed = std::min(amount, mulPermille(oath.debt, kOathSettleMaxPermille));
    oath.debt -= removed;
    return removed;
}

OathTier oathTier(const OathDebt& oath)
{
    if (oath.debt <= 0) return OathTier::Clear;
    if (oath.ceiling <= 0) return OathTier::Called;
    if (oath.debt >= mulPermilleCeil(oath.ceiling, kOathCalledPermille)) return OathTier::Called;
    if (oath.debt >= mulPermilleCeil(oath.ceiling, kOathMarkedPermille)) return OathTier::Marked;
    if (oath.debt >= mulPermilleCeil(oath.ceiling, kOathWarnedPermille)) return OathTier::Warned;
    return OathTier::Clear;
}

std::int64_t temenosStrengthPermille(float field, std::int64_t offGroundFloorPermille)
{
    const std::int64_t floorPermille = std::clamp<std::int64_t>(offGroundFloorPermille, 0, kTemenosFullPermille);
    std::int64_t fieldPermille = 0;
    if (field > 0.0f) fieldPermille = field >= 1.0f ? kTemenosFullPermille : static_cast<std::int64_t>(field * 1000.0f);
    fieldPermille = std::clamp<std::int64_t>(fieldPermille, 0, kTemenosFullPermille);
    return floorPermille + ((kTemenosFullPermille - floorPermille) * fieldPermille) / kTemenosFullPermille;
}

std::int64_t temenosScaled(std::int64_t base, float field, std::int64_t offGroundFloorPermille)
{
    if (base <= 0) return 0;
    return mulPermille(base, temenosStrengthPermille(field, offGroundFloorPermille));
}

}
