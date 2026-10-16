#pragma once

#include <cstdint>

namespace ichor {

// Doc 11, "The scale math" and "World size is a function of the traversal set". Pure integer sizing: the speeds
// the traversal set offers become a world edge and the spacings that keep travel times inside the documented
// rhythm. Floats appear only in the input and are validated before any cast.

constexpr std::uint32_t kSizingGlanceSeconds = 75;   // doc 11: 60 to 90 s between things worth glancing at
constexpr std::uint32_t kSizingSiteSeconds = 240;    // doc 11: 3 to 5 min between sites worth entering
constexpr std::uint32_t kSizingRegionSeconds = 600;  // doc 11: 10 to 15 min to cross a region
constexpr std::uint32_t kSizingWorldSeconds = 1000;  // 4 km at the documented 4 m/s run
constexpr std::uint32_t kSizingSiteMaxSeconds = 300; // upper bound of the site rhythm
constexpr std::uint32_t kSizingRegionMaxSeconds = 900;
constexpr std::uint32_t kSizingMinEdgeMetres = 1000;
constexpr std::uint32_t kSizingMaxEdgeMetres = 16000; // doc 11: 4x the edge is 16x the content
constexpr std::uint32_t kSizingFastTravelStretchPermille = 1250;
constexpr float kSizingDefaultFootSpeed = 4.0F;

struct TraversalSet {
    float footSpeed = kSizingDefaultFootSpeed; // m/s, must be finite and above zero
    float mountSpeed = 0.0F;                   // m/s, 0 means no mounts
    float boatSpeed = 0.0F;                    // m/s, 0 means no boats
    std::uint32_t mountedShare = 0;            // permille of overland travel done mounted
    std::uint32_t boatShare = 0;               // permille of travel done by boat
    bool fastTravel = false;                   // a discovered, secured road network exists
};

enum class SizingStatus {
    Ok,
    InvalidSpeed, // a speed was NaN, infinite or negative, or the foot speed was zero; it was ignored
};

struct WorldSizing {
    SizingStatus status = SizingStatus::Ok;
    std::uint32_t effectiveSpeedMmPerS = 0;
    std::uint32_t worldEdgeMetres = 0;
    std::uint32_t regionSpanMetres = 0;
    std::uint32_t regionsPerEdge = 0;
    std::uint32_t siteSpacingMetres = 0;
    std::uint32_t glanceSpacingMetres = 0;
    bool edgeClamped = false;
};

WorldSizing sizeWorld(const TraversalSet& set);

// Whole seconds to cover a distance at a speed in mm/s; 0 when the speed is 0.
std::uint32_t travelSeconds(std::uint32_t metres, std::uint32_t speedMmPerS);

// Whether a sizing keeps the site hop and region crossing inside the documented upper bounds at its own speed.
bool sizingWithinBounds(const WorldSizing& sizing);

}
