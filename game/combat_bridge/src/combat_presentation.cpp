#include "ichor/combat_bridge/combat_presentation.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr double kLiftM = 0.06;       // above the terrain, so the lines do not z-fight with it
constexpr double kRowSpacingM = 0.06; // between the fill's cross lines
constexpr double kSegmentM = 0.25;    // the longest straight piece, so a line follows the ground's slope
constexpr double kMaxReachM = 50.0;   // a cone longer than this is a broken tuning, not an attack

glm::vec3 colourOf(TelegraphPhase phase)
{
    switch (phase) {
    case TelegraphPhase::Windup: return {1.0f, 0.7f, 0.15f};
    case TelegraphPhase::Active: return {1.0f, 0.12f, 0.1f};
    case TelegraphPhase::Recovery: return {0.55f, 0.42f, 0.2f};
    case TelegraphPhase::None: break;
    }
    return {1.0f, 1.0f, 1.0f};
}

float fraction(float elapsed, float duration)
{
    if (!std::isfinite(elapsed) || !std::isfinite(duration) || duration <= 0.0f) return 1.0f;
    return std::clamp(elapsed / duration, 0.0f, 1.0f);
}

void ground(const glm::dvec3& feet, glm::dvec3& point, const std::function<double(double, double)>& groundHeight)
{
    const double y = groundHeight ? groundHeight(point.x, point.z) : feet.y;
    point.y = (std::isfinite(y) ? y : feet.y) + kLiftM;
}

void line(glm::dvec3 a, glm::dvec3 b, glm::vec3 colour, const glm::dvec3& feet, const std::function<double(double, double)>& groundHeight,
    std::vector<LineSegment>& out)
{
    const int pieces = std::max(1, static_cast<int>(std::ceil(glm::length(b - a) / kSegmentM)));
    glm::dvec3 from = a;
    ground(feet, from, groundHeight);
    for (int i = 1; i <= pieces; ++i) {
        glm::dvec3 to = glm::mix(a, b, static_cast<double>(i) / pieces);
        ground(feet, to, groundHeight);
        out.push_back({from, to, colour});
        from = to;
    }
}

}

EnemyTelegraph enemyTelegraph(const stage0::Combatant& enemy, const stage0::Tuning& tuning)
{
    const bool heavy = enemy.attack == static_cast<std::uint8_t>(stage0::AttackKind::Heavy);
    switch (enemy.combatState()) {
    case stage0::CombatState::Windup: return {TelegraphPhase::Windup, fraction(enemy.stateTime, heavy ? tuning.heavyWindup : tuning.lightWindup)};
    case stage0::CombatState::Active: return {TelegraphPhase::Active, 1.0f};
    case stage0::CombatState::Recovery: return {TelegraphPhase::Recovery, 1.0f - fraction(enemy.stateTime, heavy ? tuning.heavyRecovery : tuning.lightRecovery)};
    default: break;
    }
    return {};
}

CombatDebugAids combatDebugAids(OverlayMode overlay, bool forced)
{
    const bool on = forced || overlay == OverlayMode::Full;
    return {.stageLines = on, .colliders = on};
}

void appendEnemyTelegraph(const EnemyTelegraph& telegraph, glm::dvec3 feet, glm::vec3 forward, const stage0::Tuning& tuning,
    const std::function<double(double, double)>& groundHeight, std::vector<LineSegment>& out)
{
    if (telegraph.phase == TelegraphPhase::None || !std::isfinite(feet.x + feet.y + feet.z) || !std::isfinite(forward.x + forward.z)) return;
    const glm::dvec3 flat(forward.x, 0.0, forward.z);
    if (glm::dot(flat, flat) < 1e-12) return;
    const glm::dvec3 f = glm::normalize(flat);
    const glm::dvec3 r(-f.z, 0.0, f.x);

    const double reach = static_cast<double>(tuning.attackRange) + tuning.attackRadius;
    const double baseHalf = tuning.bodyRadius;
    const double endHalf = tuning.attackRadius;
    if (!std::isfinite(reach + baseHalf + endHalf) || reach < 0.0 || reach > kMaxReachM || baseHalf < 0.0 || endHalf < 0.0 || baseHalf > kMaxReachM || endHalf > kMaxReachM) return;
    const auto halfAt = [&](double u) { return baseHalf + (endHalf - baseHalf) * (reach > 0.0 ? u / reach : 0.0); };
    const auto at = [&](double u, double v) { return feet + f * u + r * v; };

    const glm::vec3 colour = colourOf(telegraph.phase);
    const glm::vec3 outline = telegraph.phase == TelegraphPhase::Windup ? colour * 0.6f : colour;
    line(at(0.0, -baseHalf), at(reach, -endHalf), outline, feet, groundHeight, out);
    line(at(0.0, baseHalf), at(reach, endHalf), outline, feet, groundHeight, out);
    line(at(reach, -endHalf), at(reach, endHalf), outline, feet, groundHeight, out);

    const double filled = reach * std::clamp(static_cast<double>(telegraph.progress), 0.0, 1.0);
    for (double u = kRowSpacingM; u <= filled + 1e-9; u += kRowSpacingM) {
        line(at(u, -halfAt(u)), at(u, halfAt(u)), colour, feet, groundHeight, out);
    }
    if (filled > 0.0) {
        line(at(0.0, 0.0), at(filled, 0.0), colour, feet, groundHeight, out);
    }
}

}
