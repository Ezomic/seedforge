#include "ichor/ui/inventory_screen_model.h"

#include <algorithm>
#include <format>
#include <limits>

namespace ichor {

namespace {

std::string clean(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) out.push_back(c);
    }
    if (out.size() > kInventoryScreenMaxTextBytes) {
        std::size_t cut = kInventoryScreenMaxTextBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out;
}

std::uint32_t fillOf(std::uint64_t total, std::uint64_t capacity)
{
    if (capacity == 0 || total >= capacity || total > std::numeric_limits<std::uint64_t>::max() / 1000) return capacity == 0 && total == 0 ? 0 : 1000;
    return static_cast<std::uint32_t>(total * 1000 / capacity);
}

}

InventoryScreenView buildInventoryScreen(const CarryView& view, std::string_view notice)
{
    InventoryScreenView out;
    out.notice = clean(notice);
    out.level = view.level;
    out.levelName = encumbranceName(view.level);
    out.load = formatWeightUnits(view.totalUnits) + " / " + formatWeightUnits(view.capacityUnits);
    out.fillPermille = fillOf(view.totalUnits, view.capacityUnits);

    std::vector<const CarryViewRow*> sorted;
    for (const CarryViewRow& row : view.rows) {
        if (row.count > 0) sorted.push_back(&row);
    }
    std::ranges::sort(sorted, [](const CarryViewRow* a, const CarryViewRow* b) {
        if (a->itemClass != b->itemClass) return a->itemClass < b->itemClass;
        if (a->name != b->name) return a->name < b->name;
        return a->id < b->id;
    });
    for (const CarryViewRow* row : sorted) {
        if (out.rows.size() == kInventoryScreenMaxRows) {
            ++out.omitted;
            continue;
        }
        const std::string name = clean(row->name);
        out.rows.push_back({row->id, name.empty() ? clean(row->id) : name, clean(row->itemClass), std::format("x{}", row->count), formatWeightUnits(row->unitWeightUnits),
            formatWeightUnits(row->unitWeightUnits * row->count), row->usable});
    }
    return out;
}

}
