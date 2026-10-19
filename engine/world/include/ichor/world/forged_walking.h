#pragma once

#include "ichor/core/wading.h"
#include "ichor/traversal/verbs.h"
#include "ichor/world/heightfield.h"
#include "ichor/world/water_field.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ichor {

// Walking on the forged terrain (doc 11). Forged ground is steeper and wetter than the stand-in, so the
// walker follows the same rules the traversal set states: ground steeper than ClimbLimits::minSlopeDegrees
// is never walked (it is climbed, on a designated surface, with the Climb capability), shallow water is
// waded at the wading pace, and water of chest height or more needs Swim or a Boat. A walker that makes
// no headway while trying to move is reported, never left to stand silently.

struct WalkParams {
    float maxSlopeDegrees = 0.0f;  // 0 reads as ClimbLimits{}.minSlopeDegrees, the steepest slope that is walked
    float easeSlopeDegrees = 35.0f; // from here the pace falls away, to minSlopeFactor at maxSlopeDegrees
    float minSlopeFactor = 0.3f;
    float deepWaterDepth = kWadeFull; // metres of water from which walking is no longer possible
    float probeMetres = 1.0f;         // how far ahead of the feet the filter looks
};

enum class WalkBlock : std::uint8_t {
    None,
    TooSteep,  // steeper than the walk limit; needs climbing
    DeepWater, // chest height or more without Swim or a Boat
    OffMap,    // beyond the edge of the world
    Wedged,    // the ground is fine but something (a collider) holds the walker
};

const char* walkBlockName(WalkBlock block);

struct WalkAssessment {
    WalkBlock block = WalkBlock::None;
    float hazard = 0.0f;      // 0 where walkable; how far past the limit otherwise, in a unit per block kind
    float slopeFactor = 1.0f; // pace kept on the slope, 0 when blocked by it
    float wadeFactor = 1.0f;  // pace kept in the water
    float waterDepth = 0.0f;

    bool walkable() const { return block == WalkBlock::None; }
    float speedFactor() const { return slopeFactor * wadeFactor; }
};

// Pure and platform independent: no libm in a decision, so a replay walks the same on every machine. The
// heightfield and the water field (null for dry land) must outlive it.
class ForgedWalking {
public:
    ForgedWalking(const Heightfield& heightfield, const WaterField* water, const CapabilitySet& capabilities, const WalkParams& params = {});

    // Not a number reads as off the map.
    WalkAssessment assess(double x, double z) const;

    // The velocity the walker may keep from `position`. It is the input velocity, slowed on a steep rise,
    // unless the ground a probe ahead is blocked: then it slides along whichever axis stays walkable, or
    // is zero. Ground that is blocked in the same way but no more than where the walker stands is allowed, so a walker
    // that finds itself on blocked ground (a teleport, a spawn) can always walk out. Never NaN.
    glm::dvec3 filterVelocity(const glm::dvec3& position, const glm::dvec3& velocity) const;

    const WalkParams& params() const { return params_; }

    // The pace a walker keeps on blocked ground it is walking out of, where the assessment's own factors are zero.
    float crawlFactor() const { return params_.minSlopeFactor; }

private:
    const Heightfield& heightfield_;
    const WaterField* water_;
    CapabilitySet capabilities_;
    WalkParams params_;
    double cosMax_ = 0.0;
    double cosEase_ = 0.0;
};

// The nearest walkable, dry point to `around`, searched in rings of the sample spacing out to `maxRadius`.
// False when there is none.
bool findWalkableSpawn(const ForgedWalking& walking, double aroundX, double aroundZ, double maxRadius, glm::dvec3& out);

struct StuckParams {
    double windowSeconds = 3.0;       // trying to move for this long...
    double minProgressMetres = 0.75;  // ...without getting this far is stuck
    double safeSpacingMetres = 4.0;   // safe footing is remembered this far apart
    std::size_t safeHistory = 8;
    std::size_t recoverBack = 3;      // recover to the safe point this many remembered ones back
};

struct StuckReport {
    bool stuck = false;
    WalkBlock cause = WalkBlock::None;
    glm::dvec3 where{0.0};
    glm::dvec3 recoverTo{0.0}; // the last safe footing a little way back; equals `where` when none is known
    bool canRecover = false;
    double seconds = 0.0; // how long the walker tried
};

// "stuck at (x, z): too steep (needs climbing)": a line fit for the log or the HUD.
std::string describeStuck(const StuckReport& report);

// Watches a walker that is trying to move. Feed it every tick.
class StuckDetector {
public:
    explicit StuckDetector(const StuckParams& params = {});

    // `ahead` is what the ground a probe in the intended direction would say, so the report can name the
    // blocker; None there means the ground is fine and the walker is wedged. `here` is the ground underfoot.
    StuckReport step(const glm::dvec3& position, bool wantsToMove, const WalkAssessment& here, WalkBlock ahead, double dt);

    // Forget the window and the safe history, after a teleport.
    void reset(const glm::dvec3& position);

private:
    StuckParams params_;
    glm::dvec3 windowStart_{0.0};
    double tryingSeconds_ = 0.0;
    bool windowOpen_ = false;
    std::deque<glm::dvec3> safe_;
};

}
