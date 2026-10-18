#pragma once

#include <forgecli/forge.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace forgecli {

// A rule over the layers one phase produced for one seed. Returns an empty string when the rule
// holds, else a short description of the violation. Adding a rule is one entry in phaseRules().
struct PhaseRule {
    std::string_view phase;
    std::string_view name;
    std::string (*check)(const std::vector<Layer>& layers, int size);
};

const std::vector<PhaseRule>& phaseRules();

struct FuzzOptions {
    std::uint64_t seedBegin = 0;
    std::uint64_t seedEnd = 0; // inclusive
    int size = 128;
    std::vector<std::string> phases; // empty means every registered phase
    // Per-seed budgets in cell passes and bytes (never wall clock). Zero selects the default derived from size.
    std::int64_t maxCellPasses = 0;
    std::int64_t maxLayerBytes = 0;
};

struct Violation {
    std::uint64_t seed = 0;
    std::string phase; // empty for seed-wide rules
    std::string rule;
    std::string detail;
};

struct FuzzResult {
    std::uint64_t seedsRun = 0;
    std::vector<Violation> violations;
    std::string error; // set when the options are invalid
};

FuzzResult runFuzz(const FuzzOptions& options);

std::string fuzzSummary(const FuzzOptions& options, const FuzzResult& result);

}
