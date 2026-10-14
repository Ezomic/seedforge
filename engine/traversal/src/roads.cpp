#include "ichor/traversal/roads.h"

#include "ichor/core/wading.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>

namespace ichor {

namespace {

int clampBonus(int bonusPermille)
{
    return std::clamp(bonusPermille, kMinRoadBonusPermille, kMaxRoadBonusPermille);
}

}

bool loadRoadBonus(const DataSet& data, int& bonusPermille, std::string& note, std::string& error)
{
    bonusPermille = kDefaultRoadBonusPermille;
    const DataTable* table = data.table("roads");
    if (table == nullptr) {
        return true;
    }
    const auto record = table->records.find("road_bonus");
    if (record == table->records.end()) {
        return true;
    }
    const auto field = record->second.fields.find("bonus_permille");
    if (field == record->second.fields.end()) {
        return true;
    }
    const auto* number = std::get_if<double>(&field->second);
    if (number == nullptr || !std::isfinite(*number) || *number != std::floor(*number)) {
        error = "road_bonus: \"bonus_permille\" must be an integer";
        return false;
    }
    const auto clamped = static_cast<int>(std::clamp(*number, -1e6, 1e6));
    bonusPermille = clampBonus(clamped);
    if (bonusPermille != clamped) {
        note = "road_bonus: bonus_permille " + std::to_string(clamped) + " is outside " + std::to_string(kMinRoadBonusPermille)
            + " to " + std::to_string(kMaxRoadBonusPermille) + ", clamped to " + std::to_string(bonusPermille);
    }
    return true;
}

int roadSpeedPermille(float coverage, int bonusPermille)
{
    if (!(coverage > 0.0f)) {
        return 1000;
    }
    const int coveragePermille = coverage >= 1.0f ? 1000 : static_cast<int>(coverage * 1000.0f + 0.5f);
    return 1000 + clampBonus(bonusPermille) * coveragePermille / 1000;
}

double roadSpeedFactor(float coverage, float waterDepth, int bonusPermille)
{
    if (waterDepth > kWadeStart) {
        return 1.0;
    }
    return static_cast<double>(roadSpeedPermille(coverage, bonusPermille)) / 1000.0;
}

}
