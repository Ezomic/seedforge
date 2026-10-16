#pragma once

#include "ichor/ui/map_annotation_store.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// What an NPC says when asked the way somewhere, and the map mark that goes
// with it (docs/11-traversal-map.md: "NPC directions, which write annotations
// onto the map"). Pure logic: nothing here talks, draws or owns a map. The
// caller adds `NpcDirection::annotation` to a MapAnnotationStore itself.
//
// Map convention: +y is north, +x is east.
//
// Directions are wrong or vague by design. Whether this NPC is right is one
// draw from an explicit seed, so the same (seed, npc, target, knowledge)
// always gives the same answer and a different NPC can disagree.

enum class DirectionRole : std::uint8_t {
    Local,     // lives here, knows the roads
    Traveller, // passing through, half remembers
    Hermit,    // knows their own corner and little else
};

struct KnownLandmark {
    std::string id;
    std::string name;
    AnnotationPoint position;
};

struct DirectionTarget {
    std::string id;
    std::string name;
    std::string region;
    AnnotationPoint position;
};

struct NpcKnowledge {
    std::string npcId;
    DirectionRole role = DirectionRole::Local;
    std::string homeRegion;
    std::vector<KnownLandmark> landmarks;
    std::vector<std::string> knownTargets; // target ids this NPC has heard of
};

enum class DirectionQuality : std::uint8_t {
    Exact,   // landmark, bearing and distance are all true
    Vague,   // true landmark and bearing, no distance; the pin is a midpoint
    Wrong,   // confidently points the wrong way
    Unknown, // the NPC cannot help; no annotation
};

enum class DirectionStatus : std::uint8_t {
    Ok,
    InvalidNpc,
    InvalidTarget,
    InvalidLandmark,
    DuplicateLandmark,
    TooMuchKnowledge,
};

struct DirectionLimits {
    std::size_t maxLandmarks = 256;
    std::size_t maxKnownTargets = 256;
    std::size_t maxNameBytes = 64;
};

struct NpcDirection {
    DirectionStatus status = DirectionStatus::Ok;
    DirectionQuality quality = DirectionQuality::Unknown;
    std::string landmarkId; // the landmark the NPC described the way from; empty if none
    std::string text;       // what the NPC says, clean UTF-8
    std::optional<MapAnnotation> annotation;
};

// Chance in permille (0..1000) that this NPC gives a right answer.
int directionAccuracyPermille(DirectionRole role, bool targetInHomeRegion);

// Pure mapping of a 0..999 roll to a quality, so the boundaries are testable.
DirectionQuality directionQualityForRoll(int accuracyPermille, std::uint32_t roll);

// Stable annotation id for one NPC's directions to one target.
std::string directionAnnotationId(std::string_view npcId, std::string_view targetId);

NpcDirection giveDirection(const NpcKnowledge& npc, const DirectionTarget& target, std::uint64_t seed,
    const DirectionLimits& limits = {});

} // namespace ichor
