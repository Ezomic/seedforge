#include "ichor/stage0/telegraph_geometry.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

namespace {

constexpr double kPi = 3.14159265358979323846;

using Segments = std::vector<LineSegment>;

struct Frame {
    glm::dvec3 origin;
    glm::dvec3 forward;
    glm::dvec3 right;
    glm::dvec3 at(double u, double v) const { return origin + forward * u + right * v; }
};

// The extent of a cue in its own frame: u runs along the facing, v across it.
struct Extent {
    double u0 = 0.0;
    double u1 = 0.0;
    double half = 0.0;
    bool widens = false; // a cone: the width grows from the apex
};

void push(Segments& out, const Frame& f, double u0, double v0, double u1, double v1, glm::vec3 color)
{
    out.push_back({f.at(u0, v0), f.at(u1, v1), color});
}

void ring(Segments& out, const Frame& f, double cu, double radius, glm::vec3 color, int segments)
{
    for (int i = 0; i < segments; ++i) {
        const double a0 = 2.0 * kPi * i / segments;
        const double a1 = 2.0 * kPi * (i + 1) / segments;
        push(out, f, cu + std::cos(a0) * radius, std::sin(a0) * radius, cu + std::cos(a1) * radius, std::sin(a1) * radius, color);
    }
}

// Splits every segment into `pieces` and keeps the even ones.
void gap(Segments& segs, int pieces)
{
    Segments kept;
    for (const LineSegment& s : segs) {
        const glm::dvec3 step = (s.b - s.a) / static_cast<double>(pieces);
        for (int i = 0; i < pieces; i += 2) {
            kept.push_back({s.a + step * static_cast<double>(i), s.a + step * static_cast<double>(i + 1), s.color});
        }
    }
    segs = std::move(kept);
}

Extent outline(TelegraphShape shape, const TelegraphDraw& d, const Frame& f, Segments& out)
{
    const double p = d.progress;
    const double len = d.reach * p;
    const double w = d.halfWidth * p;
    const glm::vec3 c = d.color;
    switch (shape) {
    case TelegraphShape::Cone:
        push(out, f, 0, 0, len, -w, c);
        push(out, f, 0, 0, len, w, c);
        push(out, f, len, -w, len, w, c);
        return {0.0, len, w, true};
    case TelegraphShape::Line:
        push(out, f, 0, 0, len, 0, c);
        push(out, f, len, -0.12, len, 0.12, c);
        return {0.0, len, 0.12, false};
    case TelegraphShape::Ring: {
        const double r = d.halfWidth * (0.4 + 0.6 * p);
        ring(out, f, len, r, c, 16);
        return {len - r, len + r, r, false};
    }
    case TelegraphShape::Pulse: {
        const double r = d.halfWidth * (0.3 + 0.7 * p);
        ring(out, f, 0.0, r, c, 16);
        return {-r, r, r, false};
    }
    case TelegraphShape::GroundMarker: {
        const double r = d.halfWidth * (0.4 + 0.6 * p);
        push(out, f, len + r, 0, len, r, c);
        push(out, f, len, r, len - r, 0, c);
        push(out, f, len - r, 0, len, -r, c);
        push(out, f, len, -r, len + r, 0, c);
        return {len - r, len + r, r, false};
    }
    case TelegraphShape::None: break;
    }
    return {};
}

void addPattern(TelegraphPattern pattern, const Extent& e, const Frame& f, glm::vec3 color, Segments& out)
{
    const double span = e.u1 - e.u0;
    switch (pattern) {
    case TelegraphPattern::Dashed: gap(out, 4); break;
    case TelegraphPattern::Dotted: gap(out, 12); break;
    case TelegraphPattern::Hatched:
        for (int i = 1; i <= 5; ++i) {
            const double u = e.u0 + span * i / 6.0;
            const double v = e.widens ? e.half * i / 6.0 : e.half;
            push(out, f, u, -v, u, v, color);
        }
        break;
    case TelegraphPattern::Chevrons:
        for (int i = 1; i <= 3; ++i) {
            const double u = e.u0 + span * i / 4.0;
            push(out, f, u - span * 0.1, -e.half * 0.6, u, 0, color);
            push(out, f, u, 0, u - span * 0.1, e.half * 0.6, color);
        }
        break;
    case TelegraphPattern::Solid:
    case TelegraphPattern::None: break;
    }
}

bool finite(glm::dvec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

} // namespace

void appendTelegraphLines(const VisualTelegraph& cue, const TelegraphDraw& draw, std::vector<LineSegment>& out)
{
    if (!finite(draw.origin) || !finite(glm::dvec3(draw.forward)) || !std::isfinite(draw.reach) || !std::isfinite(draw.halfWidth) ||
        !std::isfinite(draw.progress) || cue.shape == TelegraphShape::None) {
        return;
    }
    const glm::dvec3 forward(draw.forward);
    if (glm::dot(forward, forward) < 1e-12) return;
    const glm::dvec3 f = glm::normalize(forward);
    TelegraphDraw d = draw;
    d.progress = std::clamp(draw.progress, 0.0f, 1.0f);
    d.reach = std::clamp(draw.reach, 0.0f, 1000.0f);
    d.halfWidth = std::clamp(draw.halfWidth, 0.0f, 1000.0f);
    const Frame frame{draw.origin, f, glm::dvec3(-f.z, 0.0, f.x)};
    Segments segs;
    const Extent extent = outline(cue.shape, d, frame, segs);
    addPattern(cue.pattern, extent, frame, d.color, segs);
    out.insert(out.end(), segs.begin(), segs.end());
}

bool appendOffscreenArrow(const CameraPose& camera, glm::dvec3 target, float targetRadius, glm::vec3 color, std::vector<LineSegment>& out)
{
    if (!finite(target) || !finite(camera.position) || inFrustum(camera, target, targetRadius)) return false;
    constexpr double depth = 1.5;
    const glm::vec3 view = glm::conjugate(camera.rotation) * glm::vec3(target - camera.position);
    const double a = offscreenBearing(0.0f, 0.0f, view.x, view.y) * (kPi / 180.0);
    const glm::dvec2 dir(std::cos(a), std::sin(a));
    const double hx = depth * kTanHalfFovX * 0.85;
    const double hy = depth * kTanHalfFovY * 0.85;
    const double reachX = std::abs(dir.x) > 1e-9 ? hx / std::abs(dir.x) : 1e9;
    const double reachY = std::abs(dir.y) > 1e-9 ? hy / std::abs(dir.y) : 1e9;
    const glm::dvec2 tip = dir * std::min(reachX, reachY);
    const auto world = [&](glm::dvec2 p) {
        return camera.position + glm::dvec3(camera.rotation * glm::vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(-depth)));
    };
    const auto wing = [&](double turn) {
        const double wa = a + kPi + turn;
        return tip + glm::dvec2(std::cos(wa), std::sin(wa)) * 0.22;
    };
    out.push_back({world(tip - dir * 0.5), world(tip), color});
    out.push_back({world(tip), world(wing(0.5)), color});
    out.push_back({world(tip), world(wing(-0.5)), color});
    return true;
}

void appendWindupTelegraph(bool heavy, const TelegraphDraw& draw, const CameraPose* camera, float foeRadius, std::vector<LineSegment>& out)
{
    const VisualTelegraph* cue = findVisualTelegraph(visualTelegraphTable(), TelegraphRole::Skirmisher, heavy ? "heavy_windup" : "light_windup");
    if (cue == nullptr) return;
    appendTelegraphLines(*cue, draw, out);
    if (camera != nullptr && cue->offscreenArrow) appendOffscreenArrow(*camera, draw.origin, foeRadius, draw.color, out);
}

} // namespace ichor::stage0
