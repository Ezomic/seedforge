#include "ichor/ui/map_annotation_store.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

bool isContinuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

} // namespace

bool isCleanAnnotationText(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t length = 0;
        std::uint32_t minimum = 0;
        if (c < 0x80) {
            cp = c;
            length = 1;
        } else if (c >= 0xC2 && c <= 0xDF) {
            cp = c & 0x1Fu;
            length = 2;
            minimum = 0x80;
        } else if (c >= 0xE0 && c <= 0xEF) {
            cp = c & 0x0Fu;
            length = 3;
            minimum = 0x800;
        } else if (c >= 0xF0 && c <= 0xF4) {
            cp = c & 0x07u;
            length = 4;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (text.size() - i < length) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if (!isContinuation(next)) {
                return false;
            }
            cp = (cp << 6) | (next & 0x3Fu);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) {
            return false;
        }
        i += length;
    }
    return true;
}

bool isValidAnnotationId(std::string_view id, std::size_t maxIdBytes)
{
    if (id.empty() || id.size() > maxIdBytes) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

MapAnnotationStore::MapAnnotationStore(AnnotationLimits limits)
    : limits_(limits)
{
}

bool MapAnnotationStore::validPoint(AnnotationPoint point) const
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::fabs(point.x) <= limits_.maxCoordinate &&
           std::fabs(point.y) <= limits_.maxCoordinate;
}

AnnotationResult MapAnnotationStore::add(MapAnnotation annotation)
{
    if (!isValidAnnotationId(annotation.id, limits_.maxIdBytes)) {
        return AnnotationResult::InvalidId;
    }
    if (find(annotation.id) != nullptr) {
        return AnnotationResult::DuplicateId;
    }
    if (annotation.kind != AnnotationKind::Mark && annotation.kind != AnnotationKind::Note && annotation.kind != AnnotationKind::Path) {
        return AnnotationResult::InvalidShape;
    }
    if (annotation.symbol > AnnotationSymbol::Other) {
        return AnnotationResult::InvalidShape;
    }
    if (annotation.kind == AnnotationKind::Path) {
        if (annotation.points.size() < 2 || annotation.points.size() > limits_.maxPathPoints) {
            return AnnotationResult::InvalidShape;
        }
    } else if (annotation.points.size() != 1) {
        return AnnotationResult::InvalidShape;
    }
    if (annotation.text.size() > limits_.maxTextBytes) {
        return AnnotationResult::TextTooLong;
    }
    if (!isCleanAnnotationText(annotation.text) || (annotation.kind == AnnotationKind::Note && annotation.text.empty())) {
        return AnnotationResult::InvalidText;
    }
    for (const auto& point : annotation.points) {
        if (!validPoint(point)) {
            return AnnotationResult::InvalidPosition;
        }
    }
    if (items_.size() >= limits_.maxAnnotations) {
        return AnnotationResult::TooMany;
    }
    for (auto& point : annotation.points) {
        point.x += 0.0f; // turns -0.0 into +0.0
        point.y += 0.0f;
    }
    items_.push_back(std::move(annotation));
    return AnnotationResult::Ok;
}

AnnotationResult MapAnnotationStore::move(std::string_view id, AnnotationPoint to)
{
    auto it = std::find_if(items_.begin(), items_.end(), [id](const MapAnnotation& a) { return a.id == id; });
    if (it == items_.end()) {
        return AnnotationResult::UnknownId;
    }
    if (!validPoint(to)) {
        return AnnotationResult::InvalidPosition;
    }
    const float dx = to.x - it->points.front().x;
    const float dy = to.y - it->points.front().y;
    std::vector<AnnotationPoint> moved;
    moved.reserve(it->points.size());
    for (const auto& point : it->points) {
        AnnotationPoint p{point.x + dx + 0.0f, point.y + dy + 0.0f};
        if (!validPoint(p)) {
            return AnnotationResult::InvalidPosition;
        }
        moved.push_back(p);
    }
    moved.front() = {to.x + 0.0f, to.y + 0.0f};
    it->points = std::move(moved);
    return AnnotationResult::Ok;
}

AnnotationResult MapAnnotationStore::remove(std::string_view id)
{
    auto it = std::find_if(items_.begin(), items_.end(), [id](const MapAnnotation& a) { return a.id == id; });
    if (it == items_.end()) {
        return AnnotationResult::UnknownId;
    }
    items_.erase(it);
    return AnnotationResult::Ok;
}

const MapAnnotation* MapAnnotationStore::find(std::string_view id) const
{
    auto it = std::find_if(items_.begin(), items_.end(), [id](const MapAnnotation& a) { return a.id == id; });
    return it == items_.end() ? nullptr : &*it;
}

} // namespace ichor
