#pragma once

#include "ichor/economy/inventory_model.h"
#include "ichor/world/carry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The inventory as a list with a weight column and an unmistakable load state (docs/13-ui-hud.md):
// no grid, nothing to hunt for. Pure and deterministic.
struct InventoryScreenRow {
    std::string id;
    std::string name;
    std::string itemClass;
    std::string count;      // "x2"
    std::string unitWeight; // one item, "0.5"
    std::string weight;     // the whole stack, "1.0"
    bool usable = false;
    bool operator==(const InventoryScreenRow&) const = default;
};

struct InventoryScreenView {
    std::vector<InventoryScreenRow> rows;
    std::size_t omitted = 0; // rows dropped by the cap
    std::string load;        // "12.4 / 40.0"
    std::string levelName;   // Light, Loaded, Heavy or Over
    EncumbranceLevel level = EncumbranceLevel::Light;
    std::uint32_t fillPermille = 0; // the share of capacity carried, held at 1000 when at or over it
    std::string notice;
    bool operator==(const InventoryScreenView&) const = default;
};

constexpr std::size_t kInventoryScreenMaxRows = 40;
constexpr std::size_t kInventoryScreenMaxTextBytes = 80;

// Rows by class, then name, then id. Text is stripped of control characters and cut to
// kInventoryScreenMaxTextBytes on a UTF-8 boundary, and a row with no name falls back to its id.
InventoryScreenView buildInventoryScreen(const CarryView& view, std::string_view notice);

}
