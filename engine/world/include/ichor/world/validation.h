#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/world_desc.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// What the forge makes today: the land, with channels cut along its rivers when asked, and where
// the water goes over that final land.
struct ForgeRequest {
    WorldDesc desc;
    HydrologyParams rivers;
    bool carveRivers = false;
};

struct ForgedWorld {
    Heightfield heightfield;
    Hydrology hydrology;
};

// Doc 08 "Validation gates": a named pure predicate over a forged world. It returns nothing when the
// world passes and the reason when it does not. A gate that throws counts as failed, with the
// exception's message as the reason.
struct Gate {
    std::string name;
    std::function<std::optional<std::string>(const ForgedWorld&, const ForgeRequest&)> check;
};

struct GateFailure {
    std::string gate;
    std::string reason;
};

struct AttemptReport {
    std::uint32_t attempt = 0;
    std::uint64_t seed = 0; // the seed the attempt forged with
    std::vector<GateFailure> failures; // every failing gate, in gate order
};

// The seed attempt `attempt` forges with: the world seed itself for attempt 0, hashCombine(seed, attempt) after.
std::uint64_t attemptSeed(std::uint64_t worldSeed, std::uint32_t attempt);

ForgedWorld forgeWorld(const ForgeRequest& request);

// Runs every gate and lists all that fail.
std::vector<GateFailure> runGates(const std::vector<Gate>& gates, const ForgedWorld& world, const ForgeRequest& request);

// The gates that are cheap with the phases that exist: land above the water, not flat, a river when
// rivers were asked for, and hydrology that agrees with itself.
std::vector<Gate> defaultGates();

constexpr std::uint32_t kMaxRerolls = 8;

struct ValidatedWorld {
    // The world as saved. Its heightfield carries the ORIGINAL seed whatever attempt produced it.
    ForgedWorld world;
    std::uint64_t seed = 0;    // the world seed the player's save records
    std::uint32_t attempt = 0; // zero, or the reroll that passed
    bool passed = false;       // false: every attempt failed, and `world` is attempt 0 as forged
    std::vector<AttemptReport> rejected; // the attempts that failed before `attempt`, in order
};

// Forges attempt 0 with the seed as given, then up to maxRerolls more with derived seeds, and stops
// at the first that passes every gate. Nothing random: the same request and gates always pick the
// same attempt. A world that passes at attempt 0 is exactly what forgeWorld returns.
//
// To reproduce a saved world: forge with attemptSeed(seed, attempt) as the description's seed (the
// heightmap on disk already is that world, so a loader normally never needs to).
ValidatedWorld forgeValidated(const ForgeRequest& request, const std::vector<Gate>& gates, std::uint32_t maxRerolls = kMaxRerolls);

}
