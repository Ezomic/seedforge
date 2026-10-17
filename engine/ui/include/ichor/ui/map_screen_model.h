#pragma once

#include "ichor/ui/map_annotation_store.h"
#include "ichor/ui/waypoint_set.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// View model for the minimal map screen (docs/13-ui-hud.md): which of the player's annotations are in
// view, where the player marker and the waypoint land, and how the viewport pans and zooms. Pure data
// and float math: nothing here draws, reads the save file or touches the network. Map coordinates have
// +y north and +x east; screen coordinates are pixels with +y down, origin at the top-left of the view.

constexpr float kMapViewMinZoom = 0.05f; // pixels per map unit
constexpr float kMapViewMaxZoom = 64.0f;
constexpr float kMapViewMaxSize = 16384.0f; // pixels, per side
constexpr float kMapViewMaxCoordinate = 1.0e6f;

struct MapViewport {
    float centerX = 0.0f; // map coordinates
    float centerY = 0.0f;
    float zoom = 1.0f;
    float width = 0.0f; // pixels
    float height = 0.0f;
};

struct MapScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// False for a non-finite or out-of-range field, a zoom outside [kMapViewMinZoom, kMapViewMaxZoom], or a
// size that is not in (0, kMapViewMaxSize].
bool isValidMapViewport(const MapViewport& viewport);

// Nothing when the viewport or the position is invalid or the position is beyond kMapViewMaxCoordinate.
std::optional<MapScreenPoint> mapWorldToScreen(const MapViewport& viewport, float x, float y);

// Moves the view by a screen-space drag. An invalid viewport is returned unchanged; a non-finite delta
// is ignored. The centre stays within +-kMapViewMaxCoordinate.
MapViewport panMapViewport(const MapViewport& viewport, float screenDx, float screenDy);

// Multiplies the zoom (clamped to the zoom limits) keeping the map point under (screenX, screenY) fixed.
// A non-finite or non-positive factor, or an invalid viewport or anchor, returns the viewport unchanged.
MapViewport zoomMapViewport(const MapViewport& viewport, float factor, float screenX, float screenY);

MapViewport centerMapViewport(const MapViewport& viewport, float x, float y);

struct MapScreenInput {
    MapViewport viewport;
    const MapAnnotationStore* annotations = nullptr;
    // Absent when the player position is unknown.
    std::optional<WaypointPosition> player;
    float playerHeading = 0.0f; // radians clockwise from north
    const WaypointSet* waypoints = nullptr;
    WaypointSettings waypointSettings;
};

struct MapScreenAnnotation {
    std::string id;
    AnnotationKind kind = AnnotationKind::Mark;
    AnnotationSymbol symbol = AnnotationSymbol::Other;
    std::string text;
    // One point for a Mark or Note, the whole line for a Path.
    std::vector<MapScreenPoint> points;
};

struct MapScreenPlayer {
    MapScreenPoint position; // may lie outside the view
    bool onScreen = false;
    float heading = 0.0f; // radians clockwise from north, 0 when the input heading was not finite
};

struct MapScreenWaypoint {
    std::string id;
    MapScreenPoint position;
    bool onScreen = false;
    // `position` clamped into the view, for drawing an edge arrow when the waypoint is off screen.
    MapScreenPoint edge;
    float relativeBearing = 0.0f;
    float distance = 0.0f;
};

struct MapScreenView {
    bool valid = false; // false when the viewport is invalid; everything else is then empty
    std::vector<MapScreenAnnotation> annotations; // in store order, only those touching the view
    std::size_t hiddenAnnotations = 0;            // stored annotations not shown (outside the view or unusable)
    std::optional<MapScreenPlayer> player;
    // Absent unless the waypoint setting is on, an active waypoint exists, and the player is known.
    std::optional<MapScreenWaypoint> waypoint;
};

MapScreenView buildMapScreen(const MapScreenInput& input);

}
