#include "ichor/ui/hud.h"
#include "ichor/ui/hud_layout.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ichor;

namespace {

std::vector<std::string> ids(const HudLayout& layout)
{
    std::vector<std::string> out;
    for (const HudSlot& slot : layout.slots) {
        out.emplace_back(hudElementId(slot.element));
    }
    return out;
}

bool mentions(const HudLayoutResult& result, const std::string& text)
{
    return std::any_of(result.problems.begin(), result.problems.end(), [&](const std::string& p) { return p.find(text) != std::string::npos; });
}

std::size_t drawsFor(const HudLayout& layout)
{
    RecordingUiRenderer renderer;
    UiContext ui(renderer, 1280, 720);
    REQUIRE(ui.ok());
    const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
    REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
    REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    Hud hud(ui, layout);
    REQUIRE(hud.ok());
    hud.update(0.0f, 9.0f, 0.9f, 100.0f, 100.0f);
    hud.setGuide("north, 100 m");
    ui.update(0.0);
    ui.render();
    return renderer.draws().size();
}

}

TEST_CASE("the default layout is the HUD as it has always been")
{
    const HudLayout layout = HudLayout::defaultLayout();
    REQUIRE(ids(layout) == std::vector<std::string>{"compass", "clock", "water", "health"});
    REQUIRE(std::all_of(layout.slots.begin(), layout.slots.end(), [](const HudSlot& s) { return s.visible; }));
}

TEST_CASE("a layout lists its elements in order, with optional visibility")
{
    const HudLayoutResult result = parseHudLayout(R"({"elements": [{"id": "health"}, {"id": "compass", "visible": false}, {"id": "clock", "visible": true}]})");
    REQUIRE(result.ok());
    REQUIRE(ids(result.layout) == std::vector<std::string>{"health", "compass", "clock"});
    REQUIRE(result.layout.slots[0].visible);
    REQUIRE_FALSE(result.layout.slots[1].visible);
    REQUIRE(result.layout.slots[2].visible);
}

TEST_CASE("an empty list is a valid, empty HUD")
{
    const HudLayoutResult result = parseHudLayout(R"({"elements": []})");
    REQUIRE(result.ok());
    REQUIRE(result.layout.slots.empty());
}

TEST_CASE("every problem is reported with its path")
{
    const HudLayoutResult result = parseHudLayout(R"({"elements": [{"id": "compass"}, {"id": "compass"}, {"id": "radar"}, {"id": 7}, {"visible": true}, {"id": "clock", "visible": "yes"}, "water", 3]})");
    REQUIRE_FALSE(result.ok());
    REQUIRE(mentions(result, "elements[1].id: duplicate 'compass'"));
    REQUIRE(mentions(result, "elements[2].id: unknown element 'radar'"));
    REQUIRE(mentions(result, "elements[3].id: must be a string"));
    REQUIRE(mentions(result, "elements[4].id: is required"));
    REQUIRE(mentions(result, "elements[5].visible: must be true or false"));
    REQUIRE(mentions(result, "elements[6]: must be an object"));
    REQUIRE(mentions(result, "elements[7]: must be an object"));
    REQUIRE(result.problems.size() == 7);
    REQUIRE(ids(result.layout) == ids(HudLayout::defaultLayout()));
}

TEST_CASE("the wrong shapes are rejected without throwing")
{
    for (const char* text : {"", "not json", "[]", "null", "42", "{}", R"({"elements": 5})", R"({"elements": {"id": "compass"}})", R"({"elements": null})"}) {
        INFO(text);
        const HudLayoutResult result = parseHudLayout(text);
        REQUIRE_FALSE(result.ok());
        REQUIRE(ids(result.layout) == ids(HudLayout::defaultLayout()));
    }
}

TEST_CASE("a huge list is refused and the report stays short")
{
    std::string text = R"({"elements": [)";
    for (int i = 0; i < 20000; ++i) {
        text += R"({"id": "compass"})";
        text += i + 1 < 20000 ? "," : "";
    }
    text += "]}";
    const HudLayoutResult result = parseHudLayout(text);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.problems.size() <= kMaxHudLayoutProblems + 1);
    REQUIRE(mentions(result, "more"));
    REQUIRE(ids(result.layout) == ids(HudLayout::defaultLayout()));
}

TEST_CASE("a missing layout file falls back to the default; a bad one reports why")
{
    const std::filesystem::path dir = std::filesystem::path(ICHOR_TEST_OUTPUT) / "hud_layout_file_test";
    std::filesystem::create_directories(dir);

    const HudLayoutResult missing = loadHudLayout(dir / "absent.json");
    REQUIRE(missing.ok());
    REQUIRE(ids(missing.layout) == ids(HudLayout::defaultLayout()));

    const std::filesystem::path bad = dir / "bad.json";
    std::ofstream(bad, std::ios::binary) << R"({"elements": [{"id": "radar"}]})";
    const HudLayoutResult broken = loadHudLayout(bad);
    REQUIRE_FALSE(broken.ok());
    REQUIRE(mentions(broken, "unknown element 'radar'"));

    REQUIRE_FALSE(loadHudLayout(dir).ok()); // a directory is not a layout
}

TEST_CASE("the shipped layout file is valid and is the default")
{
    const std::filesystem::path file = std::filesystem::path(ICHOR_REPO_DATA) / "ui" / "hud.json";
    REQUIRE(std::filesystem::exists(file));
    const HudLayoutResult result = loadHudLayout(file);
    REQUIRE(result.ok());
    REQUIRE(ids(result.layout) == ids(HudLayout::defaultLayout()));
}

TEST_CASE("the HUD draws only the elements its layout holds")
{
    const std::size_t full = drawsFor(HudLayout::defaultLayout());
    HudLayout reduced;
    reduced.slots = {{HudElement::Compass, true}, {HudElement::Health, true}};
    const std::size_t two = drawsFor(reduced);
    HudLayout hidden = HudLayout::defaultLayout();
    for (HudSlot& slot : hidden.slots) {
        slot.visible = false;
    }
    const std::size_t none = drawsFor(hidden);
    const std::size_t empty = drawsFor(HudLayout{});

    REQUIRE(two < full);
    REQUIRE(none < two);
    REQUIRE(none == empty);

    HudLayout hiddenClock = HudLayout::defaultLayout();
    hiddenClock.slots[1].visible = false;
    REQUIRE(drawsFor(hiddenClock) < full);
}
