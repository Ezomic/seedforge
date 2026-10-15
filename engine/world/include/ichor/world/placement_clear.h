#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// What kind of geometry makes a fight unreadable (doc 05, readability).
enum class ObstacleKind { Foliage, Rock, Water, Cliff };

std::string_view obstacleKindName(ObstacleKind kind);

// A disc of unreadable geometry on the ground plane, world metres.
struct PlacementObstacle {
    double x = 0.0;
    double z = 0.0;
    double radius = 0.0;
    ObstacleKind kind = ObstacleKind::Foliage;
};

// The world covers [0, extent] on both axes.
struct PlacementBounds {
    double extent = 0.0;
};

// A fight needs a disc of clearRadius around it that stays inside the world and
// overlaps no obstacle.
struct ClearRadiusRules {
    double clearRadius = 0.0;
    PlacementBounds bounds;
};

struct PlacementCandidate {
    double x = 0.0;
    double z = 0.0;
};

constexpr double kMaxPlacementCoordinate = 1.0e6;
constexpr std::size_t kMaxPlacementObstacles = 1u << 20;

enum class PlacementVerdict { Clear, Blocked, OutOfBounds, Invalid };

struct PlacementCheck {
    PlacementVerdict verdict = PlacementVerdict::Invalid;
    // Lowest-index intruding obstacle; meaningful only when Blocked.
    std::size_t firstBlocker = 0;
    std::size_t blockerCount = 0;
};

// Problems with the rules: non-finite, non-positive or oversized values, a clear
// disc wider than the world. Empty when usable.
std::vector<std::string> validateClearRadiusRules(const ClearRadiusRules& rules);

// Problems with the obstacle list: non-finite, oversized or negative values, too
// many obstacles. Empty when usable.
std::vector<std::string> validateObstacles(const std::vector<PlacementObstacle>& obstacles);

// One candidate against the rules and obstacles. An obstacle blocks when its disc
// overlaps the clear disc (touching does not). Invalid on a non-finite candidate,
// bad rules or bad obstacles. Squared comparisons only, no libm.
PlacementCheck checkPlacement(const PlacementCandidate& candidate, const ClearRadiusRules& rules, const std::vector<PlacementObstacle>& obstacles);

// Indices of the candidates that pass, in input order. Bad rules or obstacles are
// appended to `problems` and yield no indices; an invalid candidate is reported and
// skipped. Never throws on bad input.
std::vector<std::size_t> filterClearPlacements(const std::vector<PlacementCandidate>& candidates, const ClearRadiusRules& rules, const std::vector<PlacementObstacle>& obstacles, std::vector<std::string>& problems);

}
