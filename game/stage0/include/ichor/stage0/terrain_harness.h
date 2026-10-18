#pragma once

#include "ichor/stage0/ambusher.h"
#include "ichor/stage0/artillery.h"
#include "ichor/stage0/fight_record.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ichor::stage0 {

// Enemy validation across the terrain conditions of doc 18. A condition is plain data on a flat
// plane (a walkable box, blocking discs, a slope drag), not a physics or navmesh query: the real
// failures of steep slopes and water edges need real terrain and are deferred until it exists.
// The enemy spawns at the origin; the target stands engageRange away along `approach`.
enum class TerrainCondition : std::uint8_t { Flat, Slope, Obstacle, Narrow, Open };
constexpr std::size_t kTerrainConditionCount = 5;
const char* name(TerrainCondition condition);

struct TerrainDisc {
    glm::vec2 centre{0.0f};
    float radius = 0.0f;
};

struct TerrainDescriptor {
    TerrainCondition condition = TerrainCondition::Flat;
    glm::vec2 boundsMin{-40.0f, -40.0f};
    glm::vec2 boundsMax{40.0f, 40.0f};
    std::vector<TerrainDisc> obstacles; // block movement and line of sight
    glm::vec2 approach{1.0f, 0.0f};     // unit direction from the enemy to the target
    glm::vec2 uphill{1.0f, 0.0f};       // unit direction of rising ground; ignored when slopeDrag is 0
    float slopeDrag = 0.0f;             // fraction of speed lost moving straight uphill, in [0, 1)
};

// The five stock conditions, in TerrainCondition order.
std::array<TerrainDescriptor, kTerrainConditionCount> stockTerrain();

struct ProbeSenses {
    glm::vec2 self{0.0f};
    glm::vec2 target{0.0f};
    bool lineOfSight = true;
};

struct ProbeIntent {
    glm::vec2 move{0.0f}; // metres per second
    bool attack = false;  // an attack resolved this tick
    bool hit = false;     // with attack: it landed on the target
};

using ProbeStep = std::function<ProbeIntent(const ProbeSenses&)>;

// A behaviour under test. `make` builds a fresh brain per condition so runs cannot leak state.
struct EnemyProbe {
    std::string name;
    float engageRange = 3.0f; // how far from the enemy the target stands
    float tickSeconds = 1.0f / 60.0f;
    std::function<ProbeStep()> make;
};

EnemyProbe ambusherProbe(const AmbusherTuning& tuning = {});
EnemyProbe artilleryProbe(const ArtilleryTuning& tuning = {});

constexpr int kHarnessMaxTicks = 1800;
constexpr int kHarnessMaxBlockedTicks = 90; // longest tolerated run of wanting to move and not getting anywhere

struct ConditionResult {
    TerrainCondition condition = TerrainCondition::Flat;
    bool invalidInput = false; // descriptor or probe rejected before running
    bool finite = true;        // false once the behaviour emitted a non-finite move
    bool hitLanded = false;
    int ticks = 0;
    int ticksToHit = -1;
    int blockedTicks = 0;
    int longestBlockedRun = 0;
    bool stuck = false;
    bool passed = false;
    std::optional<FightRecord> record;
    FightRecordSummary summary;
};

ConditionResult runCondition(const EnemyProbe& probe, const TerrainDescriptor& terrain, int maxTicks = kHarnessMaxTicks);

struct HarnessReport {
    std::string probe;
    std::vector<ConditionResult> results;
    bool passed = false; // every condition passed
};

HarnessReport validateEnemy(const EnemyProbe& probe, int maxTicks = kHarnessMaxTicks);
HarnessReport validateEnemy(const EnemyProbe& probe, const std::vector<TerrainDescriptor>& terrain, int maxTicks = kHarnessMaxTicks);

// One line per condition plus a verdict, as plain text for a session log.
std::string formatHarnessReport(const HarnessReport& report);

}
