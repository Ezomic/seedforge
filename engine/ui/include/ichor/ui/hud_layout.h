#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The elements the HUD knows how to draw. The guide line is part of the compass.
enum class HudElement { Compass, Clock, Water, Health };

// The id an element has in the layout file: compass, clock, water or health.
const char* hudElementId(HudElement element);

// One place on the plate: which element, and whether it is drawn. A hidden element keeps its
// slot in the file but leaves no trace in the document.
struct HudSlot {
    HudElement element = HudElement::Compass;
    bool visible = true;
};

// Which elements the HUD plate holds, top to bottom. An empty layout is a valid, empty HUD.
struct HudLayout {
    std::vector<HudSlot> slots;

    // The HUD as it has always looked: compass, clock, water, health.
    static HudLayout defaultLayout();
};

constexpr std::size_t kMaxHudLayoutProblems = 16;

// A layout read from data, or why it could not be. `problems` lists the faults with their
// paths, at most kMaxHudLayoutProblems of them and then a count of the rest; when it is not
// empty the layout is the default.
struct HudLayoutResult {
    HudLayout layout;
    std::vector<std::string> problems;

    bool ok() const { return problems.empty(); }
};

// Parses {"elements": [{"id": "compass"}, {"id": "water", "visible": false}, ...]}. Unknown
// ids, repeated ids, a missing or non-string id, a non-boolean visible and any other type are
// all reported. Never throws.
HudLayoutResult parseHudLayout(std::string_view json);

// Reads a layout file. A file that does not exist is not an error: the default layout comes
// back. One that exists but cannot be read, or does not parse, is a problem.
HudLayoutResult loadHudLayout(const std::filesystem::path& path);

}
