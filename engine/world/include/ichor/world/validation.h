#pragma once

#include "ichor/world/heightfield.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/phase_pipeline.h"
#include "ichor/world/world_desc.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

struct WorldConfig;

// What the forge makes today: the land, with channels cut along its rivers when asked, and where
// the water goes over that final land.
struct ForgeRequest {
    WorldDesc desc;
    HydrologyParams rivers;
    bool carveRivers = false;
    // Take the land from libworldforge (forged_heightfield.h) instead of the value-noise stand-in. Off by
    // default, so the stand-in and every golden made from it stay as they are.
    bool forgedTerrain = false;
    // Doc 08 phase 0: the world's axioms. Only the axiom gates read it; null or without axioms, they pass.
    std::shared_ptr<const WorldConfig> axioms;
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

// The names of the forge's phases, in order. Each artifact is final once made: "rivers" is a copy of
// "terrain" when rivers are not carved, never terrain edited in place.
constexpr const char* kPhaseTerrain = "terrain";     // Heightfield
constexpr const char* kPhaseRivers = "rivers";       // Heightfield
constexpr const char* kPhaseHydrology = "hydrology"; // Hydrology

// The forge as phases over the request. Run it whole, or from a phase on with the earlier artifacts
// of a run made from the same description (to re-run only the hydrology, say, or to diff two seeds).
PhasePipeline forgePipeline(const ForgeRequest& request);

ForgedWorld forgedWorld(const PhaseRun& run);

// The same as forgedWorld(forgePipeline(request).run()).
ForgedWorld forgeWorld(const ForgeRequest& request);

// Runs every gate and lists all that fail.
std::vector<GateFailure> runGates(const std::vector<Gate>& gates, const ForgedWorld& world, const ForgeRequest& request);

// The gates that are cheap with the phases that exist: land above the water, not flat, a river when
// rivers were asked for, and hydrology that agrees with itself, then axiomGates().
std::vector<Gate> defaultGates();

// Doc 01 invariants an axiom's merged directives put on the forged land. Each reads the request's WorldConfig and
// passes when it is absent or does not set the directive, so a world without axioms is never rejected by them:
//  settlement-elevation-land  settlement.min_elevation_m: enough dry land stands at or above that height
//  dive-water-reachable       traversal.unlock has swim_deep: water kDiveDepthM deep is reachable from a shore
//  spring-needs-water         poi.weights gives sacred_spring weight: a river, lake or sea exists
//  coast-needs-shore          biomes.inject has tidal_ruin or salt_blight, or poi.weights gives pharos or
//                             drowned_polis weight: land meets water somewhere
std::vector<Gate> axiomGates();

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
