#include "ichor/ui/landmark_compass.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ichor {

namespace {

constexpr std::size_t kHardMaxLandmarks = 4096;
constexpr std::size_t kHardMaxNameBytes = 64;
constexpr std::size_t kHardMaxResults = 32;

bool validPoint(AnnotationPoint p)
{
    const float maxCoord = AnnotationLimits{}.maxCoordinate;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) <= maxCoord && std::fabs(p.y) <= maxCoord;
}

bool validLandmark(const NavLandmark& lm, const CompassLimits& limits)
{
    const std::size_t maxName = std::min(limits.maxNameBytes, kHardMaxNameBytes);
    return isValidAnnotationId(lm.id, AnnotationLimits{}.maxIdBytes) && !lm.name.empty() && lm.name.size() <= maxName
        && isCleanAnnotationText(lm.name) && validPoint(lm.position);
}

CompassStatus validateLandmarks(const std::vector<NavLandmark>& landmarks, const CompassLimits& limits)
{
    if (landmarks.size() > std::min(limits.maxLandmarks, kHardMaxLandmarks)) {
        return CompassStatus::TooManyLandmarks;
    }
    std::vector<std::string_view> ids;
    ids.reserve(landmarks.size());
    for (const auto& lm : landmarks) {
        if (!validLandmark(lm, limits)) {
            return CompassStatus::InvalidLandmark;
        }
        ids.push_back(lm.id);
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
        return CompassStatus::DuplicateLandmark;
    }
    return CompassStatus::Ok;
}

double squaredDistance(AnnotationPoint a, AnnotationPoint b)
{
    const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
    const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
    return dx * dx + dy * dy;
}

// 22.5 degrees either side of an axis is tan(67.5) = 2.4142 : 1.
int octantIndex(double right, double forward)
{
    const double ar = std::fabs(right);
    const double af = std::fabs(forward);
    if (ar == 0.0 && af == 0.0) {
        return 0;
    }
    constexpr double kTan = 2.41421356;
    if (af * kTan < ar) {
        return right > 0.0 ? 2 : 6;
    }
    if (ar * kTan < af) {
        return forward > 0.0 ? 0 : 4;
    }
    if (forward > 0.0) {
        return right > 0.0 ? 1 : 7;
    }
    return right > 0.0 ? 3 : 5;
}

std::string upperFirst(std::string s)
{
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
        s[0] = static_cast<char>(s[0] - 'a' + 'A');
    }
    return s;
}

struct Candidate {
    const NavLandmark* landmark;
    double d2;
};

bool nearer(const Candidate& a, const Candidate& b)
{
    if (a.d2 != b.d2) {
        return a.d2 < b.d2;
    }
    return a.landmark->id < b.landmark->id;
}

std::string entrySentence(const NavLandmark& lm, CompassPoint bearing, RelativeSector relative, DistanceBand band)
{
    return upperFirst(lm.name) + " lies " + compassPointName(bearing) + ", " + relativeSectorPhrase(relative) + ", "
        + distanceBandPhrase(band) + " off.";
}

} // namespace

const char* compassPointName(CompassPoint p)
{
    switch (p) {
    case CompassPoint::North: return "north";
    case CompassPoint::NorthEast: return "northeast";
    case CompassPoint::East: return "east";
    case CompassPoint::SouthEast: return "southeast";
    case CompassPoint::South: return "south";
    case CompassPoint::SouthWest: return "southwest";
    case CompassPoint::West: return "west";
    case CompassPoint::NorthWest: return "northwest";
    }
    return "north";
}

const char* relativeSectorPhrase(RelativeSector s)
{
    switch (s) {
    case RelativeSector::Ahead: return "straight ahead";
    case RelativeSector::AheadRight: return "ahead and to your right";
    case RelativeSector::Right: return "to your right";
    case RelativeSector::BehindRight: return "behind you and to the right";
    case RelativeSector::Behind: return "behind you";
    case RelativeSector::BehindLeft: return "behind you and to the left";
    case RelativeSector::Left: return "to your left";
    case RelativeSector::AheadLeft: return "ahead and to your left";
    }
    return "straight ahead";
}

const char* distanceBandPhrase(DistanceBand d)
{
    switch (d) {
    case DistanceBand::StonesThrow: return "a stone's throw";
    case DistanceBand::ShortWalk: return "a short walk";
    case DistanceBand::GoodWalk: return "a good walk";
    case DistanceBand::LongWay: return "a long way";
    }
    return "a long way";
}

CompassPoint bearingOf(double dx, double dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return CompassPoint::North;
    }
    return static_cast<CompassPoint>(octantIndex(dx, dy));
}

