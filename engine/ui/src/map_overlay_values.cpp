#include "ichor/ui/map_overlay_values.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

bool lessCell(const MapOverlayCell& a, const MapOverlayCell& b)
{
    return a.y != b.y ? a.y < b.y : a.x < b.x;
}

std::uint32_t temenosStrength(const MapOverlaySources& s, const MapOverlayCell& cell)
{
    const float x = (static_cast<float>(cell.x) + 0.5f) * s.cellSize;
    const float z = (static_cast<float>(cell.y) + 0.5f) * s.cellSize;
    const float field = temenosField(s.sanctuaries, x, z);
    if (!(field > 0.0f)) {
        return 0;
    }
    const float scaled = std::min(field, kTemenosFieldMax) * static_cast<float>(kMapOverlayTemenosMax);
    return static_cast<std::uint32_t>(scaled + 0.5f);
}

}

MapOverlay computeMapOverlay(MapOverlayKind kind, std::span<const MapOverlayCell> revealed, const MapOverlaySources& sources)
{
    MapOverlay out;
    out.kind = kind;

    const bool known = kind == MapOverlayKind::Region || kind == MapOverlayKind::Territory || kind == MapOverlayKind::Temenos;
    const bool haveSource = (kind == MapOverlayKind::Region && sources.regionAt) || (kind == MapOverlayKind::Territory && sources.ownerAt)
        || (kind == MapOverlayKind::Temenos && std::isfinite(sources.cellSize) && sources.cellSize > 0.0f && sources.cellSize <= kMapOverlayMaxCellSize);
    if (!known || !haveSource) {
        out.status = MapOverlayStatus::InvalidInput;
        return out;
    }

    std::vector<MapOverlayCell> cells(revealed.begin(), revealed.end());
    std::sort(cells.begin(), cells.end(), lessCell);
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    if (cells.size() > kMaxMapOverlayCells) {
        cells.resize(kMaxMapOverlayCells);
        out.status = MapOverlayStatus::Truncated;
    }

    for (const MapOverlayCell& cell : cells) {
        std::uint32_t value = 0;
        switch (kind) {
        case MapOverlayKind::Region:
            value = sources.regionAt(cell.x, cell.y);
            if (value == kMapOverlayNone) {
                continue;
            }
            break;
        case MapOverlayKind::Territory:
            value = sources.ownerAt(cell.x, cell.y);
            if (value >= sources.factionCount) {
                continue;
            }
            break;
        case MapOverlayKind::Temenos:
            value = temenosStrength(sources, cell);
            if (value == 0) {
                continue;
            }
            break;
        }
        out.entries.push_back({cell, value});
    }
    return out;
}

}
