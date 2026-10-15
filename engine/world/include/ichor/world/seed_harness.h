#pragma once

#include "ichor/world/validation.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

constexpr std::uint32_t kMaxHarnessSeeds = 100'000;
constexpr std::size_t kMaxSampleReasons = 3;

// Forges `count` consecutive seeds from `firstSeed` through the validation gates, with the same
// reroll rule a player's world gets, and tallies which gates reject worlds. Only the phases that
// exist today are run, so the report grows with the forge: a new phase's gate appears in it as soon
// as it is in `gates`.
struct HarnessConfig {
    ForgeRequest request; // desc.seed is replaced per seed
    std::uint64_t firstSeed = 1;
    std::uint32_t count = 0;
    std::uint32_t maxRerolls = kMaxRerolls;
    std::vector<Gate> gates;
};

struct SeedOutcome {
    std::uint64_t seed = 0;
    bool passed = false;       // some attempt passed every gate
    std::uint32_t attempt = 0; // the attempt that passed; the last one tried when none did
    std::vector<GateFailure> firstAttemptFailures; // empty when attempt 0 passed
    std::uint64_t micros = 0;  // wall time; left out of the text and JSON unless asked for
};

struct GateStats {
    std::string gate;
    std::uint32_t firstAttemptFailures = 0; // seeds whose attempt 0 failed this gate
    std::uint32_t attemptFailures = 0;      // attempts, over every seed, that failed this gate
    std::uint32_t blockedSeeds = 0;         // seeds where every attempt failed this gate
    std::vector<std::string> sampleReasons; // the first distinct reasons met, at most kMaxSampleReasons
};

struct HarnessReport {
    std::uint64_t firstSeed = 0;
    std::uint32_t count = 0;
    std::uint32_t maxRerolls = 0;
    std::uint32_t passedFirstAttempt = 0;
    std::uint32_t passedAfterReroll = 0;
    std::uint32_t failedAllAttempts = 0;
    std::vector<GateStats> gates; // in gate order, one per gate that was run
    std::vector<SeedOutcome> seeds;
};

// Throws std::invalid_argument for a count of zero or above kMaxHarnessSeeds, or a description that is not valid.
HarnessReport runHarness(const HarnessConfig& config);

// Both are a pure function of the report when `withTiming` is false: no clock, no paths, no addresses.
std::string harnessText(const HarnessReport& report, bool withTiming);
std::string harnessJson(const HarnessReport& report, bool withTiming);

}
