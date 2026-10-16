#include "ichor/stage0/visual_telegraph.h"

#include "ichor/core/dmath.h"

#include <cmath>

namespace ichor::stage0 {

namespace {

constexpr TelegraphRole kRoles[] = {TelegraphRole::Skirmisher, TelegraphRole::Swarm,     TelegraphRole::Ambusher,
                                    TelegraphRole::Anchor,     TelegraphRole::Artillery, TelegraphRole::Controller};

bool roleKnown(TelegraphRole role)
{
    for (const TelegraphRole r : kRoles)
        if (r == role) return true;
    return false;
}

bool shapeValid(TelegraphShape s) { return s >= TelegraphShape::Ring && s <= TelegraphShape::Pulse; }
bool patternValid(TelegraphPattern p) { return p >= TelegraphPattern::Solid && p <= TelegraphPattern::Chevrons; }
bool directionValid(TelegraphDirection d) { return d >= TelegraphDirection::AtAttacker && d <= TelegraphDirection::Radial; }

bool visible(const VisualTelegraph& v)
{
    return roleKnown(v.role) && shapeValid(v.shape) && patternValid(v.pattern) && directionValid(v.direction) && v.durationTicks > 0;
}

} // namespace

const std::vector<AudioTelegraph>& audioTelegraphs()
{
    static const std::vector<AudioTelegraph> list{
        {TelegraphRole::Skirmisher, "tell"},       {TelegraphRole::Skirmisher, "light_windup"}, {TelegraphRole::Skirmisher, "heavy_windup"},
        {TelegraphRole::Swarm, "lunge"},           {TelegraphRole::Ambusher, "ambush"},         {TelegraphRole::Ambusher, "strike"},
        {TelegraphRole::Anchor, "slam"},           {TelegraphRole::Artillery, "shell"},         {TelegraphRole::Controller, "cast"},
    };
    return list;
}

// Durations are the wind-up lengths in the role tunings, at the 60 Hz fixed step.
const std::vector<VisualTelegraph>& visualTelegraphTable()
{
    using S = TelegraphShape;
    using P = TelegraphPattern;
    using D = TelegraphDirection;
    static const std::vector<VisualTelegraph> table{
        {TelegraphRole::Skirmisher, "tell", S::Pulse, P::Dotted, D::AtAttacker, 9, true},
        {TelegraphRole::Skirmisher, "light_windup", S::Cone, P::Solid, D::TowardTarget, 21, true},
        {TelegraphRole::Skirmisher, "heavy_windup", S::Cone, P::Hatched, D::TowardTarget, 42, true},
        {TelegraphRole::Swarm, "lunge", S::Line, P::Dashed, D::TowardTarget, 30, true},
        {TelegraphRole::Ambusher, "ambush", S::Line, P::Chevrons, D::TowardTarget, 12, true},
        {TelegraphRole::Ambusher, "strike", S::Line, P::Solid, D::TowardTarget, 30, true},
        {TelegraphRole::Anchor, "slam", S::Ring, P::Hatched, D::Radial, 60, true},
        {TelegraphRole::Artillery, "shell", S::GroundMarker, P::Chevrons, D::AtGroundPoint, 50, true},
        {TelegraphRole::Controller, "cast", S::Ring, P::Dotted, D::AtAttacker, 45, true},
    };
    return table;
}

const VisualTelegraph* findVisualTelegraph(const std::vector<VisualTelegraph>& table, TelegraphRole role, std::string_view attack)
{
    for (const VisualTelegraph& v : table)
        if (v.role == role && v.attack == attack) return &v;
    return nullptr;
}

VisualTelegraphReport validateVisualTelegraphs(const std::vector<VisualTelegraph>& table, const std::vector<AudioTelegraph>& audio)
{
    VisualTelegraphReport report;
    for (const AudioTelegraph& a : audio) {
        const VisualTelegraph* v = roleKnown(a.role) ? findVisualTelegraph(table, a.role, a.attack) : nullptr;
        if (v == nullptr) report.missing.push_back(a);
    }
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (!visible(table[i])) report.invalid.push_back(table[i]);
        for (std::size_t j = 0; j < i; ++j)
            if (table[j].role == table[i].role && table[j].attack == table[i].attack) {
                report.duplicates.push_back(table[i]);
                break;
            }
    }
    return report;
}

const char* name(TelegraphRole role)
{
    switch (role) {
    case TelegraphRole::Skirmisher: return "skirmisher";
    case TelegraphRole::Swarm: return "swarm";
    case TelegraphRole::Ambusher: return "ambusher";
    case TelegraphRole::Anchor: return "anchor";
    case TelegraphRole::Artillery: return "artillery";
    case TelegraphRole::Controller: return "controller";
    }
    return "unknown";
}

bool parseTelegraphRole(std::string_view text, TelegraphRole& out)
{
    for (const TelegraphRole r : kRoles)
        if (text == name(r)) {
            out = r;
            return true;
        }
    return false;
}

int offscreenBearing(float fromX, float fromY, float toX, float toY)
{
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    if (!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.0f && dy == 0.0f)) return 0;
    const float deg = dmath::atan2(dy, dx) * (180.0f / dmath::kPi);
    if (!std::isfinite(deg)) return 0;
    int d = static_cast<int>(deg + (deg < 0.0f ? -0.5f : 0.5f)) % 360;
    return d < 0 ? d + 360 : d;
}

} // namespace ichor::stage0