RelativeSector relativeSectorOf(double dx, double dy, double facingX, double facingY)
{
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(facingX) || !std::isfinite(facingY)) {
        return RelativeSector::Ahead;
    }
    const double forward = dx * facingX + dy * facingY;
    const double right = dx * facingY - dy * facingX;
    return static_cast<RelativeSector>(octantIndex(right, forward));
}

DistanceBand distanceBandFor(double distanceSquared)
{
    if (!(distanceSquared >= 50.0 * 50.0)) {
        return DistanceBand::StonesThrow;
    }
    if (distanceSquared < 250.0 * 250.0) {
        return DistanceBand::ShortWalk;
    }
    if (distanceSquared < 1000.0 * 1000.0) {
        return DistanceBand::GoodWalk;
    }
    return DistanceBand::LongWay;
}

CompassReading readCompass(AnnotationPoint from, AnnotationPoint facing, const std::vector<NavLandmark>& landmarks,
    const CompassLimits& limits)
{
    CompassReading out;
    if (!validPoint(from)) {
        out.status = CompassStatus::InvalidPosition;
        return out;
    }
    if (!std::isfinite(facing.x) || !std::isfinite(facing.y) || (facing.x == 0.0f && facing.y == 0.0f)) {
        out.status = CompassStatus::InvalidFacing;
        return out;
    }
    out.status = validateLandmarks(landmarks, limits);
    if (out.status != CompassStatus::Ok) {
        return out;
    }
    out.facing = bearingOf(facing.x, facing.y);

    std::vector<Candidate> candidates;
    candidates.reserve(landmarks.size());
    for (const auto& lm : landmarks) {
        candidates.push_back({&lm, squaredDistance(from, lm.position)});
    }
    std::sort(candidates.begin(), candidates.end(), nearer);
    candidates.resize(std::min({candidates.size(), limits.maxResults, kHardMaxResults}));

    for (const auto& c : candidates) {
        const double dx = static_cast<double>(c.landmark->position.x) - from.x;
        const double dy = static_cast<double>(c.landmark->position.y) - from.y;
        CompassEntry e;
        e.landmarkId = c.landmark->id;
        e.name = c.landmark->name;
        e.bearing = bearingOf(dx, dy);
        e.relative = relativeSectorOf(dx, dy, facing.x, facing.y);
        e.distance = distanceBandFor(c.d2);
        e.distanceSquared = c.d2;
        e.text = entrySentence(*c.landmark, e.bearing, e.relative, e.distance);
        out.nearest.push_back(std::move(e));
    }
    return out;
}

RouteDirections describeRoute(AnnotationPoint from, const NavLandmark& target, const std::vector<NavLandmark>& landmarks,
    const CompassLimits& limits)
{
    RouteDirections out;
    if (!validPoint(from)) {
        out.status = CompassStatus::InvalidPosition;
        return out;
    }
    if (!validLandmark(target, limits)) {
        out.status = CompassStatus::InvalidLandmark;
        return out;
    }
    out.status = validateLandmarks(landmarks, limits);
    if (out.status != CompassStatus::Ok) {
        return out;
    }

    const double directSq = squaredDistance(from, target.position);
    const NavLandmark* via = nullptr;
    double viaSq = 0.0;
    for (const auto& lm : landmarks) {
        if (lm.id == target.id || squaredDistance(from, lm.position) == 0.0) {
            continue;
        }
        const double d2 = squaredDistance(lm.position, target.position);
        if (d2 >= directSq || squaredDistance(from, lm.position) >= directSq) {
            continue;
        }
        if (via == nullptr || d2 < viaSq || (d2 == viaSq && lm.id < via->id)) {
            via = &lm;
            viaSq = d2;
        }
    }

    const double dx = static_cast<double>(target.position.x) - from.x;
    const double dy = static_cast<double>(target.position.y) - from.y;
    const DistanceBand band = distanceBandFor(directSq);

    if (directSq == 0.0) {
        out.text = "You are standing at " + target.name + ".";
        return out;
    }
    if (via == nullptr) {
        out.text = std::string("Head ") + compassPointName(bearingOf(dx, dy)) + " to " + target.name + ", "
            + distanceBandPhrase(band) + " off.";
        return out;
    }

    const CompassPoint first = bearingOf(static_cast<double>(via->position.x) - from.x, static_cast<double>(via->position.y) - from.y);
    const CompassPoint second = bearingOf(static_cast<double>(target.position.x) - via->position.x,
        static_cast<double>(target.position.y) - via->position.y);
    out.viaId = via->id;
    out.text = upperFirst(compassPointName(first)) + " past " + via->name + ", then " + compassPointName(second)
        + " to " + target.name + ", " + distanceBandPhrase(band) + " off.";
    return out;
}

} // namespace ichor
