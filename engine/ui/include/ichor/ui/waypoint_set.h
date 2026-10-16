#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The optional accessibility waypoint (docs/11-traversal-map.md, docs/13-ui-hud.md).
// The game has no waypoints by default, so the setting is off until the player
// turns it on, and while it is off guide() returns nothing at all: no bearing,
// no distance, no id. Pure data: nothing here draws or reads the save file.
// Positions are map coordinates with +y north and +x east; headings and
// bearings are radians measured clockwise from north.

struct WaypointSettings {
    bool enabled = false;
};

struct WaypointLimits {
    std::size_t maxWaypoints = 16;
    std::size_t maxIdBytes = 48;
    float maxCoordinate = 1.0e6f; // |x| and |y| must not exceed this
};

struct WaypointPosition {
    float x = 0.0f;
    float y = 0.0f;
};

struct Waypoint {
    std::string id; // 1..maxIdBytes of [A-Za-z0-9_.-], unique within the set
    WaypointPosition position;
};

enum class WaypointResult {
    Ok,
    InvalidId,
    DuplicateId,
    UnknownId,
    InvalidPosition, // NaN, infinity, or beyond maxCoordinate
    TooMany,
};

struct WaypointGuide {
    std::string id;
    float bearing = 0.0f;         // absolute, in [0, 2pi)
    float relativeBearing = 0.0f; // to the player's heading, in (-pi, pi]; positive is to the right
    float distance = 0.0f;        // map units, never negative
};

class WaypointSet {
public:
    explicit WaypointSet(WaypointLimits limits = {});

    // The first waypoint added becomes active. Rejected calls leave the set untouched.
    WaypointResult add(Waypoint waypoint);
    // Removing the active waypoint makes the earliest remaining one active.
    WaypointResult remove(std::string_view id);
    WaypointResult setActive(std::string_view id);

    const Waypoint* find(std::string_view id) const;
    const Waypoint* active() const;
    const std::vector<Waypoint>& all() const { return items_; }
    std::size_t size() const { return items_.size(); }

    // Nothing when the setting is off, there is no active waypoint, or the
    // player position or heading is not finite and within range.
    std::optional<WaypointGuide> guide(const WaypointSettings& settings, WaypointPosition player, float heading) const;

private:
    bool validPosition(WaypointPosition position) const;

    WaypointLimits limits_;
    std::vector<Waypoint> items_;
    std::size_t active_ = 0; // index into items_, meaningful only when items_ is not empty
};

} // namespace ichor
