#pragma once

#include "ichor/data/data_set.h"

#include <map>
#include <string>
#include <vector>

namespace ichor {

// Doc 06 class ratios: the share of all loot each class should make up.
struct LootRatioTargets {
    std::map<std::string, double> shares = {
        {"consumable", 0.55}, {"material", 0.20}, {"information", 0.10}, {"coin", 0.10}, {"gear", 0.05}};
    // A class passes when its actual share is within this many points (absolute) of its target.
    double tolerance = 0.05;
};

struct LootClassShare {
    std::string name;
    int count = 0;
    double target = 0.0;
    double actual = 0.0;
    double deviation = 0.0; // actual - target
    bool within = true;
};

struct LootRatioReport {
    int total = 0;
    std::vector<LootClassShare> classes;  // every target class, in target-name order
    std::map<std::string, int> tierCounts; // tier -> items
    std::vector<std::string> problems;    // unknown class or tier, empty table
    bool ok() const;
    // One line per class ("gear: 30 (50.0%) target 5.0% +45.0 points OUT") plus any problems.
    std::string text() const;
};

// Checks the class mix of a catalogue table (records with "class" and "tier"
// fields) against the targets. Pure: reads only its arguments.
LootRatioReport checkLootRatios(const DataTable& catalogue, const LootRatioTargets& targets = {});

}
