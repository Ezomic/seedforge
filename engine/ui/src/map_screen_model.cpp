#include "ichor/ui/map_screen_model.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ichor {

namespace {

bool finite(float v)
{
    return std::isfinite(v);
}

bool validCoordinate(float v)
{
    return finite(v) && std::fabs(v) <= kMapViewMaxCoordinate;
}

float clampCoordinate(float v)
{
    return std::clamp(v, -kMapViewMaxCoordinate, kMapViewMaxCoordinate);
}

MapScreenPoint project(const MapViewport& vp, float x, float y)
{
    return {vp.width * 0.5f + (x - vp.centerX) * vp.zoom, vp.height * 0.5f - (y - vp.centerY) * vp.zoom};
}

bool inside(const MapViewport& vp, MapScreenPoint p)
{
    return p.x >= 0.0f && p.x <= vp.width && p.y >= 0.0f && p.y <= vp.height;
}

MapScreenPoint clampToView(const MapViewport& vp, MapScreenPoint p)
{
    return {std::clamp(p.x, 0.0f, vp.width), std::clamp(p.y, 0.0f, vp.height)};
}

bool boxTouches(const MapViewport& vp, MapScreenPoint lo, MapScreenPoint hi)
{
    return hi.x >= 0.0f && lo.x <= vp.width && hi.y >= 0.0f && lo.y <= vp.height;
}

}

bool isValidMapViewport(const MapViewport& v)
{
    return validCoordinate(v.centerX) && validCoordinate(v.centerY) && finite(v.zoom) && v.zoom >= kMapViewMinZoom &&
           v.zoom <= kMapViewMaxZoom && finite(v.width) && v.width > 0.0f && v.width <= kMapViewMaxSize &&
           finite(v.height) && v.height > 0.0f && v.height <= kMapViewMaxSize;
}

std::optional<MapScreenPoint> mapWorldToScreen(const MapViewport& viewport, float x, float y)
{
    if (!isValidMapViewport(viewport) || !validCoordinate(x) || !validCoordinate(y)) {
        return std::nullopt;
    }
    return project(viewport, x, y);
}

MapViewport panMapViewport(const MapViewport& viewport, float screenDx, float screenDy)
{
    if (!isValidMapViewport(viewport) || !finite(screenDx) || !finite(screenDy)) {
        return viewport;
    }
    MapViewport out = viewport;
    out.centerX = clampCoordinate(viewport.centerX - screenDx / viewport.zoom);
    out.centerY = clampCoordinate(viewport.centerY + screenDy / viewport.zoom);
    return out;
}

MapViewport zoomMapViewport(const MapViewport& viewport, float factor, float screenX, float screenY)
{
    if (!isValidMapViewport(viewport) || !finite(factor) || factor <= 0.0f || !finite(screenX) || !finite(screenY)) {
        return viewport;
    }
    const float zoom = std::clamp(viewport.zoom * factor, kMapViewMinZoom, kMapViewMaxZoom);
    const double ax = static_cast<double>(screenX) - viewport.width * 0.5;
    const double ay = static_cast<double>(screenY) - viewport.height * 0.5;
    const double worldX = viewport.centerX + ax / viewport.zoom;
    const double worldY = viewport.centerY - ay / viewport.zoom;
    MapViewport out = viewport;
    out.zoom = zoom;
    constexpr double kLimit = kMapViewMaxCoordinate;
    out.centerX = static_cast<float>(std::clamp(worldX - ax / zoom, -kLimit, kLimit));
    out.centerY = static_cast<float>(std::clamp(worldY + ay / zoom, -kLimit, kLimit));
    return out;
}

MapViewport centerMapViewport(const MapViewport& viewport, float x, float y)
{
    if (!isValidMapViewport(viewport) || !validCoordinate(x) || !validCoordinate(y)) {
        return viewport;
    }
    MapViewport out = viewport;
    out.centerX = x;
    out.centerY = y;
    return out;
}

MapScreenView buildMapScreen(const MapScreenInput& input)
{
    MapScreenView view;
    const MapViewport& vp = input.viewport;
    if (!isValidMapViewport(vp)) {
        return view;
    }
    view.valid = true;

    if (input.annotations != nullptr) {
        for (const MapAnnotation& a : input.annotations->all()) {
            MapScreenAnnotation shown{a.id, a.kind, a.symbol, a.text, {}};
            shown.points.reserve(a.points.size());
            MapScreenPoint lo;
            MapScreenPoint hi;
            bool ok = !a.points.empty();
            for (const AnnotationPoint& p : a.points) {
                if (!validCoordinate(p.x) || !validCoordinate(p.y)) {
                    ok = false;
                    break;
                }
                const MapScreenPoint s = project(vp, p.x, p.y);
                if (shown.points.empty()) {
                    lo = hi = s;
                } else {
                    lo = {std::min(lo.x, s.x), std::min(lo.y, s.y)};
                    hi = {std::max(hi.x, s.x), std::max(hi.y, s.y)};
                }
                shown.points.push_back(s);
            }
            if (ok && boxTouches(vp, lo, hi)) {
                view.annotations.push_back(std::move(shown));
            } else {
                ++view.hiddenAnnotations;
            }
        }
    }

    const bool playerKnown = input.player && validCoordinate(input.player->x) && validCoordinate(input.player->y);
    if (playerKnown) {
        MapScreenPlayer player;
        player.position = project(vp, input.player->x, input.player->y);
        player.onScreen = inside(vp, player.position);
        player.heading = finite(input.playerHeading) ? input.playerHeading : 0.0f;
        view.player = player;
    }

    if (playerKnown && input.waypoints != nullptr) {
        const auto guide = input.waypoints->guide(input.waypointSettings, *input.player, view.player->heading);
        const Waypoint* active = input.waypoints->active();
        if (guide && active != nullptr && validCoordinate(active->position.x) && validCoordinate(active->position.y)) {
            MapScreenWaypoint wp;
            wp.id = guide->id;
            wp.position = project(vp, active->position.x, active->position.y);
            wp.onScreen = inside(vp, wp.position);
            wp.edge = clampToView(vp, wp.position);
            wp.relativeBearing = guide->relativeBearing;
            wp.distance = guide->distance;
            view.waypoint = wp;
        }
    }
    return view;
}

}
