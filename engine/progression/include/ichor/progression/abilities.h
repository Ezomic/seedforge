#pragma once

#include <cstdint>
#include <string>

namespace ichor {

// Doc 05 "One ability system. The Law swaps only the power source." An ability states what it
// costs in abstract power units and what it does; the PowerSource decides what is actually paid.
// Pure integer logic, so the simulation agrees bit for bit. Nothing here is wired into combat yet.

constexpr std::int64_t kMaxAbilityCost = 1000000;
constexpr std::int64_t kMaxAbilityEffect = 1000000;

struct AbilityDef {
    std::string id;
    std::int64_t cost = 0;   // power units, 1 to kMaxAbilityCost
    std::int64_t effect = 0; // magnitude delivered on resolve, 0 to kMaxAbilityEffect
};

bool abilityDefValid(const AbilityDef& def);

class PowerSource {
public:
    virtual ~PowerSource() = default;

    // True when `cost` units (positive) can be paid now, without paying them.
    virtual bool canPay(std::int64_t cost) const = 0;

    // Pays `cost` units. False, and no change, when canPay would be false.
    virtual bool pay(std::int64_t cost) = 0;
};

enum class AbilityOutcome : std::uint8_t { Resolved, InvalidDef, Unaffordable };

struct AbilityResult {
    AbilityOutcome outcome = AbilityOutcome::InvalidDef;
    std::int64_t effect = 0; // 0 unless Resolved
};

// The single entry point for using an ability: validates the definition, asks the source to pay,
// and delivers the effect only when payment succeeded. A failed use changes nothing.
AbilityResult resolveAbility(const AbilityDef& def, PowerSource& source);

// ---- Orichalcum: physical charges, one unit per charge ----

class ChargeSource final : public PowerSource {
public:
    explicit ChargeSource(std::int64_t charges);
    std::int64_t charges() const { return charges_; }
    bool canPay(std::int64_t cost) const override;
    bool pay(std::int64_t cost) override;

private:
    std::int64_t charges_ = 0;
};

// ---- Hecatomb: the caster's own health is the offering; a use can never kill ----

class HealthSource final : public PowerSource {
public:
    explicit HealthSource(std::int64_t health);
    std::int64_t health() const { return health_; }
    bool canPay(std::int64_t cost) const override;
    bool pay(std::int64_t cost) override;

private:
    std::int64_t health_ = 0;
};

} // namespace ichor
