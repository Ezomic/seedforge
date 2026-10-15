#pragma once

#include "ichor/data/data_set.h"

#include <string>

namespace ichor {

// Doc 11 gives roads a movement bonus of 20 to 30 percent. Data outside that range is clamped at load.
constexpr int kMinRoadBonusPermille = 200;
constexpr int kMaxRoadBonusPermille = 300;
constexpr int kDefaultRoadBonusPermille = 250;

// How much of the ground under a walker is road, 0 (none) to 1 (all of it).
class RoadField {
public:
    virtual ~RoadField() = default;
    virtual float coverageAt(double x, double z) const = 0;
};

class NullRoadField final : public RoadField {
public:
    float coverageAt(double, double) const override { return 0.0f; }
};

// The same coverage everywhere, for tests.
class UniformRoadField final : public RoadField {
public:
    explicit UniformRoadField(float coverage) : coverage_(coverage) {}
    float coverageAt(double, double) const override { return coverage_; }

private:
    float coverage_;
};

// The road bonus in thousandths of walking speed, from the "roads" table of the data layer (record "road_bonus",
// integer field "bonus_permille"). A missing table or record gives the default. A value outside the doc's range
// is clamped and described in note; a field that is not an integer is an error and returns false.
bool loadRoadBonus(const DataSet& data, int& bonusPermille, std::string& note, std::string& error);

// The walking speed multiplier in thousandths (1000 is unchanged) at this road coverage. The bonus is scaled
// in proportion to coverage in whole permille; coverage that is not a number counts as none, and is held
// between 0 and 1. A bonus outside the doc's range is clamped.
int roadSpeedPermille(float coverage, int bonusPermille);

// The same as a multiplier for the scene's speed modifier. Water and road do not stack: with the water deeper
// than the wading threshold (kWadeStart) there is no bonus. A depth that is not a number counts as dry.
double roadSpeedFactor(float coverage, float waterDepth, int bonusPermille);

}
