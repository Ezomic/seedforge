#pragma once

#include "ichor/ui/map_annotation_store.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Landmark navigation view model (docs/11-traversal-map.md: "landmarks over
// waypoints"). A compass bearing plus rough distance to the nearest landmarks,
// and verbal directions built from a fixed vocabulary. Pure logic: nothing
// here draws, owns a map or places a quest pin.
//
// Map convention: +y is north, +x is east. No libm, so results are identical
// on every platform.

enum class CompassPoint : std::uint8_t {
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
    NorthWest,
};

// Where a landmark lies relative to the way the player faces.
enum class RelativeSector : std::uint8_t {
    Ahead,
    AheadRight,
    Right,
    BehindRight,
    Behind,
    BehindLeft,
    Left,
    AheadLeft,
};

enum class DistanceBand : std::uint8_t {
    StonesThrow,
    ShortWalk,
    GoodWalk,
    LongWay,
};

enum class CompassStatus : std::uint8_t {
    Ok,
    InvalidPosition,
    InvalidFacing,
    InvalidLandmark,
    DuplicateLandmark,
    TooManyLandmarks,
};

struct NavLandmark {
    std::string id;
    std::string name; // lower case noun phrase, e.g. "the broken tower"
    AnnotationPoint position;
};

struct CompassLimits {
    std::size_t maxLandmarks = 1024;
    std::size_t maxNameBytes = 64;
    std::size_t maxResults = 8;
};

struct CompassEntry {
    std::string landmarkId;
    std::string name;
    CompassPoint bearing = CompassPoint::North;
    RelativeSector relative = RelativeSector::Ahead;
    DistanceBand distance = DistanceBand::StonesThrow;
    double distanceSquared = 0.0; // world units squared
    std::string text;             // e.g. "The broken tower lies north, ahead and to your right, a short walk off."
};

struct CompassReading {
    CompassStatus status = CompassStatus::Ok;
    CompassPoint facing = CompassPoint::North;
    std::vector<CompassEntry> nearest; // closest first, ties broken by id
};

struct RouteDirections {
    CompassStatus status = CompassStatus::Ok;
    bool viaLandmark = false;
    std::string viaId; // empty when no landmark helped
    std::string text;  // e.g. "North past the salt flats, then east where the broken tower stands."
};

const char* compassPointName(CompassPoint p);
const char* relativeSectorPhrase(RelativeSector s);
const char* distanceBandPhrase(DistanceBand d);

// Eight-point bearing of (dx, dy). A zero vector reports North.
CompassPoint bearingOf(double dx, double dy);

// Sector of (dx, dy) as seen by someone facing (facingX, facingY).
RelativeSector relativeSectorOf(double dx, double dy, double facingX, double facingY);

DistanceBand distanceBandFor(double distanceSquared);

// The `limits.maxResults` landmarks nearest `from`, each with its bearing,
// relative sector and a sentence. The facing vector need not be normalised
// but must be finite and non-zero.
CompassReading readCompass(AnnotationPoint from, AnnotationPoint facing, const std::vector<NavLandmark>& landmarks,
    const CompassLimits& limits = {});

// Verbal route to `target`: the bearing from `from` toward the landmark
// nearest the target (not the target itself), then the bearing from that
// landmark to the target. With no usable landmark it gives a single
// bearing and a distance band instead.
RouteDirections describeRoute(AnnotationPoint from, const NavLandmark& target, const std::vector<NavLandmark>& landmarks,
    const CompassLimits& limits = {});

} // namespace ichor
