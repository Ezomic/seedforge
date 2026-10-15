#include "ichor/factions/poleis.h"

namespace ichor {

namespace {

constexpr std::string_view kPowersPrefix = "powers.";

}

std::optional<IncumbentScaling> scaleIncumbents(const IncumbentPlan& plan, std::uint32_t fullScale)
{
    if (fullScale > kMaxIncumbentScale) {
        return std::nullopt;
    }
    IncumbentScaling scaling{plan.shape, {}, {}};
    const auto count = static_cast<std::uint32_t>(plan.incumbents.size());
    if (count == 0) {
        return scaling;
    }
    const std::uint32_t each = fullScale / count;
    const std::uint32_t remainder = fullScale % count;
    for (std::uint32_t i = 0; i < count; ++i) {
        scaling.shares.push_back({plan.incumbents[i].id, each + (i == 0 ? remainder : 0)});
    }
    if (plan.shape == IncumbentShape::Poleis) {
        for (std::size_t i = 1; i < plan.incumbents.size(); ++i) {
            scaling.wars.emplace_back(plan.incumbents[i - 1].id, plan.incumbents[i].id);
        }
    }
    return scaling;
}

std::optional<IncumbentScaling> scaleIncumbentsForAxioms(const std::vector<std::string>& axiomIds, int poleisCount, std::uint32_t fullScale)
{
    const std::string* powers = nullptr;
    for (const auto& id : axiomIds) {
        if (std::string_view(id).substr(0, kPowersPrefix.size()) != kPowersPrefix) {
            continue;
        }
        if (powers != nullptr) {
            return std::nullopt;
        }
        powers = &id;
    }
    if (powers == nullptr) {
        return std::nullopt;
    }
    const auto plan = planIncumbents(*powers, poleisCount);
    if (!plan) {
        return std::nullopt;
    }
    return scaleIncumbents(*plan, fullScale);
}

}
