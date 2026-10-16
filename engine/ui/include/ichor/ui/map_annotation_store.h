#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The player's own marks on one map (docs/13-ui-hud.md: pins with typed notes,
// since quest markers are gone). Pure data: nothing here draws, and nothing
// here knows about the map screen or the save file. One store belongs to one
// map, so every limit below is a per-map limit.

enum class AnnotationKind : std::uint8_t {
    Mark, // a pin with a symbol and optional short text, one point
    Note, // a text label, one point, text required
    Path, // a hand-drawn line, two or more points, optional text
};

enum class AnnotationSymbol : std::uint8_t {
    Camp,
    Water,
    Danger,
    Loot,
    Door,
    Direction,
    Other, // keep last: the checks reject anything beyond it
};

struct AnnotationPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct AnnotationLimits {
    std::size_t maxAnnotations = 256;
    std::size_t maxIdBytes = 48;
    std::size_t maxTextBytes = 280;
    std::size_t maxPathPoints = 128;
    float maxCoordinate = 1.0e6f; // |x| and |y| must not exceed this
};

struct MapAnnotation {
    std::string id; // 1..maxIdBytes of [A-Za-z0-9_.-], unique within the store
    AnnotationKind kind = AnnotationKind::Mark;
    AnnotationSymbol symbol = AnnotationSymbol::Other;
    std::string text; // valid UTF-8, no control characters, at most maxTextBytes
    std::vector<AnnotationPoint> points;
};

enum class AnnotationResult {
    Ok,
    InvalidId,
    DuplicateId,
    UnknownId,
    InvalidText,     // bad UTF-8, a control character, or a Note with no text
    TextTooLong,
    InvalidShape,    // wrong point count for the kind, or an unknown kind or symbol
    InvalidPosition, // NaN, infinity, or beyond maxCoordinate
    TooMany,         // the per-map annotation limit is reached
};

// True for well-formed UTF-8 (no overlong forms, surrogates or values above
// U+10FFFF) with no C0, DEL or C1 control characters.
bool isCleanAnnotationText(std::string_view text);
bool isValidAnnotationId(std::string_view id, std::size_t maxIdBytes);

class MapAnnotationStore {
public:
    explicit MapAnnotationStore(AnnotationLimits limits = {});

    // Rejected annotations leave the store untouched. -0.0 is stored as +0.0.
    AnnotationResult add(MapAnnotation annotation);
    // A Mark or Note moves to `to`; a Path is translated so its first point
    // lands on `to`. Nothing changes if any resulting point would be invalid.
    AnnotationResult move(std::string_view id, AnnotationPoint to);
    AnnotationResult remove(std::string_view id);

    const MapAnnotation* find(std::string_view id) const;
    // In insertion order, which is also the order serialisation uses.
    std::span<const MapAnnotation> all() const { return items_; }
    std::size_t size() const { return items_.size(); }
    const AnnotationLimits& limits() const { return limits_; }

private:
    bool validPoint(AnnotationPoint point) const;

    AnnotationLimits limits_;
    std::vector<MapAnnotation> items_;
};

} // namespace ichor
