#include "ichor/world/placement_clear.h"

#include <cmath>

namespace ichor {

namespace {

bool inRange(double v)
{
    return std::isfinite(v) && v >= -kMaxPlacementCoordinate && v <= kMaxPlacementCoordinate;
}

PlacementCheck checkValidated(const PlacementCandidate& candidate, const ClearRadiusRules& rules, const std::vector<PlacementObstacle>& obstacles)
{
    PlacementCheck result;
    if (!inRange(candidate.x) || !inRange(candidate.z))
        return result;

    const double r = rules.clearRadius;
    const double extent = rules.bounds.extent;
    if (candidate.x - r < 0.0 || candidate.z - r < 0.0 || candidate.x + r > extent || candidate.z + r > extent) {
        result.verdict = PlacementVerdict::OutOfBounds;
        return result;
    }

    for (std::size_t i = 0; i < obstacles.size(); ++i) {
        const PlacementObstacle& o = obstacles[i];
        const double dx = o.x - candidate.x;
        const double dz = o.z - candidate.z;
        const double reach = r + o.radius;
        if (dx * dx + dz * dz < reach * reach) {
            if (result.blockerCount == 0)
                result.firstBlocker = i;
            ++result.blockerCount;
        }
    }
    result.verdict = result.blockerCount == 0 ? PlacementVerdict::Clear : PlacementVerdict::Blocked;
    return result;
}

}

std::string_view obstacleKindName(ObstacleKind kind)
{
    switch (kind) {
    case ObstacleKind::Foliage: return "foliage";
    case ObstacleKind::Rock: return "rock";
    case ObstacleKind::Water: return "water";
    case ObstacleKind::Cliff: return "cliff";
    }
    return "unknown";
}

std::vector<std::string> validateClearRadiusRules(const ClearRadiusRules& rules)
{
    std::vector<std::string> problems;
    const bool radiusOk = inRange(rules.clearRadius) && rules.clearRadius > 0.0;
    const bool extentOk = inRange(rules.bounds.extent) && rules.bounds.extent > 0.0;
    if (!radiusOk)
        problems.push_back("clear radius must be finite, positive and within range");
    if (!extentOk)
        problems.push_back("world extent must be finite, positive and within range");
    if (radiusOk && extentOk && rules.clearRadius * 2.0 > rules.bounds.extent)
        problems.push_back("clear disc is wider than the world");
    return problems;
}

std::vector<std::string> validateObstacles(const std::vector<PlacementObstacle>& obstacles)
{
    std::vector<std::string> problems;
    if (obstacles.size() > kMaxPlacementObstacles) {
        problems.push_back("too many obstacles");
        return problems;
    }
    for (std::size_t i = 0; i < obstacles.size(); ++i) {
        const PlacementObstacle& o = obstacles[i];
        if (!inRange(o.x) || !inRange(o.z) || !inRange(o.radius) || o.radius < 0.0)
            problems.push_back("obstacle [" + std::to_string(i) + "] has a non-finite, out-of-range or negative value");
    }
    return problems;
}

PlacementCheck checkPlacement(const PlacementCandidate& candidate, const ClearRadiusRules& rules, const std::vector<PlacementObstacle>& obstacles)
{
    if (!validateClearRadiusRules(rules).empty() || !validateObstacles(obstacles).empty())
        return {};
    return checkValidated(candidate, rules, obstacles);
}

std::vector<std::size_t> filterClearPlacements(const std::vector<PlacementCandidate>& candidates, const ClearRadiusRules& rules, const std::vector<PlacementObstacle>& obstacles, std::vector<std::string>& problems)
{
    std::vector<std::size_t> kept;
    const auto ruleProblems = validateClearRadiusRules(rules);
    const auto obstacleProblems = validateObstacles(obstacles);
    problems.insert(problems.end(), ruleProblems.begin(), ruleProblems.end());
    problems.insert(problems.end(), obstacleProblems.begin(), obstacleProblems.end());
    if (!ruleProblems.empty() || !obstacleProblems.empty())
        return kept;

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const PlacementCheck check = checkValidated(candidates[i], rules, obstacles);
        if (check.verdict == PlacementVerdict::Invalid)
            problems.push_back("candidate [" + std::to_string(i) + "] is non-finite or out of range");
        else if (check.verdict == PlacementVerdict::Clear)
            kept.push_back(i);
    }
    return kept;
}

}
