#include "ichor/ui/waypoint_set.h"

#include "ichor/core/dmath.h"
#include "ichor/ui/map_annotation_store.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kPi = 3.1415927410125732; // pi as a float, so a bearing of exactly pi stays +pi

} // namespace

WaypointSet::WaypointSet(WaypointLimits limits)
    : limits_(limits)
{
}

bool WaypointSet::validPosition(WaypointPosition position) const
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::fabs(position.x) <= limits_.maxCoordinate
        && std::fabs(position.y) <= limits_.maxCoordinate;
}

WaypointResult WaypointSet::add(Waypoint waypoint)
{
    if (!isValidAnnotationId(waypoint.id, limits_.maxIdBytes)) {
        return WaypointResult::InvalidId;
    }
    if (find(waypoint.id) != nullptr) {
        return WaypointResult::DuplicateId;
    }
    if (!validPosition(waypoint.position)) {
        return WaypointResult::InvalidPosition;
    }
    if (items_.size() >= limits_.maxWaypoints) {
        return WaypointResult::TooMany;
    }
    waypoint.position.x += 0.0f; // -0.0 becomes +0.0
    waypoint.position.y += 0.0f;
    items_.push_back(std::move(waypoint));
    return WaypointResult::Ok;
}

WaypointResult WaypointSet::remove(std::string_view id)
{
    const auto it = std::find_if(items_.begin(), items_.end(), [&](const Waypoint& w) { return w.id == id; });
    if (it == items_.end()) {
        return WaypointResult::UnknownId;
    }
    const auto index = static_cast<std::size_t>(it - items_.begin());
    items_.erase(it);
    if (index < active_) {
        --active_;
    } else if (index == active_) {
        active_ = 0;
    }
    return WaypointResult::Ok;
}

WaypointResult WaypointSet::setActive(std::string_view id)
{
    const auto it = std::find_if(items_.begin(), items_.end(), [&](const Waypoint& w) { return w.id == id; });
    if (it == items_.end()) {
        return WaypointResult::UnknownId;
    }
    active_ = static_cast<std::size_t>(it - items_.begin());
    return WaypointResult::Ok;
}

const Waypoint* WaypointSet::find(std::string_view id) const
{
    const auto it = std::find_if(items_.begin(), items_.end(), [&](const Waypoint& w) { return w.id == id; });
    return it == items_.end() ? nullptr : &*it;
}

const Waypoint* WaypointSet::active() const
{
    return items_.empty() ? nullptr : &items_[active_];
}

std::optional<WaypointGuide> WaypointSet::guide(const WaypointSettings& settings, WaypointPosition player, float heading) const
{
    if (!settings.enabled) {
        return std::nullopt;
    }
    const Waypoint* target = active();
    if (target == nullptr || !validPosition(player) || !std::isfinite(heading)) {
        return std::nullopt;
    }

    const double dx = static_cast<double>(target->position.x) - static_cast<double>(player.x);
    const double dy = static_cast<double>(target->position.y) - static_cast<double>(player.y);

    WaypointGuide out;
    out.id = target->id;
    out.distance = static_cast<float>(std::sqrt(dx * dx + dy * dy));

    double bearing = static_cast<double>(dmath::atan2(static_cast<float>(dx), static_cast<float>(dy)));
    if (bearing < 0.0) {
        bearing += kTwoPi;
    }
    if (bearing >= kTwoPi) {
        bearing -= kTwoPi;
    }
    out.bearing = static_cast<float>(bearing);
    if (out.bearing >= static_cast<float>(kTwoPi)) {
        out.bearing = 0.0f;
    }

    double relative = std::fmod(bearing - static_cast<double>(heading), kTwoPi);
    if (relative > kPi) {
        relative -= kTwoPi;
    } else if (relative <= -kPi) {
        relative += kTwoPi;
    }
    out.relativeBearing = static_cast<float>(relative);
    return out;
}

} // namespace ichor
